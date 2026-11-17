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
 * Adapter session lifecycle, negotiation, state, and event validation.
 * 
 */

#define __need_minmax
#include <os/osdefs.h>
#include "session.h"
#include "private.h"

void
NetAdapterGetStatus(
    _In_  const NetworkAdapter_t* adapter,
    _Out_ NetAdapterStatus_t*     statusOut)
{
    memset(statusOut, 0, sizeof(*statusOut));
    statusOut->State = adapter->State;
    statusOut->Mtu = adapter->Mtu;
    statusOut->Mac[0] = adapter->Info.current_mac.octet0;
    statusOut->Mac[1] = adapter->Info.current_mac.octet1;
    statusOut->Mac[2] = adapter->Info.current_mac.octet2;
    statusOut->Mac[3] = adapter->Info.current_mac.octet3;
    statusOut->Mac[4] = adapter->Info.current_mac.octet4;
    statusOut->Mac[5] = adapter->Info.current_mac.octet5;
}

void
NetAdapterSetState(
    _In_ NetworkAdapter_t*    adapter,
    _In_ enum NetAdapterState state)
{
    NetAdapterStatus_t status;

    if (adapter->State == state) {
        return;
    }

    // Fire the State callback immediately when the state changes. Packets for the
    // new state only start later, so consumers always see the new state and MTU
    // before any packet that depends on them.
    adapter->State = state;
    if (adapter->Callbacks.State) {
        NetAdapterGetStatus(adapter, &status);
        adapter->Callbacks.State(adapter->Callbacks.Context, &status);
    }
}

bool
NetAdapterMatchesSession(
    _In_ const NetworkAdapter_t*              adapter,
    _In_ uuid_t                               driver,
    _In_ const struct ctt_netadapter_session* session)
{
    if (adapter == NULL || session == NULL || session->id == 0) {
        return false;
    }

    // Driver UUID, session ID, and generation must all match.
    return driver == adapter->Driver && 
           session->id == adapter->Session.id && 
           session->generation == adapter->Session.generation;
}

void
NetAdapterMarkFailed(
    _In_ NetworkAdapter_t* adapter,
    _In_ oserr_t           status)
{
    adapter->LastError = status;
    adapter->Control.Pending = false;
    
    // If a session is already open, close it with the driver before tearing down
    // shared buffers. Without a session, jump straight to FAILED.
    if (adapter->Session.id) {
        adapter->Intent.CloseRequested = true;
        NetAdapterSetState(adapter, NET_ADAPTER_CLOSING);
    } else {
        NetAdapterSetState(adapter, NET_ADAPTER_FAILED);
    }
}

void
NetAdapterConfigInitializeDefault(
    _In_ NetAdapterConfig_t* config)
{
    if (config == NULL) {
        return;
    }

    *config = (NetAdapterConfig_t){
        .TxSlots = NET_ADAPTER_TX_SLOTS_DEFAULT,
        .RxSlots = NET_ADAPTER_RX_SLOTS_DEFAULT,
        .Mtu = NET_ADAPTER_MTU_AUTO,
        .MemoryBudget = NET_ADAPTER_MEMORY_BUDGET_DEFAULT,
        .RetryMilliseconds = NET_ADAPTER_RETRY_MILLISECONDS_DEFAULT,
        .RetryLimit = NET_ADAPTER_RETRY_LIMIT_DEFAULT,
        .PollMilliseconds = NET_ADAPTER_POLL_MILLISECONDS_DEFAULT,
        .RxRetainedSlots = NET_ADAPTER_RX_RETAINED_SLOTS_DEFAULT,
        .RxCopySlots = NET_ADAPTER_RX_COPY_SLOTS_DEFAULT,
        .BatchWindow = NET_ADAPTER_BATCH_WINDOW_DEFAULT
    };
}

// TODO: Check realistic upper/lower bounds instead of only checking for zero.
static bool
__VerifyConfigurationValues(
    _In_ const NetAdapterConfig_t* config)
{
    if (config->TxSlots == 0 || config->RxSlots == 0) {
        return false;
    }

    if (config->MemoryBudget == 0) {
        return false;
    }

    if (config->RetryMilliseconds == 0) {
        return false;
    }

    if (config->RetryLimit == 0) {
        return false;
    }

    if (config->PollMilliseconds == 0) {
        return false;
    }

    if (config->MemoryBudget <= sizeof(NetworkAdapter_t)) {
        return false;
    }

    if (config->BatchWindow == 0 || config->BatchWindow > NET_ADAPTER_WINDOW_MAX) {
        return false;
    }

    return true;
}

