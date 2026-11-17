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
 * Global adapter registry, locking, and worker loop.
 *
 * Public functions here take g_lock, record what the caller wants (like closing
 * a port or queueing a frame), and wake the worker thread. The worker runs all
 * adapter logic under that same lock: attaching drivers, polling Gracht IPC, and
 * advancing each port's state machine. Consumer callbacks run with g_lock held,
 * so they must never block or call back into netd.
 *
 * See private.h for internal registry and transport helpers.
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
static uint64_t            g_nextInstanceId = 1;
static NetworkAdapterOps_t g_ops;
static struct usched_mtx   g_lock;
static bool                g_initialized;
static int                 g_eventSet;
static int                 g_wake;

// Write to the reset-event descriptor so ioset_wait wakes up right away when a
// caller queues new work from another thread.
static void
__WakeWorker(void)
{
    unsigned int one = 1;
    if (g_initialized) {
        (void)write(g_wake, &one, sizeof(one));
    }
}

// The callbacks below wrap the consumer's global NetworkAdapterOps_t table,
// tagging each adapter event with the entry's Device, Port, and InstanceId.
static void
__OnReceive(
    _In_ void*       context,
    _In_ const void* bytes,
    _In_ uint32_t    length)
{
    struct AdapterEntry* entry = context;
    if (g_ops.Receive) {
        g_ops.Receive(entry->Device, entry->Port, entry->InstanceId, bytes, length);
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
        entry->InstanceId,
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
        g_ops.Transmitted(entry->Device, entry->Port, entry->InstanceId, cookie, status);
    }
}

static void
__OnLink(
    _In_ void*                             context,
    _In_ const struct ctt_netadapter_link* link)
{
    struct AdapterEntry* entry = context;
    if (g_ops.Link) {
        g_ops.Link(entry->Device, entry->Port, entry->InstanceId, link);
    }
}

static void
__OnState(
    _In_ void*                     context,
    _In_ const NetAdapterStatus_t* status)
{
    struct AdapterEntry* entry = context;
    if (g_ops.State) {
        g_ops.State(
            entry->Device,
            entry->Port,
            entry->InstanceId,
            status
        );
    }
}

/** 
 * @brief Build the callback table for one adapter entry.
 * Leaves Receive or ReceivePacket null if the consumer did not install one.
 * That lets rx.c know when no consumer is listening so it skips backup copies
 * and counts unhandled frames as dropped.
 */
