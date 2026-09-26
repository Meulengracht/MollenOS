/** Service-side adapter registry and serialized worker.
 *
 * Public entry points take g_lock and wake the worker after changing intent.
 * The worker owns attachment, transport dispatch and lifecycle progress under that
 * same lock. Consumer hooks run inside it and must never block or reenter netd.
 * See manager.h for the private discovery/transport boundary.
 */

//#define __TRACE

#include <ddk/utils.h>
#include <event.h>
#include <io.h>
#include <ioset.h>
#include <os/usched/job.h>
#include <string.h>
#include <time.h>

#include <ctt_netadapter_service_client.h>

#include "private.h"

static struct AdapterEntry g_adapters[NET_ADAPTER_LIMIT];
static NetworkAdapterOps_t g_ops;
static mtx_t               g_lock;
static bool                g_initialized;
static int                 g_eventSet;
static int                 g_wake;

static void
__WakeWorker(void)
{
    unsigned int one = 1;
    if (g_initialized) {
        (void)write(g_wake, &one, sizeof(one));
    }
}

static void
__OnReceive(
    _In_ void*       context,
    _In_ const void* bytes,
    _In_ uint32_t    length)
{
    struct AdapterEntry* entry = context;
    if (g_ops.Receive) {
        g_ops.Receive(entry->Device, entry->Port, bytes, length);
    }
}

static void
__OnTransmitted(
    _In_ void*    context,
    _In_ uint64_t cookie,
    _In_ oserr_t  status)
{
    struct AdapterEntry* entry = context;
    if (g_ops.Transmitted) {
        g_ops.Transmitted(entry->Device, entry->Port, cookie, status);
    }
}

static void
__OnLink(
    _In_ void*                             context,
    _In_ const struct ctt_netadapter_link* link)
{
    struct AdapterEntry* entry = context;
    if (g_ops.Link) {
        g_ops.Link(entry->Device, entry->Port, link);
    }
}

static struct AdapterEntry*
__FindPort(
    _In_ uuid_t   device,
    _In_ uint32_t port)
{
    for (int i = 0; i < NET_ADAPTER_LIMIT; ++i) {
        if ((g_adapters[i].Adapter || g_adapters[i].PendingDriver) &&
            g_adapters[i].Device == device && g_adapters[i].Port == port) {
            return &g_adapters[i];
        }
    }
    return NULL;
}

struct AdapterEntry*
NetAdapterRegistryFindClient(
    _In_ gracht_client_t* client)
{
    for (int i = 0; i < NET_ADAPTER_LIMIT; ++i) {
        if (g_adapters[i].Adapter && g_adapters[i].Client == client) {
            return &g_adapters[i];
        }
    }
    return NULL;
}

/** 
 * @brief Attach only after the previous session has proved safe to close. 
 * Keep the announcement on failure so a one-shot event survives even if
 * there are failures. 
 * The worker retries with backoff using a new client endpoint.
 */
static void
AttachPendingPort(
    _In_ struct AdapterEntry* entry,
    _In_ uint64_t             now)
{
    NetworkAdapter_t*     adapter;
    NetAdapterConfig_t    config;
    gracht_client_t*      client;
    oserr_t               oserr;
    int                   status;

    // If the entry already has an adapter, or if there is no pending driver, 
    // or if the backoff period has not elapsed, then nothing to do here.
    if (entry->Adapter || !entry->PendingDriver || now < entry->AttachAfter) {
        if (entry->AttachAfter && now < entry->AttachAfter) {
            TRACE("AttachPendingPort: backoff device=%u port=%u retry_in=%llu ms",
                  entry->Device, entry->Port, entry->AttachAfter - now);
        }
        return;
    }

    // Set the next attach attempt time to enforce backoff.
    entry->AttachAfter = now + 1000;
    
    TRACE("AttachPendingPort: attempting attach device=%u port=%u driver=%u",
          entry->Device, entry->Port, entry->Driver);
    status = NetAdapterClientCreate(g_eventSet, &ctt_netadapter_client_protocol, &client);
    if (status) {
        ERROR("AttachPendingPort: failed to create NetAdapter client: %i", status);
        return;
    }

    NetAdapterDefaultConfig(&config);
    oserr = NetAdapterCreate(
        entry->Device,
        entry->PendingDriver,
        entry->Port,
        &config,
        &(NetAdapterCallbacks_t){
            __OnReceive,
            __OnTransmitted,
            __OnLink,
            entry
        },
        &adapter
    );
    if (oserr != OS_EOK) {
        ERROR("AttachPendingPort: failed to create adapter device=%u port=%u",
              entry->Device, entry->Port);
        NetAdapterClientDestroy(g_eventSet, client);
        return;
    }
    TRACE("AttachPendingPort: successfully attached device=%u port=%u",
          entry->Device, entry->Port);

    // Update the provided entry with the new adapter and client information.
    *entry = (struct AdapterEntry){
        .Adapter = adapter,
        .Device = entry->Device,
        .Driver = entry->PendingDriver,
        .Port = entry->Port,
        .Endpoint = gracht_client_iod(client),
        .Client = client
    };
}