oserr_t
NetAdapterCreate(
    _In_ uuid_t                       device,
    _In_ uuid_t                       driver,
    _In_ uint32_t                     port,
    _In_ const NetAdapterConfig_t*    config,
    _In_ const NetAdapterCallbacks_t* callbacks,
    _In_ NetworkAdapter_t**           out)
{
    NetworkAdapter_t* adapter;

    if (!device || !driver || !config || !out) {
        return OS_EINVALPARAMS;
    }
    *out = NULL;
    
    if (!__VerifyConfigurationValues(config)) {
        return OS_EINVALPARAMS;
    }
    
    adapter = calloc(1, sizeof(NetworkAdapter_t));
    if (!adapter) {
        return OS_EOOM;
    }
    
    adapter->Device = device;
    adapter->Driver = driver;
    adapter->Port = port;
    adapter->Config = *config;
    
    if (callbacks) {
        adapter->Callbacks = *callbacks;
    }

    // Every counter starts at 1 so zero stays reserved as "unset".
    adapter->Control.NextSerial = 1;
    adapter->Window.NextBatch = 1;
    adapter->Tx.QueueOrder = 1;
    adapter->Window.Run = 1;
    // Post RX buffers first so incoming traffic starts flowing before TX.
    adapter->Window.PreferRx = true;
    
    *out = adapter;
    return OS_EOK;
}

void
NetAdapterSetCallbacks(
    _In_ NetworkAdapter_t*            adapter,
    _In_ const NetAdapterCallbacks_t* callbacks)
{
    if (adapter == NULL) {
        return;
    }

    if (callbacks) {
        adapter->Callbacks = *callbacks;
    } else {
        adapter->Callbacks = (NetAdapterCallbacks_t){ 0 };
    }
}

oserr_t
NetAdapterDestroy(
    _In_ NetworkAdapter_t** adapterOut)
{
    NetworkAdapter_t* adapter;
    oserr_t           status;

    if (adapterOut == NULL) {
        return OS_EINVALPARAMS;
    } else if (*adapterOut == NULL) {
        return OS_EOK;
    }
    
    adapter = *adapterOut;
    
    // Wait until the session is fully closed or failed without an open session.
    if (adapter->State != NET_ADAPTER_CLOSED && adapter->State != NET_ADAPTER_FAILED) {
        return OS_EBUSY;
    }

    // Wait until consumers return every kept RX packet.
    if (adapter->Rx.Copy.Retained || adapter->Rx.PoolRetained) {
        return OS_EBUSY;
    }
    
    // Free shared-memory pools first. This fails with OS_EBUSY if a caller
    // still holds a TX packet handle.
    status = NetBuffersDestroy(&adapter->Buffers);
    if (status != OS_EOK) {
        return status;
    }
    
    // Free netd's private lease arrays and backup RX copy buffers.
    free(adapter->Queues[NET_ADAPTER_TX].Leases);
    free(adapter->Queues[NET_ADAPTER_RX].Leases);
    free(adapter->Rx.Copy.Entries);
    free(adapter->Rx.Copy.Bytes);
    free(adapter);
    
    *adapterOut = NULL;
    return OS_EOK;
}

/**
 * @brief Validate and cache a link status snapshot from the driver, then notify
 * the Link callback if the sequence number moved forward.
 * @param adapter Adapter to update.
 * @param link Link report from the driver.
 * @return OS_EOK if applied or safely ignored as an older report, or
 *         OS_EPROTOCOL if the report has invalid enums or conflicts with an
 *         already-seen sequence number.
 */
static oserr_t
__UpdateLink(
    _In_ NetworkAdapter_t*                 adapter,
    _In_ const struct ctt_netadapter_link* link)
{
    // Treat link reports as untrusted driver input. Require a nonzero sequence
    // for ordering and check that status and duplex fall in known ranges.
    if (!link->sequence || link->status < CTT_NETADAPTER_LINK_STATUS_UNKNOWN ||
        link->status > CTT_NETADAPTER_LINK_STATUS_UP ||
        link->duplex < CTT_NETADAPTER_DUPLEX_UNKNOWN || link->duplex > CTT_NETADAPTER_DUPLEX_FULL) {
        return OS_EPROTOCOL;
    }

    // Ignore older reports that arrive late so cached link state never rolls back.
    if (link->sequence < adapter->Link.sequence) {
        return OS_EOK;
    }

    // Same sequence with identical values is a harmless repeat. Same sequence
    // with changed values breaks protocol rules.
    if (link->sequence == adapter->Link.sequence) {
        if (link->status != adapter->Link.status) {
            return OS_EPROTOCOL;
        } else if (link->duplex != adapter->Link.duplex) {
            return OS_EPROTOCOL;
        } else if (link->speed_bps != adapter->Link.speed_bps) {
            return OS_EPROTOCOL;
        }
        return OS_EOK;
    }
    adapter->Link = *link;
    
    if (adapter->Callbacks.Link) {
        adapter->Callbacks.Link(adapter->Callbacks.Context, link);
    }
    return OS_EOK;
}