static NetAdapterCallbacks_t
__AdapterCallbacks(struct AdapterEntry* entry)
{
    return (NetAdapterCallbacks_t){
        .Receive = g_ops.Receive ? __OnReceive : NULL,
        .Transmitted = __OnTransmitted,
        .Link = __OnLink,
        .State = __OnState,
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

/**
 * @brief Look up a port only if its active session matches instance.
 * Stops requests meant for an old driver session from hitting its replacement
 * on the same device and port.
 */
static struct AdapterEntry*
__FindInstance(
    _In_ uuid_t   device,
    _In_ uint32_t port,
    _In_ uint64_t instance)
{
    struct AdapterEntry* entry = __FindPort(device, port);
    if (entry && (instance == 0 || entry->InstanceId != instance)) {
        return NULL;
    }
    return entry;
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
 * @brief Attach a pending driver to an empty registry slot.
 * Only runs once any prior adapter on that port has finished closing and been
 * freed. On failure, PendingDriver stays set so the worker retries once per
 * second without losing a one-shot device discovery event.
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

    // Skip if already attached, if no driver is waiting, or while waiting out
    // the one-second retry backoff.
    if (entry->Adapter || !entry->PendingDriver || now < entry->AttachAfter) {
        if (entry->AttachAfter && now < entry->AttachAfter) {
            TRACE("AttachPendingPort: backoff device=%u port=%u retry_in=%llu ms",
                  entry->Device, entry->Port, entry->AttachAfter - now);
        }
        return;
    }

    // Stop attaching if 64-bit instance IDs ever reach the limit, since a wrap
    // could make a stale instance ID match a brand-new session.
    if (g_nextInstanceId == UINT64_MAX) {
        ERROR("AttachPendingPort: adapter instance identities are used up");
        return;
    }

    // Space attach attempts at least one second apart so a failing driver cannot
    // spin the worker loop.
    entry->AttachAfter = now + NET_ADAPTER_ATTACH_RETRY_MILLISECONDS;
    
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
        .InstanceId = g_nextInstanceId++,
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
        // Close and replace an existing entry when it has no active adapter, is
        // already marked removed, or switched to a different driver UUID.
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

    // No existing entry matched. Take the first free registry slot.
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
    // Drop any pending replacement driver. If an adapter is active, start an
    // orderly close. Otherwise clear the slot right away.
    entry->PendingDriver = UUID_INVALID;
    if (entry->Adapter) {
        entry->Removed = true;
        NetAdapterClose(entry->Adapter);
    } else {
        memset(entry, 0, sizeof(struct AdapterEntry));
    }
}

void
NetAdapterRegistryRemove(
    _In_ uuid_t device)
{
    TRACE("NetAdapterRegistryRemove: device=%u", device);
    
    // Clear waiting drivers and close all open ports for this device. Quarantined
    // ports hold their shared buffers until a retry confirms the close.
    for (int i = 0; i < NET_ADAPTER_LIMIT; ++i) {
        struct AdapterEntry* entry = &g_adapters[i];
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

    if (!g_initialized || !device || !driver) {
        if (!g_initialized) {
            WARNING("NetworkAdaptersDiscover: not initialized");
        }
        return;
    }
    
    usched_mtx_lock(&g_lock);
    // When a driver changes on a device, close any extra ports (port 1+) created
    // by the old driver. Only port 0 starts here, and extra ports get added once
    // the new driver replies to GET_INFO with its port_count.
    for (int i = 0; i < NET_ADAPTER_LIMIT; ++i) {
        struct AdapterEntry* entry = &g_adapters[i];
        if (entry->Device == device && entry->Port &&
            (entry->Adapter || entry->PendingDriver) && entry->Driver != driver) {
            __CloseEntry(entry);
        }
    }
    NetAdapterRegistryDiscover(device, driver, 0);
    usched_mtx_unlock(&g_lock);
    
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
 * @brief Run periodic housekeeping on one attached port: log failures, discover
 * extra ports on multiport devices, and destroy removed ports once closed.
 */
static void
__UpdatePort(
    _In_ struct AdapterEntry* entry)
{
    NetAdapterSnapshot_t snapshot;

    NetAdapterSnapshot(entry->Adapter, &snapshot);
    if (snapshot.State != entry->LastState) {
        entry->LastState = snapshot.State;

        if (snapshot.State == NET_ADAPTER_QUARANTINED || snapshot.State == NET_ADAPTER_FAILED) {
            WARNING("netadapter device=%u port=%u state=%u error=%u",
                entry->Device,
                entry->Port,
                snapshot.State,
                snapshot.LastError
            );
        }
    }

    // Once port 0 receives GET_INFO, register ports 1..port_count-1.
    if (entry->Port == 0 && !entry->Removed && snapshot.Info.port_count) {
        TRACE("UpdatePort: expanding multiport device=%u port_count=%u",
            entry->Device, snapshot.Info.port_count);
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
            
            // Preserve PendingDriver so a replacement driver attaches next.
            // Quarantined ports never reach CLOSED, keeping old buffers safe
            // from any replacement driver until resolved or explicitly removed.
            *entry = (struct AdapterEntry){
                .Device = entry->Device,
                .Port = entry->Port,
                .PendingDriver = entry->PendingDriver
            };
        } else if (oserr != OS_EBUSY) {
            // OS_EBUSY is normal while callers still hold RX or TX packets.
            // Returning the final packet wakes the worker to try again.
            WARNING("UpdatePort: failed to destroy adapter device=%u port=%u",
                    entry->Device, entry->Port);
        }
    }
}

/** 
 * @brief Wait for an IPC event or wake signal, up to NET_ADAPTER_WORKER_WAIT_MILLISECONDS.
 * @param busy true if a port hit its per-poll work budget and still has queued
 *             work, in which case we poll again immediately without sleeping.
 */
static void
__WaitForWork(
    _In_ bool busy)
{
    struct ioset_event events[NET_ADAPTER_LIMIT + 2];
    int                count;
    struct timespec    until;
    TRACE("WaitForWork: %s", busy ? "immediate reschedule" : "waiting for work");
    
    // Client traffic and API calls wake ioset_wait right away. The short sleep
    // only drives retry timers and periodic polls when traffic is idle.
    timespec_get(&until, TIME_UTC);
    if (!busy) {
        until.tv_nsec += NET_ADAPTER_WORKER_WAIT_MILLISECONDS * 1000000;
        if (until.tv_nsec >= 1000000000) {
            until.tv_sec++;
            until.tv_nsec -= 1000000000;
        }
    }
    
    count = ioset_wait(g_eventSet, events, NET_ADAPTER_LIMIT + 2, &until);
    for (int i = 0; i < count; ++i) {
        // Drain the wake signal so the next ioset_wait sleeps normally.
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
        
        // Use a monotonic clock in milliseconds so wall-clock adjustments never
        // break retry deadlines.
        timespec_get(&time, TIME_MONOTONIC);
        now = (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
        
        usched_mtx_lock(&g_lock);
        for (int i = 0; i < NET_ADAPTER_LIMIT; ++i) {
            AttachPendingPort(&g_adapters[i], now);
            
            if (g_adapters[i].Adapter) {
                busy |= NetAdapterTransportPoll(&g_adapters[i], now);
                __UpdatePort(&g_adapters[i]);
            }
        }
        usched_mtx_unlock(&g_lock);

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
    
    // Update every active adapter and bring late consumers up to date with
    // each port's current state and link before any new packets arrive.
    for (int i = 0; i < NET_ADAPTER_LIMIT; ++i) {
        if (g_adapters[i].Adapter) {
            NetAdapterCallbacks_t callbacks = __AdapterCallbacks(&g_adapters[i]);
            NetAdapterSetCallbacks(g_adapters[i].Adapter, &callbacks);
            
            if (ops && ops->State) {
                NetAdapterStatus_t status;
                NetAdapterGetStatus(g_adapters[i].Adapter, &status);
                ops->State(g_adapters[i].Device, g_adapters[i].Port, g_adapters[i].InstanceId, &status);
            }

            // Link updates only fire when the link changes. Replay the cached
            // link if sequence != 0 (meaning the driver already reported one).
            if (ops && ops->Link && g_adapters[i].Adapter->Link.sequence != 0) {
                ops->Link(g_adapters[i].Device, g_adapters[i].Port, g_adapters[i].InstanceId,
                          &g_adapters[i].Adapter->Link);
            }
        }
    }
    usched_mtx_unlock(&g_lock);
}

oserr_t
NetworkAdaptersSend(
    _In_ uuid_t      device,
    _In_ uint32_t    port,
    _In_ uint64_t    instance,
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
    entry = __FindInstance(device, port, instance);
    if (entry != NULL && entry->Adapter != NULL) {
        status = NetAdapterSend(entry->Adapter, data, length, cookie);
        if (status != OS_EOK) {
            TRACE("NetworkAdaptersSend: send returned status=%i device=%u port=%u",
                status, device, port);
        }
    } else if (entry != NULL && entry->Adapter == NULL) {
        // Port is registered and waiting for its driver to attach.
        status = OS_EBUSY;
    } else {
        TRACE("NetworkAdaptersSend: port not found device=%u port=%u", device, port);
    }
    usched_mtx_unlock(&g_lock);

    __WakeWorker();
    return status;
}

oserr_t
NetworkAdaptersTxAcquire(
    _In_ uuid_t                device,
    _In_ uint32_t              port,
    _In_ uint64_t              instance,
    _In_ NetAdapterTxPacket_t* packet)
{
    struct AdapterEntry* entry;
    oserr_t              status = OS_ENOENT;

    if (!packet) {
        return OS_EINVALPARAMS;
    }

    memset(packet, 0, sizeof(*packet));
    
    if (!g_initialized) {
        return OS_ENOTSUPPORTED;
    }

    // No worker wake needed here because acquiring a slot queues nothing for the driver yet.
    usched_mtx_lock(&g_lock);
    entry = __FindInstance(device, port, instance);
    if (entry) {
        status = entry->Adapter ? NetAdapterTxAcquire(entry->Adapter, packet) : OS_EBUSY;
    }
    usched_mtx_unlock(&g_lock);
    return status;
}

oserr_t
NetworkAdaptersTxSubmit(
    _In_ uuid_t                device,
    _In_ uint32_t              port,
    _In_ uint64_t              instance,
    _In_ NetAdapterTxPacket_t* packet,
    _In_ uint32_t              length,
    _In_ uint64_t              cookie)
{
    struct AdapterEntry* entry;
    oserr_t              status = OS_ENOENT;

    if (!g_initialized) {
        return OS_ENOTSUPPORTED;
    }

    usched_mtx_lock(&g_lock);
    entry = __FindInstance(device, port, instance);
    if (entry && entry->Adapter) {
        status = NetAdapterTxSubmit(entry->Adapter, packet, length, cookie);
    }
    usched_mtx_unlock(&g_lock);
    __WakeWorker();
    return status;
}

oserr_t
NetworkAdaptersTxCancel(
    _In_ uuid_t                device,
    _In_ uint32_t              port,
    _In_ uint64_t              instance,
    _In_ NetAdapterTxPacket_t* packet)
{
    struct AdapterEntry* entry;
    oserr_t              status = OS_ENOENT;

    if (!g_initialized) {
        return OS_ENOTSUPPORTED;
    }
    
    usched_mtx_lock(&g_lock);
    // Removed ports stay in g_adapters until every held packet returns, so
    // cancel still finds the adapter after device removal.
    entry = __FindInstance(device, port, instance);
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
    _In_ uint64_t              instance,
    _In_ NetAdapterRxPacket_t* packet)
{
    struct AdapterEntry* entry;
    oserr_t              status = OS_ENOENT;

    if (!g_initialized) {
        return OS_ENOTSUPPORTED;
    }

    // Kept RX packets hold the removed entry open until the final release.
    usched_mtx_lock(&g_lock);
    entry = __FindInstance(device, port, instance);
    if (entry && entry->Adapter) {
        status = NetAdapterRxRelease(
            entry->Adapter,
            packet
        );
    }
    usched_mtx_unlock(&g_lock);

    // Wake the worker so freed RX slots can be posted back to the driver.
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
            out->InstanceId = entry->InstanceId;
            NetAdapterRefreshCounters(entry->Adapter);
        } else {
            // Port is discovered but still waiting to attach. Report an empty
            // snapshot in NET_ADAPTER_INFO so callers get clean zeroes.
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
        // Port is discovered, though its adapter object has not been created yet.
        TRACE("NetworkAdaptersSetRunning: adapter not yet allocated device=%u port=%u",
              device, port);
        status = OS_EBUSY;
    } else if (entry != NULL) {
        if (start) {
            status = NetAdapterStart(entry->Adapter);
            if (status != OS_EOK) {
                WARNING("NetworkAdaptersSetRunning: failed to start device=%u port=%u status=%i",
                        device, port, status);
            }
        } else {
            NetAdapterStop(entry->Adapter);
            status = OS_EOK;
        }
    } else {
        TRACE("NetworkAdaptersSetRunning: port not found device=%u port=%u", device, port);
    }
    usched_mtx_unlock(&g_lock);
    
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