void
NetAdapterRegistryDiscover(
    _In_ uuid_t   device,
    _In_ uuid_t   driver,
    _In_ uint32_t port)
{
    struct AdapterEntry* existing;

    TRACE("NetAdapterRegistryDiscover: device=%u driver=%u port=%u",
          device, driver, port);

    if (!device || !driver || port >= NET_ADAPTER_LIMIT) {
        if (port >= NET_ADAPTER_LIMIT) {
            WARNING("NetAdapterRegistryDiscover: port %u exceeds limit %u",
                    port, NET_ADAPTER_LIMIT);
        }
        return;
    }
    
    existing = __FindPort(device, port);
    if (existing) {
        // An existing adapter was found for this device and port.
        // Check if it needs to be replaced, it does if the following is true
        // 1. Adapter needs replacement if it is not currently attached, 
        // 2. Has been removed
        // 3. The driver has changed.
        if (!existing->Adapter || existing->Removed || existing->Driver != driver) {
            TRACE("NetAdapterRegistryDiscover: marking existing adapter for replacement device=%u port=%u",
                  device, port);
            
            existing->PendingDriver = driver;
            if (existing->Adapter) {
                existing->Removed = true;
                NetAdapterClose(existing->Adapter);
            }
        }
        return;
    }

    // No existing adapter was found for this device and port, 
    // so we look for a free slot to register a new entry.
    for (int i = 0; i < NET_ADAPTER_LIMIT; ++i) {
        struct AdapterEntry* entry = &g_adapters[i];
        if (!entry->Adapter && !entry->PendingDriver) {
            TRACE("NetAdapterRegistryDiscover: registered entry slot=%d device=%u port=%u",
                  i, device, port);
            
            *entry = (struct AdapterEntry){
                .Device = device,
                .Port = port,
                .PendingDriver = driver
            };
            return;
        }
    }
    ERROR("NetAdapterRegistryDiscover: no free slots device=%u port=%u", device, port);
}

static void
__CloseEntry(struct AdapterEntry* entry)
{
    // Mark the entry for removal and close the adapter if it is active.
    entry->PendingDriver = UUID_INVALID;
    if (entry->Adapter) {
        entry->Removed = true;
        NetAdapterClose(entry->Adapter);
    } else {
        // The adapter was not active, so we can safely clear the entry.
        memset(entry, 0, sizeof(struct AdapterEntry));
    }
}

void
NetAdapterRegistryRemove(
    _In_ uuid_t device)
{
    TRACE("NetAdapterRegistryRemove: device=%u", device);
    
    // Removal cancels pending replacement as well as closing active ports. It
    // is not a DMA fence: quarantined sessions retain their resources.
    for (int i = 0; i < NET_ADAPTER_LIMIT; ++i) {
        struct AdapterEntry* entry = &g_adapters[i];
        // Skip entries that do not match the device being removed.
        if (entry->Device != device) {
            continue;
        }
        __CloseEntry(entry);
    }
}