oserr_t
NetAdapterLinkChanged(
    _In_ NetworkAdapter_t*                    adapter,
    _In_ uuid_t                               driver,
    _In_ const struct ctt_netadapter_session* session,
    _In_ const struct ctt_netadapter_link*    link)
{
    oserr_t status;

    if (!NetAdapterMatchesSession(adapter, driver, session)) {
        return OS_ENOENT;
    }
    
    if (adapter->State == NET_ADAPTER_CLOSED || adapter->Intent.CloseRequested ||
        adapter->State == NET_ADAPTER_QUARANTINED) {
        return OS_ENOENT;
    }
    
    if (!link) {
        return OS_EINVALPARAMS;
    }
    
    status = __UpdateLink(adapter, link);
    if (status != OS_EOK) {
        NetAdapterMarkFailed(adapter, status);
    }
    return status;
}

void
NetAdapterRefreshCounters(
    _In_ NetworkAdapter_t* adapter)
{
    if (adapter == NULL) {
        return;
    }
    adapter->Intent.CountersNeeded = true;
}

void
NetAdapterStop(
    _In_ NetworkAdapter_t* adapter)
{
    if (adapter == NULL) {
        return;
    }
    adapter->Intent.StopRequested = true;
}

void
NetAdapterClose(
    _In_ NetworkAdapter_t* adapter)
{
    if (adapter == NULL) {
        return;
    }
    adapter->Intent.CloseRequested = true;
}

oserr_t
NetAdapterStart(
    _In_ NetworkAdapter_t* adapter)
{
    if (adapter == NULL) {
        return OS_EINVALPARAMS;
    }

    // Only a stopped adapter can begin a new traffic run.
    if (adapter->State != NET_ADAPTER_STOPPED) {
        return OS_EBUSY;
    }

    adapter->Intent.StopRequested = false;
    adapter->Intent.StopReplied   = false;

    // Each start gets a fresh run ID so packets from an earlier run are rejected.
    if (adapter->Window.Run == UINT64_MAX) {
        NetAdapterMarkFailed(adapter, OS_EOVERFLOW);
        return OS_EOVERFLOW;
    }
    adapter->Window.Run++;
    // Enter PREPARE so the scheduler re-prepares the driver and primes RX buffers.
    NetAdapterSetState(adapter, NET_ADAPTER_PREPARE);
    return OS_EOK;
}

oserr_t
NetAdapterRetry(
    _In_ NetworkAdapter_t* adapter)
{
    if (!adapter) {
        return OS_EINVALPARAMS;
    }
    
    if (adapter->State == NET_ADAPTER_FAILED && !adapter->Session.id && !adapter->Buffers) {
        // Failed before opening a session, so no pools were registered with the
        // driver. Restart from GET_INFO with a fresh serial so late replies fail match.
        NetAdapterSetState(adapter, NET_ADAPTER_INFO);
        adapter->Control.Pending = false;
        adapter->Intent.CloseRequested = adapter->Intent.StopRequested = false;
        return OS_EOK;
    }
    
    if (adapter->State != NET_ADAPTER_QUARANTINED || !adapter->Control.Pending) {
        return OS_EINVALPARAMS;
    }
    
    // Re-enter OPEN or CLOSING with the saved request intact so the driver sees
    // the exact same operation and serial on retry.
    if (adapter->Control.Request.Operation == SERVICE_CTT_NETADAPTER_OPEN_ID) {
        NetAdapterSetState(adapter, NET_ADAPTER_OPEN);
    } else {
        NetAdapterSetState(adapter, NET_ADAPTER_CLOSING);
    }
    adapter->Control.Attempts = 0;
    adapter->Control.Deadline = 0;
    return OS_EOK;
}

