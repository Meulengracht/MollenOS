/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 * 
 *
 * The public functions in this file take g_lock, record what the caller wants (for
 * example "close this port"), and then wake the worker thread. The worker does the
 * actual work under the same lock: attaching drivers, talking to them over IPC and
 * moving each adapter through its lifecycle. Consumer callbacks are called while the
 * lock is held, so they must never block or call back into netd.
 * See private.h for the internal registry and transport functions.
 */

//#define __TRACE

#include <ddk/utils.h>
#include <event.h>
#include <io.h>
#include <ioset.h>
#include <os/usched/job.h>
#include <os/usched/mutex.h>
#include <string.h>
#include <time.h>

#include <ctt_netadapter_service_client.h>

#include "private.h"

static struct AdapterEntry g_adapters[NET_ADAPTER_LIMIT];
static NetworkAdapterOps_t g_ops;
static struct usched_mtx   g_lock;
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

static bool
__OnReceivePacket(
    _In_ void*                       context,
    _In_ const NetAdapterRxPacket_t* packet)
{
    struct AdapterEntry* entry = context;
    if (!g_ops.ReceivePacket) {
        return false;
    }

    return g_ops.ReceivePacket(
        entry->Device,
        entry->Port,
        packet
    );
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

/** 
 * @brief Build the callback table for one adapter. If the consumer did not install a
 * Receive or ReceivePacket callback, the entry is left NULL instead of pointing at a
 * wrapper that does nothing. That way the adapter can tell nobody is listening: it
 * counts frames without a receiver as dropped, and skips copying frames for a consumer
 * that cannot keep them.
 */
static NetAdapterCallbacks_t
__AdapterCallbacks(struct AdapterEntry* entry)
{
    return (NetAdapterCallbacks_t){
        .Receive = g_ops.Receive ? __OnReceive : NULL,
        .Transmitted = __OnTransmitted,
        .Link = __OnLink,
        .Context = entry,
        .ReceivePacket = g_ops.ReceivePacket ? __OnReceivePacket : NULL
    };
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
 * @brief Create the adapter for a port that has a driver waiting to be attached. This
 * only happens once the port's previous adapter, if any, has been fully closed and
 * cleaned up. If attaching fails, the waiting driver is kept on the entry, so a
 * discovery event that is only sent once is not lost. The worker tries again at most
 * once per second, creating a new client connection for each attempt.
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

    // Nothing to do if the entry already has an adapter, if no driver is waiting
    // to be attached, or if the one second wait after the last attempt is not over.
    if (entry->Adapter || !entry->PendingDriver || now < entry->AttachAfter) {
        if (entry->AttachAfter && now < entry->AttachAfter) {
            TRACE("AttachPendingPort: backoff device=%u port=%u retry_in=%llu ms",
                  entry->Device, entry->Port, entry->AttachAfter - now);
        }
        return;
    }

    // Do not try again for one second, so a failing driver is not retried in a tight loop.
    entry->AttachAfter = now + 1000;
    
    TRACE("AttachPendingPort: attempting attach device=%u port=%u driver=%u",
          entry->Device, entry->Port, entry->Driver);
    status = NetAdapterClientCreate(g_eventSet, &ctt_netadapter_client_protocol, &client);
    if (status) {
        ERROR("AttachPendingPort: failed to create NetAdapter client: %i", status);
        return;
    }

    NetAdapterConfigInitializeDefault(&config);
    NetAdapterCallbacks_t callbacks = __AdapterCallbacks(entry);
    oserr = NetAdapterCreate(
        entry->Device,
        entry->PendingDriver,
        entry->Port,
        &config,
        &callbacks,
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
    
    // Removal forgets any driver waiting to be attached and asks active ports to
    // close. It does not guarantee the device has stopped writing to our buffers;
    // ports stuck in the quarantined state keep their buffers until that is known.
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
    
    usched_mtx_lock(&g_lock);
    // When the driver changes, also close the extra ports (port 1 and up) set up by
    // the old driver. Only port 0 is registered here; the new driver's GET_INFO reply
    // tells us how many ports it has, which may be fewer than before.
    for (int i = 0; i < NET_ADAPTER_LIMIT; ++i) {
        struct AdapterEntry* entry = &g_adapters[i];
        if (entry->Device == device && entry->Port &&
            (entry->Adapter || entry->PendingDriver) && entry->Driver != driver) {
            __CloseEntry(entry);
        }
    }
    NetAdapterRegistryDiscover(device, driver, 0);
    usched_mtx_unlock(&g_lock);
    
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

    usched_mtx_lock(&g_lock);
    NetAdapterRegistryRemove(device);
    usched_mtx_unlock(&g_lock);

    // Notify the worker
    __WakeWorker();
}

/** 
 * @brief Periodic housekeeping for one attached port. Logs state changes, registers the
 * extra ports of a device with more than one port once port 0 has learned how many
 * there are, and frees the adapter of a removed port once it has fully closed or
 * failed.
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
            
            // Keep the driver waiting to be attached, if any, so a replacement can
            // start right away. A close the driver never confirmed leaves the port
            // quarantined and never gets here, so a new driver cannot be attached while
            // the device might still write to the old buffers. A later removal clears
            // PendingDriver explicitly.
            *entry = (struct AdapterEntry){
                .Device = entry->Device,
                .Port = entry->Port,
                .PendingDriver = entry->PendingDriver
            };
        } else if (oserr != OS_EBUSY) {
            // Only errors other than OS_EBUSY are logged. OS_EBUSY is expected while
            // callers still hold packets after the close; returning the last of them
            // wakes the worker, and we try to free the adapter again then.
            WARNING("UpdatePort: failed to destroy adapter device=%u port=%u",
                    entry->Device, entry->Port);
        }
    }
}

/** 
 * @brief Sleep until there is something to do, or for at most 10 ms so timed work such
 * as retries still runs. If the last pass did not finish all queued work, return
 * right away instead of sleeping.
 * @param busy true if the last pass stopped early with work still queued, in which
 *             case the worker should not sleep.
 */
static void
__WaitForWork(
    _In_ bool busy)
{
    struct ioset_event events[NET_ADAPTER_LIMIT + 2];
    int                count;
    struct timespec    until;
    TRACE("WaitForWork: %s", busy ? "immediate reschedule" : "waiting for work");
    
    // Packet work is woken immediately when a client becomes readable. The short
    // timeout below only exists so retries and device discovery still run when
    // nothing else happens. If the work budget was used up, there is more work
    // waiting, so we reschedule immediately instead of sleeping.
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
        
        // Use a clock that never jumps backwards, so retry timers are not upset by
        // changes to the wall-clock time. The value is in milliseconds.
        timespec_get(&time, TIME_MONOTONIC);
        now = (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
        
        usched_mtx_lock(&g_lock);
        for (int i = 0; i < NET_ADAPTER_LIMIT; ++i) {
            // Attempt to attach any pending ports for this adapter before polling it.
            AttachPendingPort(&g_adapters[i], now);
            
            // Poll the adapter if it is attached.
            if (g_adapters[i].Adapter) {
                busy |= NetAdapterTransportPoll(&g_adapters[i], now);
                __UpdatePort(&g_adapters[i]);
            }
        }
        usched_mtx_unlock(&g_lock);

        // Wait for work to become available or until the next timeout.
        __WaitForWork(busy);
    }
}

oserr_t
NetworkAdaptersInitialize(void)
{
    struct usched_job_parameters params;
    uuid_t                       workerID;
    int                          status;
    TRACE("NetworkAdaptersInitialize: starting");
    
    if (g_initialized) {
        WARNING("NetworkAdaptersInitialize: already initialized");
        return OS_EEXISTS;
    }
    
    usched_mtx_init(&g_lock, USCHED_MUTEX_PLAIN);
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
        return OS_EUNKNOWN;
    }
    
    status = ioset_ctrl(
        g_eventSet,
        IOSET_ADD,
        g_wake,
        &(struct ioset_event){
            .events = IOSETSYN,
            .data.iod = g_wake
        }
    );
    if (status < 0) {
        ERROR("NetworkAdaptersInitialize: failed to add wake event to event set");
        close(g_eventSet);
        close(g_wake);
        return OS_EUNKNOWN;
    }

    usched_job_parameters_init(&params);
    params.detached = true;
    workerID = usched_job_queue3(__WorkerMain, NULL, &params);
    if (workerID == UUID_INVALID) {
        ERROR("NetworkAdaptersInitialize: failed to create worker thread");
        close(g_eventSet);
        close(g_wake);
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
    
    usched_mtx_lock(&g_lock);
    if (ops) {
        g_ops = *ops;
    } else {
        memset(&g_ops, 0, sizeof(g_ops));
    }
    
    // Go through the adapters and update their callbacks
    for (int i = 0; i < NET_ADAPTER_LIMIT; ++i) {
        if (g_adapters[i].Adapter) {
            NetAdapterCallbacks_t callbacks = __AdapterCallbacks(&g_adapters[i]);
            NetAdapterSetCallbacks(g_adapters[i].Adapter, &callbacks);
        }
    }
    usched_mtx_unlock(&g_lock);
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
    
    usched_mtx_lock(&g_lock);
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
    usched_mtx_unlock(&g_lock);

    // Notify the worker of new work
    __WakeWorker();
    return status;
}

oserr_t
NetworkAdaptersTxAcquire(uuid_t device, uint32_t port, NetAdapterTxPacket_t* packet)
{
    if (!packet) {
        return OS_EINVALPARAMS;
    }
    memset(packet, 0, sizeof(*packet));
    if (!g_initialized) {
        return OS_ENOTSUPPORTED;
    }
    usched_mtx_lock(&g_lock);
    struct AdapterEntry* entry = __FindPort(device, port);
    oserr_t status = OS_ENOENT;
    if (entry) {
        status = entry->Adapter ? NetAdapterTxAcquire(entry->Adapter, packet) : OS_EBUSY;
    }
    usched_mtx_unlock(&g_lock);
    return status;
}

oserr_t
NetworkAdaptersTxSubmit(uuid_t device, uint32_t port, NetAdapterTxPacket_t* packet,
                        uint32_t length, uint64_t cookie)
{
    if (!g_initialized) {
        return OS_ENOTSUPPORTED;
    }
    usched_mtx_lock(&g_lock);
    struct AdapterEntry* entry = __FindPort(device, port);
    oserr_t status = OS_ENOENT;
    if (entry && entry->Adapter) {
        status = NetAdapterTxSubmit(entry->Adapter, packet, length, cookie);
    }
    usched_mtx_unlock(&g_lock);
    __WakeWorker();
    return status;
}

oserr_t
NetworkAdaptersTxCancel(uuid_t device, uint32_t port, NetAdapterTxPacket_t* packet)
{
    if (!g_initialized) {
        return OS_ENOTSUPPORTED;
    }
    usched_mtx_lock(&g_lock);
    // A removed port stays in the table until every packet taken from it has been
    // returned, so this lookup still works after removal.
    struct AdapterEntry* entry = __FindPort(device, port);
    oserr_t status = OS_ENOENT;
    if (entry && entry->Adapter) {
        status = NetAdapterTxCancel(entry->Adapter, packet);
    }
    usched_mtx_unlock(&g_lock);
    __WakeWorker();
    return status;
}

oserr_t
NetworkAdaptersRxRelease(
    _In_ uuid_t                device,
    _In_ uint32_t              port,
    _In_ NetAdapterRxPacket_t* packet)
{
    struct AdapterEntry* entry;
    oserr_t              status = OS_ENOENT;

    if (!g_initialized) {
        return OS_ENOTSUPPORTED;
    }

    // RX packets held by a consumer keep a removed entry 
    // alive until the final release.
    usched_mtx_lock(&g_lock);
    entry = __FindPort(device, port);
    if (entry && entry->Adapter) {
        status = NetAdapterRxRelease(
            entry->Adapter,
            packet
        );
    }
    usched_mtx_unlock(&g_lock);

    // Notify the worker of new events
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

    usched_mtx_lock(&g_lock);
    entry = __FindPort(device, port);
    if (entry) {
        if (entry->Adapter) {
            NetAdapterSnapshot(entry->Adapter, out);
            NetAdapterRefreshCounters(entry->Adapter);
        } else {
            // The port is visible as soon as it is discovered, before its adapter
            // is created. Report an empty snapshot in the INFO state rather than
            // leaving the output uninitialized.
            TRACE("NetworkAdaptersSnapshot: adapter not yet allocated device=%u port=%u",
                  device, port);
            memset(out, 0, sizeof(*out));
            out->State = NET_ADAPTER_INFO;
        }
        status = OS_EOK;
    } else {
        TRACE("NetworkAdaptersSnapshot: port not found device=%u port=%u", device, port);
    }
    usched_mtx_unlock(&g_lock);

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

    usched_mtx_lock(&g_lock);
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
    usched_mtx_unlock(&g_lock);
    
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
    
    usched_mtx_lock(&g_lock);
    entry = __FindPort(device, port);
    if (entry) {
        __CloseEntry(entry);
        status = OS_EOK;
    } else {
        TRACE("NetworkAdaptersClose: port not found device=%u port=%u", device, port);
    }
    usched_mtx_unlock(&g_lock);
    
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
    
    usched_mtx_lock(&g_lock);
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
    usched_mtx_unlock(&g_lock);
    
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
    usched_mtx_lock(&g_lock);
    NetAdapterRegistryDiscover(device, driver, 0);
    struct AdapterEntry* entry = __FindPort(device, 0);
    oserr_t status = OS_EUNKNOWN;
    if (entry && (entry->Driver == driver || entry->PendingDriver == driver)) {
        status = OS_EOK;
    }
    usched_mtx_unlock(&g_lock);
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
    usched_mtx_lock(&g_lock);
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
    usched_mtx_unlock(&g_lock);
    return status;
}
#endif