void
NetworkAdaptersDiscover(
    _In_ uuid_t device,
    _In_ uuid_t driver)
{
    TRACE("NetworkAdaptersDiscover: device=%u driver=%u", device, driver);

    // Validate that the network adapter system is 
    // initialized and that both the device and driver UUIDs are valid.
    if (!g_initialized || !device || !driver) {
        if (!g_initialized) {
            WARNING("NetworkAdaptersDiscover: not initialized");
        }
        return;
    }
    
    mtx_lock(&g_lock);
    // Retire secondary ports from the previous driver too. The replacement's
    // GET_INFO determines which ports to reopen; its port count may be smaller.
    for (int i = 0; i < NET_ADAPTER_LIMIT; ++i) {
        struct AdapterEntry* entry = &g_adapters[i];
        if (entry->Device == device && entry->Port &&
            (entry->Adapter || entry->PendingDriver) && entry->Driver != driver) {
            __CloseEntry(entry);
        }
    }
    NetAdapterRegistryDiscover(device, driver, 0);
    mtx_unlock(&g_lock);
    
    // Notify the worker
    __WakeWorker();
}

void
NetworkAdaptersRemove(
    _In_ uuid_t device)
{
    TRACE("NetworkAdaptersRemove: device=%u", device);
    
    // Sanitize against uninitialized
    if (!g_initialized) {
        WARNING("NetworkAdaptersRemove: not initialized");
        return;
    }

    mtx_lock(&g_lock);
    NetAdapterRegistryRemove(device);
    mtx_unlock(&g_lock);

    // Notify the worker
    __WakeWorker();
}

/** 
 * @brief Expand discovered multiport devices and reclaim only safely closed entries.
 */
static void
__UpdatePort(
    _In_ struct AdapterEntry* entry)
{
    NetAdapterSnapshot_t snapshot;

    // Take a snapshot of the current state of the network adapter.
    NetAdapterSnapshot(entry->Adapter, &snapshot);
    if (snapshot.State != entry->LastState) {
        entry->LastState = snapshot.State;

        // If the state has changed to quarantined or failed, log a warning.
        if (snapshot.State == NET_ADAPTER_QUARANTINED || snapshot.State == NET_ADAPTER_FAILED) {
            WARNING("netadapter device=%u port=%u state=%u error=%u",
                entry->Device,
                entry->Port,
                snapshot.State,
                snapshot.LastError
            );
        }
    }

    // Discover additional ports for multiport devices, if we are the
    // primary port and additional ports are available.
    if (entry->Port == 0 && !entry->Removed && snapshot.Info.port_count) {
        TRACE("UpdatePort: expanding multiport device=%u port_count=%u",
            entry->Device, snapshot.Info.port_count);
        // Start at port 1
        for (uint32_t port = 1; port < snapshot.Info.port_count; ++port) {
            if (port >= NET_ADAPTER_LIMIT) {
                WARNING("UpdatePort: port %u exceeds NET_ADAPTER_LIMIT", port);
                break;
            }
            NetAdapterRegistryDiscover(entry->Device, entry->Driver, port);
        }
    }

    if (entry->Removed &&
        (snapshot.State == NET_ADAPTER_CLOSED || snapshot.State == NET_ADAPTER_FAILED)) {
        oserr_t oserr;

        oserr = NetAdapterDestroy(&entry->Adapter);
        if (oserr == OS_EOK) {
            TRACE("UpdatePort: reclaimed device=%u port=%u", entry->Device, entry->Port);
            NetAdapterClientDestroy(g_eventSet, entry->Client);
            
            // Preserve the latest desired driver through teardown. A failed
            // close never reaches this point, so replacement cannot bypass DMA
            // quarantine; a later removal clears PendingDriver explicitly.
            *entry = (struct AdapterEntry){
                .Device = entry->Device,
                .Port = entry->Port,
                .PendingDriver = entry->PendingDriver
            };
        } else {
            WARNING("UpdatePort: failed to destroy adapter device=%u port=%u",
                    entry->Device, entry->Port);
        }
    }
}