void
NetAdapterSnapshot(
    _In_    const NetworkAdapter_t* adapter,
    _InOut_ NetAdapterSnapshot_t*   out)
{
    if (!adapter || !out) {
        return;
    }

    memset(out, 0, sizeof(*out));
    out->State = adapter->State;
    out->Mtu = adapter->Mtu;
    out->LastError = adapter->LastError;
    out->Run = adapter->Window.Run;
    out->AdmittedBatch = adapter->Window.Admitted;
    out->RetiredBatch = adapter->Window.Retired;
    
    for (uint32_t i = 0; i < adapter->Window.Size; ++i) {
        out->PendingBatches += adapter->Window.Batches[i].Used;
    }
    
    out->Info = adapter->Info;
    out->Link = adapter->Link;
    out->Counters = adapter->Counters;
    out->RxPoolRetained = adapter->Rx.PoolRetained;
    out->RxCopyRetained = adapter->Rx.Copy.Retained;
    out->RxFallbackCopies = adapter->Rx.FallbackCopies;
    out->RxDropped = adapter->Rx.Dropped;
    
    if (adapter->Buffers) {
        NetBuffersGetStats(adapter->Buffers, &out->Buffers);
    }
}

void
NetAdapterSetProtocolError(
    _In_ NetworkAdapter_t* adapter)
{
    if (!adapter) {
        return;
    }

    if (!adapter->Control.Pending) {
        NetAdapterMarkFailed(adapter, OS_EPROTOCOL);
        return;
    }
    
    switch (adapter->Control.Request.Operation) {
        case SERVICE_CTT_NETADAPTER_OPEN_ID:
        case SERVICE_CTT_NETADAPTER_CLOSE_ID:
            adapter->LastError = OS_EPROTOCOL;
            NetAdapterSetState(adapter, NET_ADAPTER_QUARANTINED);
            break;
        default:
            NetAdapterMarkFailed(adapter, OS_EPROTOCOL);
            break;
    }
}

static bool
__VerifyNetAdapterInfo(
    _In_ NetworkAdapter_t* adapter)
{
    // Confirm the requested port index exists on this device.
    if (adapter->Port >= adapter->Info.port_count) {
        return false;
    }

    // This client only implements protocol version 2.
    if (adapter->Info.min_version > 2 || adapter->Info.max_version < 2) {
        return false;
    }

    // Minimum MTU must be positive and no larger than the maximum MTU.
    if (!adapter->Info.min_mtu || adapter->Info.min_mtu > adapter->Info.max_mtu) {
        return false;
    }

    // Batch and window limits must be nonzero, and batch size must fit inside
    // our fixed-size NetAdapterRequest_t packet array.
    if (!adapter->Info.max_batch_size || !adapter->Info.max_pending_batches ||
        adapter->Info.max_batch_size > NET_ADAPTER_BATCH_MAX) {
        return false;
    }

    return true;
}

/** 
 * @brief Validate driver capabilities from GET_INFO and pick the session's MTU,
 * batch window, and batch size before opening the session.
 */
static oserr_t
__UpdateAdapterInfo(
    _In_ NetworkAdapter_t*        adapter,
    _In_ const NetAdapterReply_t* reply)
{
    adapter->Info = reply->Info;
    
    // Treat capability values from the driver as untrusted input.
    if (!__VerifyNetAdapterInfo(adapter)) {
        return OS_EPROTOCOL;
    }
    
    // An MTU of zero selects the automatic default (driver's current MTU capped
    // at NET_ADAPTER_MTU_DEFAULT). Explicit MTU values stay unchanged.
    if (adapter->Config.Mtu) {
        adapter->Mtu = adapter->Config.Mtu;
    } else {
        adapter->Mtu = MIN(adapter->Info.current_mtu, NET_ADAPTER_MTU_DEFAULT);
        
        // Raise the automatic default up to the driver's minimum if needed.
        if (adapter->Mtu < adapter->Info.min_mtu) {
            adapter->Mtu = adapter->Info.min_mtu;
        }
    }
    
    // Make sure the chosen MTU fits device limits and leaves room for the
    // 14-byte Ethernet header without overflowing uint32_t.
    if (adapter->Mtu < adapter->Info.min_mtu || adapter->Mtu > adapter->Info.max_mtu ||
        adapter->Mtu > UINT32_MAX - NET_ADAPTER_ETHERNET_HEADER_SIZE) {
        return OS_ENOTSUPPORTED;
    }
    
    adapter->Window.Size = MIN(adapter->Config.BatchWindow, adapter->Info.max_pending_batches);
    adapter->Window.BatchSize = MIN(adapter->Info.max_batch_size, NET_ADAPTER_BATCH_MAX);
    NetAdapterSetState(adapter, NET_ADAPTER_OPEN);
    return OS_EOK;
}

void
NetAdapterReclaimClosedBuffers(
    _In_ NetworkAdapter_t* adapter)
{
    // Buffers stay alive until the driver confirms the close.
    if (adapter->State != NET_ADAPTER_CLOSED) {
        return;
    }

    // Keep pools and copy buffers allocated while consumers hold RX packets.
    if (adapter->Rx.Copy.Retained || adapter->Rx.PoolRetained) {
        return;
    }
    
    // Callers may still hold TX packets being built after close. NetBuffersDestroy
    // returns OS_EBUSY until the last one is cancelled, which calls here again.
    if (NetBuffersDestroy(&adapter->Buffers) == OS_EOK) {
        free(adapter->Queues[NET_ADAPTER_TX].Leases);
        free(adapter->Queues[NET_ADAPTER_RX].Leases);
        adapter->Queues[NET_ADAPTER_TX].Leases = NULL;
        adapter->Queues[NET_ADAPTER_RX].Leases = NULL;
        
        free(adapter->Rx.Copy.Entries);
        free(adapter->Rx.Copy.Bytes);
        adapter->Rx.Copy.Entries = NULL;
        adapter->Rx.Copy.Bytes = NULL;
    }
}

/** 
 * @brief Finish closing after a successful CLOSE reply from the driver.
 * Because the driver confirmed it stopped accessing shared memory, any worker
 * leases still queued or prepared are cancelled and released. Packets held by
 * callers (kept RX packets or TX packets being built) stay valid until returned.
 */
static oserr_t
__HandleClose(
    _In_ NetworkAdapter_t* adapter)
{
    oserr_t status = OS_EOK;
    
    if (adapter->Buffers) {
        status = NetBuffersClosed(adapter->Buffers, &adapter->Session);

        for (int poolIndex = 0; poolIndex < 2 && status == OS_EOK; ++poolIndex) {
            if (!adapter->Queues[poolIndex].Leases) {
                continue;
            }
            for (uint32_t n = 0; n < adapter->Queues[poolIndex].Slots && status == OS_EOK; ++n) {
                struct AdapterLease* entry = &adapter->Queues[poolIndex].Leases[n];
                if (__AdapterLeaseIsManagedByWorker(entry)) {
                    status = NetAdapterReleaseLease(adapter, entry, OS_ECANCELLED, false);
                }
            }
        }
    }
    
    if (status == OS_EOK) {
        memset(adapter->Window.Batches, 0, sizeof(adapter->Window.Batches));
        
        adapter->Drain.Active = false;
        adapter->Drain.Needed = false;
        NetAdapterSetState(adapter, NET_ADAPTER_CLOSED);
        NetAdapterReclaimClosedBuffers(adapter);
    }
    return status;
}