/** 
 * @brief Sleep until readiness or the next recovery tick. A spent work budget uses
 * an immediate deadline so queued work is serviced without a timer delay.
 * @param busy Indicates whether the work budget is spent and an 
 *             immediate deadline should be used.
 */
static void
__WaitForWork(
    _In_ bool busy)
{
    struct ioset_event events[NET_ADAPTER_LIMIT + 2];
    int                count;
    struct timespec    until;
    TRACE("WaitForWork: %s", busy ? "immediate reschedule" : "waiting for work");
    
    // Readiness wakes packet work immediately; the short bounded timeout is
    // only a recovery/discovery timer tick. A full work budget reschedules
    // immediately instead of sleeping with known pending work.
    timespec_get(&until, TIME_UTC);
    if (!busy) {
        until.tv_nsec += 10000000;
        if (until.tv_nsec >= 1000000000) {
            until.tv_sec++;
            until.tv_nsec -= 1000000000;
        }
    }
    
    // Wait for events or until the specified timeout.
    count = ioset_wait(g_eventSet, events, NET_ADAPTER_LIMIT + 2, &until);
    for (int i = 0; i < count; ++i) {
        if (events[i].data.iod == g_wake) {
            unsigned int value;
            (void)read(g_wake, &value, sizeof(value));
        }
    }
}

static void
__WorkerMain(
    _In_ void* unused0,
    _In_ void* cancellationToken)
{
    (void)unused0;
    TRACE("Worker: starting");
    while (usched_is_cancelled(cancellationToken) == false) {
        struct timespec time;
        uint64_t        now;
        bool            busy = false;
        
        // Use the monotonic clock to determine the current time in milliseconds.
        timespec_get(&time, TIME_MONOTONIC);
        now = (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
        
        mtx_lock(&g_lock);
        for (int i = 0; i < NET_ADAPTER_LIMIT; ++i) {
            // Attempt to attach any pending ports for this adapter before polling it.
            AttachPendingPort(&g_adapters[i], now);
            
            // Poll the adapter if it is attached.
            if (g_adapters[i].Adapter) {
                busy |= NetAdapterTransportPoll(&g_adapters[i], now);
                __UpdatePort(&g_adapters[i]);
            }
        }
        mtx_unlock(&g_lock);

        // Wait for work to become available or until the next timeout.
        __WaitForWork(busy);
    }
}

oserr_t
NetworkAdaptersInitialize(void)
{
    struct usched_job_parameters params;
    uuid_t                       workerID;
    TRACE("NetworkAdaptersInitialize: starting");
    
    if (g_initialized) {
        WARNING("NetworkAdaptersInitialize: already initialized");
        return OS_EEXISTS;
    }
    
    if (mtx_init(&g_lock, mtx_plain) != thrd_success) {
        ERROR("NetworkAdaptersInitialize: failed to initialize mutex");
        return OS_EOOM;
    }
    
    g_eventSet = ioset(0);
    g_wake     = eventd(0, EVT_RESET_EVENT);
    if (g_eventSet < 0 || g_wake < 0) {
        ERROR("NetworkAdaptersInitialize: failed to initialize I/O resources eventSet=%d wake=%d",
              g_eventSet, g_wake);
        if (g_eventSet >= 0) {
            close(g_eventSet);
        }
        if (g_wake >= 0) {
            close(g_wake);
        }
        mtx_destroy(&g_lock);
        return OS_EUNKNOWN;
    }
    
    ioset_ctrl(
        g_eventSet,
        IOSET_ADD,
        g_wake,
        &(struct ioset_event){
            .events = IOSETSYN,
            .data.iod = g_wake
        }
    );

    usched_job_parameters_init(&params);
    params.detached = true;
    workerID = usched_job_queue3(__WorkerMain, NULL, &params);
    if (workerID == UUID_INVALID) {
        ERROR("NetworkAdaptersInitialize: failed to create worker thread");
        close(g_eventSet);
        close(g_wake);
        mtx_destroy(&g_lock);
        return OS_EOOM;
    }
    
    g_initialized = true;
    TRACE("NetworkAdaptersInitialize: completed successfully");
    return OS_EOK;
}

void
NetworkAdaptersSetHooks(
    _In_ const NetworkAdapterOps_t* ops)
{
    TRACE("NetworkAdaptersSetHooks: %s", ops ? "installing" : "clearing");

    if (!g_initialized) {
        WARNING("NetworkAdaptersSetHooks: not initialized");
        return;
    }
    
    mtx_lock(&g_lock);
    if (ops) {
        g_ops = *ops;
    } else {
        memset(&g_ops, 0, sizeof(g_ops));
    }
    mtx_unlock(&g_lock);
}

oserr_t
NetworkAdaptersSend(
    _In_ uuid_t      device,
    _In_ uint32_t    port,
    _In_ const void* data,
    _In_ uint32_t    length,
    _In_ uint64_t    cookie)
{
    struct AdapterEntry* entry;
    oserr_t              status = OS_ENOENT;
    TRACE("NetworkAdaptersSend: device=%u port=%u length=%u cookie=%lu",
          device, port, length, cookie);

    if (!g_initialized) {
        return OS_ENOTSUPPORTED;
    }
    
    mtx_lock(&g_lock);
    entry = __FindPort(device, port);
    if (entry != NULL && entry->Adapter != NULL) {
        status = NetAdapterSend(entry->Adapter, data, length, cookie);
        if (status != OS_EOK) {
            TRACE("NetworkAdaptersSend: send returned status=%i device=%u port=%u",
                status, device, port);
        }
    } else if (entry != NULL && entry->Adapter == NULL) {
        // In this case the adapter is likely migrating or unavailable.
        status = OS_EBUSY;
    } else {
        TRACE("NetworkAdaptersSend: port not found device=%u port=%u", device, port);
    }
    mtx_unlock(&g_lock);

    // Notify the worker of new work
    __WakeWorker();
    return status;
}

oserr_t
NetworkAdaptersSnapshot(
        uuid_t                device,
        uint32_t              port,
        NetAdapterSnapshot_t* out)
{
    struct AdapterEntry* entry;
    oserr_t              status = OS_ENOENT;
    TRACE("NetworkAdaptersSnapshot: device=%u port=%u", device, port);
    
    if (!out) {
        return OS_EINVALPARAMS;
    }
    
    if (!g_initialized) {
        return OS_ENOTSUPPORTED;
    }

    mtx_lock(&g_lock);
    entry = __FindPort(device, port);
    if (entry) {
        if (entry->Adapter) {
            NetAdapterSnapshot(entry->Adapter, out);
            NetAdapterRefreshCounters(entry->Adapter);
        } else {
            // Discovery is visible before local allocation succeeds. Report an
            // empty capability-discovery snapshot, never uninitialized data.
            TRACE("NetworkAdaptersSnapshot: adapter not yet allocated device=%u port=%u",
                  device, port);
            memset(out, 0, sizeof(*out));
            out->State = NET_ADAPTER_INFO;
        }
        status = OS_EOK;
    } else {
        TRACE("NetworkAdaptersSnapshot: port not found device=%u port=%u", device, port);
    }
    mtx_unlock(&g_lock);

    // Notify the worker of new events
    __WakeWorker();
    return status;
}

oserr_t
NetworkAdaptersSetRunning(
    _In_ uuid_t   device,
    _In_ uint32_t port,
    _In_ bool     start)
{
    struct AdapterEntry* entry;
    oserr_t              status = OS_ENOENT;
    TRACE("NetworkAdaptersSetRunning: device=%u port=%u %s",
          device, port, start ? "start" : "stop");

    if (!g_initialized) {
        return OS_ENOTSUPPORTED;
    }

    mtx_lock(&g_lock);
    entry = __FindPort(device, port);
    if (entry != NULL && entry->Adapter == NULL) {
        // Entry exists but adapter is not yet allocated. This typically occurs
        // during the discovery or attachment phase. Return EBUSY to indicate
        // the port is present but not yet ready for I/O operations.
        TRACE("NetworkAdaptersSetRunning: adapter not yet allocated device=%u port=%u",
              device, port);
        status = OS_EBUSY;
    } else if (entry != NULL) {
        // Entry and adapter is attached
        if (start) {
            // Start the network adapter and check for failures.
            status = NetAdapterStart(entry->Adapter);
            if (status != OS_EOK) {
                WARNING("NetworkAdaptersSetRunning: failed to start device=%u port=%u status=%i",
                        device, port, status);
            }
        } else {
            // Stop the network adapter.
            NetAdapterStop(entry->Adapter);
            status = OS_EOK;
        }
    } else {
        TRACE("NetworkAdaptersSetRunning: port not found device=%u port=%u", device, port);
    }
    mtx_unlock(&g_lock);
    
    // Notify the worker thread of the state change.
    __WakeWorker();
    return status;
}

oserr_t
NetworkAdaptersClose(
    _In_ uuid_t   device,
    _In_ uint32_t port)
{
    struct AdapterEntry* entry;
    oserr_t              status = OS_ENOENT;

    TRACE("NetworkAdaptersClose: device=%u port=%u", device, port);
    if (!g_initialized) {
        return OS_ENOTSUPPORTED;
    }
    
    mtx_lock(&g_lock);
    entry = __FindPort(device, port);
    if (entry) {
        __CloseEntry(entry);
        status = OS_EOK;
    } else {
        TRACE("NetworkAdaptersClose: port not found device=%u port=%u", device, port);
    }
    mtx_unlock(&g_lock);
    
    // Notify the worker thread of the state change.
    __WakeWorker();
    return status;
}

oserr_t
NetworkAdaptersRetry(
    _In_ uuid_t   device,
    _In_ uint32_t port)
{
    struct AdapterEntry* entry;
    oserr_t              status = OS_ENOENT;
    TRACE("NetworkAdaptersRetry: device=%u port=%u", device, port);

    if (!g_initialized) {
        return OS_ENOTSUPPORTED;
    }
    
    mtx_lock(&g_lock);
    entry = __FindPort(device, port);
    if (entry) {
        if (entry->Adapter) {
            status = NetAdapterRetry(entry->Adapter);
        } else {
            TRACE("NetworkAdaptersRetry: adapter not yet allocated device=%u port=%u",
                  device, port);
            status = OS_EBUSY;
        }
    } else {
        TRACE("NetworkAdaptersRetry: port not found device=%u port=%u", device, port);
    }
    mtx_unlock(&g_lock);
    
    // Notify the worker thread of the request.
    __WakeWorker();
    return status;
}

#ifdef VALI_NET_ADAPTER_TESTS
oserr_t
NetworkAdaptersTestAttach(
    _In_ uuid_t device,
    _In_ uuid_t driver)
{
    if (!g_initialized || !device || !driver) {
        return OS_EINVALPARAMS;
    }
    mtx_lock(&g_lock);
    NetAdapterRegistryDiscover(device, driver, 0);
    struct AdapterEntry* entry = __FindPort(device, 0);
    oserr_t status = OS_EUNKNOWN;
    if (entry && (entry->Driver == driver || entry->PendingDriver == driver)) {
        status = OS_EOK;
    }
    mtx_unlock(&g_lock);
    __WakeWorker();
    return status;
}
#endif

#ifdef VALI_VIRTIO_NET_TESTS
oserr_t
NetworkAdaptersTestFindVirtual(
    _Out_ uuid_t* deviceOut)
{
    if (!g_initialized || deviceOut == NULL) {
        return OS_EINVALPARAMS;
    }
    oserr_t status = OS_ENOENT;
    mtx_lock(&g_lock);
    for (int i = 0; i < NET_ADAPTER_LIMIT; ++i) {
        if (!g_adapters[i].Adapter) {
            continue;
        }
        NetAdapterSnapshot_t snapshot;
        NetAdapterSnapshot(g_adapters[i].Adapter, &snapshot);
        if (snapshot.Info.medium == CTT_NETADAPTER_MEDIUM_VIRTUAL) {
            *deviceOut = g_adapters[i].Device;
            status = OS_EOK;
            break;
        }
    }
    mtx_unlock(&g_lock);
    return status;
}
#endif