oserr_t
HandleAdapterRequest(
    _In_ NetworkAdapter_t*        adapter,
    _In_ uuid_t                   driver,
    _In_ uint64_t                 serial,
    _In_ const NetAdapterReply_t* reply,
    _In_ uint64_t                 now)
{
    oserr_t status;
    uint8_t op;

    if (!adapter || !reply) {
        return OS_EINVALPARAMS;
    }
    
    // Accept only the reply matching the active driver and pending request serial.
    // Late or misrouted replies from earlier requests must be ignored.
    if (driver != adapter->Driver || !adapter->Control.Pending || serial != adapter->Control.Request.Serial) {
        return OS_ENOENT;
    }
    
    // Reject out-of-range status codes. For OPEN, quarantine the adapter so a
    // retry can resolve a session the driver might have created.
    if (!OSERR_VALID(reply->Status)) {
        if (adapter->Control.Request.Operation == SERVICE_CTT_NETADAPTER_OPEN_ID) {
            adapter->LastError = OS_EPROTOCOL;
            NetAdapterSetState(adapter, NET_ADAPTER_QUARANTINED);
            return OS_EPROTOCOL;
        }
        NetAdapterMarkFailed(adapter, OS_EPROTOCOL);
        return OS_EPROTOCOL;
    }
    
    op = adapter->Control.Request.Operation;
    // A failed CLOSE cannot prove the driver stopped touching shared buffers.
    // Quarantine the adapter for an explicit retry instead of freeing memory.
    if (reply->Status != OS_EOK) {
        if (op == SERVICE_CTT_NETADAPTER_CLOSE_ID) {
            adapter->LastError = reply->Status;
            NetAdapterSetState(adapter, NET_ADAPTER_QUARANTINED);
            return reply->Status;
        }
        NetAdapterMarkFailed(adapter, reply->Status);
        return reply->Status;
    }
    adapter->Control.Pending = false;
    
    status = OS_EOK;
    switch (op) {
        case SERVICE_CTT_NETADAPTER_GET_INFO_ID:
            status = __UpdateAdapterInfo(adapter, reply);
            break;
        case SERVICE_CTT_NETADAPTER_OPEN_ID:
            // Require a full session identity so future messages can be matched
            // and the session can be closed later. Quarantine if missing.
            if (!reply->Session.id || !reply->Session.generation) {
                adapter->Control.Pending = true;
                NetAdapterSetState(adapter, NET_ADAPTER_QUARANTINED);
                adapter->LastError = OS_EPROTOCOL;
                return OS_EPROTOCOL;
            }
            adapter->Session = reply->Session;
            // OPEN asks for zero optional features, so a nonzero mask is invalid.
            if (reply->Value) {
                status = OS_EPROTOCOL;
                break;
            }
            status = __UpdateLink(adapter, &reply->Link);
            // Check the initial link report before allocating shared buffers.
            if (status == OS_EOK) {
                status = NetAdapterSetupBuffers(adapter);
            }
            if (status == OS_EOK) {
                NetAdapterSetState(adapter, NET_ADAPTER_REGISTER_TX);
            }
            break;
        case SERVICE_CTT_NETADAPTER_REGISTER_POOL_ID:
            status = NetBuffersRegistered(adapter->Buffers,
                                          &adapter->Session,
                                          adapter->Control.Request.Pool.direction,
                                          reply->PoolId);
            // Register RX right after TX, then configure once both pools are set.
            if (status == OS_EOK) {
                NetAdapterSetState(adapter,
                                   adapter->Control.Request.Pool.direction == CTT_NETADAPTER_DIRECTION_TX
                                           ? NET_ADAPTER_REGISTER_RX
                                           : NET_ADAPTER_CONFIGURE);
            }
            break;
        case SERVICE_CTT_NETADAPTER_CONFIGURE_ID:
            NetAdapterSetState(adapter, NET_ADAPTER_PREPARE);
            break;
        case SERVICE_CTT_NETADAPTER_PREPARE_RUN_ID:
            NetAdapterSetState(adapter, NET_ADAPTER_PRIME);
            break;
        case SERVICE_CTT_NETADAPTER_START_RUN_ID:
            NetAdapterSetState(adapter, NET_ADAPTER_RUNNING);
            // Arm the periodic completion poll timer for active traffic.
            adapter->Drain.NextPoll = __NetAdapterDeadline(now, adapter->Config.PollMilliseconds);
            break;
        case SERVICE_CTT_NETADAPTER_STOP_RUN_ID: {
            NetBufferStats_t stats;
            NetBuffersGetStats(adapter->Buffers, &stats);
            // reply->Value is the last completion sequence the driver will emit for
            // this run. It must be at least what we already saw or processed.
            if (reply->Value < stats.ProcessedCompletion || reply->Value < adapter->Window.Highest) {
                status = OS_EPROTOCOL;
                break;
            }
            adapter->Intent.StopBarrier = reply->Value;
            adapter->Intent.StopReplied = true;
            adapter->Drain.Needed = stats.ProcessedCompletion < adapter->Intent.StopBarrier;
            break;
        }
        case SERVICE_CTT_NETADAPTER_GET_LINK_ID:
            status = __UpdateLink(adapter, &reply->Link);
            adapter->Intent.LinkNeeded = false;
            break;
        case SERVICE_CTT_NETADAPTER_GET_COUNTERS_ID:
            adapter->Counters = reply->Counters;
            adapter->Intent.CountersNeeded = false;
            break;
        case SERVICE_CTT_NETADAPTER_CLOSE_ID:
            status = __HandleClose(adapter);
            break;
        default:
            status = OS_EPROTOCOL;
            break;
    }
    
    // Any setup error or bad reply triggers normal failure teardown.
    if (status != OS_EOK) {
        NetAdapterMarkFailed(adapter, status);
    }
    return status;
}
