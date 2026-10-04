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

bool
NetAdapterMatchesSession(
    _In_ const NetworkAdapter_t*              adapter,
    _In_ uuid_t                               driver,
    _In_ const struct ctt_netadapter_session* session)
{
    if (adapter == NULL || session == NULL || session->id == 0) {
        return false;
    }

    // Driver, session id+generation must be identical
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
    
    if (adapter->Session.id) {
        adapter->Intent.CloseRequested = true;
        adapter->State = NET_ADAPTER_CLOSING;
    } else {
        adapter->State = NET_ADAPTER_FAILED;
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

// TODO: This needs sane values and not just 0 checks
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

    // Reset counters
    adapter->Control.NextSerial = 1;
    adapter->Window.NextBatch = 1;
    adapter->Tx.QueueOrder = 1;
    adapter->Window.Run = 1;
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
    
    // Only safe to destroy an adapter that is closed or has failed
    if (adapter->State != NET_ADAPTER_CLOSED && adapter->State != NET_ADAPTER_FAILED) {
        return OS_EBUSY;
    }

    // Only safe to destroy an adapter that has no retained RX resources
    if (adapter->Rx.Copy.Retained || adapter->Rx.PoolRetained) {
        return OS_EBUSY;
    }
    
    // Destroy the network buffers allocated by the adapter
    status = NetBuffersDestroy(&adapter->Buffers);
    if (status != OS_EOK) {
        return status;
    }
    
    free(adapter->Queues[NET_ADAPTER_TX].Leases);
    free(adapter->Queues[NET_ADAPTER_RX].Leases);
    free(adapter->Rx.Copy.Entries);
    free(adapter->Rx.Copy.Bytes);
    free(adapter);
    
    *adapterOut = NULL;
    return OS_EOK;
}

/**
 * @brief Update the link state of the network adapter. If link is successfully validated
 * the adapter's link state will be updated and the link callback will be invoked if registered.
 * @param adapter The network adapter structure.
 * @param link The new link state information.
 * @return OS_EOK if the link state was successfully updated, or an error code otherwise.
 */
static oserr_t
__UpdateLink(
    _In_ NetworkAdapter_t*                 adapter,
    _In_ const struct ctt_netadapter_link* link)
{
    // A link report is untrusted protocol input. Require a non-zero sequence so
    // every accepted report can be ordered, and constrain the enum fields to
    // values understood by this client before they reach state or callbacks.
    if (!link->sequence || link->status < CTT_NETADAPTER_LINK_STATUS_UNKNOWN ||
        link->status > CTT_NETADAPTER_LINK_STATUS_UP ||
        link->duplex < CTT_NETADAPTER_DUPLEX_UNKNOWN || link->duplex > CTT_NETADAPTER_DUPLEX_FULL) {
        return OS_EPROTOCOL;
    }

    // Reports can arrive after a newer report has already been processed. They
    // are harmless but must not roll the cached state back or notify clients
    // with an obsolete view of the link.
    if (link->sequence < adapter->Link.sequence) {
        return OS_EOK;
    }

    // A sequence identifies one link-state version. Reusing it for different
    // values indicates a protocol violation, while an identical repeat is a
    // valid duplicate and can be acknowledged without invoking the callback.
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

    // We can only start the adapter if it is currently stopped.
    if (adapter->State != NET_ADAPTER_STOPPED) {
        return OS_EBUSY;
    }

    adapter->Intent.StopRequested = false;
    adapter->Intent.StopReplied   = false;

    // Too many runs would overflow the counter.
    if (adapter->Window.Run == UINT64_MAX) {
        NetAdapterMarkFailed(adapter, OS_EOVERFLOW);
        return OS_EOVERFLOW;
    }
    adapter->Window.Run++;
    adapter->State = NET_ADAPTER_PREPARE;
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
        // The failure happened before a session was opened, so we know for certain that
        // no pools were registered with the driver. Start over by asking for its
        // capabilities; the new request gets a new serial, so late replies are ignored.
        adapter->State = NET_ADAPTER_INFO;
        adapter->Control.Pending = false;
        adapter->Intent.CloseRequested = adapter->Intent.StopRequested = false;
        return OS_EOK;
    }
    
    if (adapter->State != NET_ADAPTER_QUARANTINED || !adapter->Control.Pending) {
        return OS_EINVALPARAMS;
    }
    
    if (adapter->Control.Request.Operation == SERVICE_CTT_NETADAPTER_OPEN_ID) {
        adapter->State = NET_ADAPTER_OPEN;
    } else {
        adapter->State = NET_ADAPTER_CLOSING;
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
            adapter->State = NET_ADAPTER_QUARANTINED;
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
    // The selected port must exist
    if (adapter->Port >= adapter->Info.port_count) {
        return false;
    }

    // Version must be supported
    if (adapter->Info.min_version > 2 || adapter->Info.max_version < 2) {
        return false;
    }

    // MTU limits must describe a usable range.
    if (!adapter->Info.min_mtu || adapter->Info.min_mtu > adapter->Info.max_mtu) {
        return false;
    }

    // Batch and window limits must be nonzero; batch size must
    // also fit the client's fixed protocol buffers.
    if (!adapter->Info.max_batch_size || !adapter->Info.max_pending_batches ||
        adapter->Info.max_batch_size > NET_ADAPTER_BATCH_MAX) {
        return false;
    }

    return true;
}

/** 
 * @brief Check the capabilities reported by the driver and choose the local limits (MTU,
 * batch window and batch size) before the adapter is opened. Packet buffers are sized
 * for the MTU plus the Ethernet header. No optional protocol features are
 * requested.
 */
static oserr_t
__UpdateAdapterInfo(
    _In_ NetworkAdapter_t*        adapter,
    _In_ const NetAdapterReply_t* reply)
{
    // Store the updated adapter information from the reply.
    adapter->Info = reply->Info;
    
    // Treat the capability reply as untrusted.
    if (!__VerifyNetAdapterInfo(adapter)) {
        return OS_EPROTOCOL;
    }
    
    // MTU zero requests the documented default: 
    //     Current MTU capped at NET_ADAPTER_MTU_DEFAULT.
    // An explicit configured MTU is used as-is and is never silently changed.
    if (adapter->Config.Mtu) {
        adapter->Mtu = adapter->Config.Mtu;
    } else {
        adapter->Mtu = MIN(adapter->Info.current_mtu, NET_ADAPTER_MTU_DEFAULT);
        
        // The advertised default can be below the device's minimum; raise only
        // that default to the minimum. Explicit requests still need to be valid.
        if (adapter->Mtu < adapter->Info.min_mtu) {
            adapter->Mtu = adapter->Info.min_mtu;
        }
    }
    
    // Reject an MTU the device cannot support, or one whose Ethernet header
    // header would overflow when converted to a frame size.
    if (adapter->Mtu < adapter->Info.min_mtu || adapter->Mtu > adapter->Info.max_mtu ||
        adapter->Mtu > UINT32_MAX - NET_ADAPTER_ETHERNET_HEADER_SIZE) {
        return OS_ENOTSUPPORTED;
    }
    
    adapter->Window.Size = MIN(adapter->Config.BatchWindow, adapter->Info.max_pending_batches);
    adapter->Window.BatchSize = MIN(adapter->Info.max_batch_size, NET_ADAPTER_BATCH_MAX);
    adapter->State = NET_ADAPTER_OPEN;
    return OS_EOK;
}

/** 
 * @brief Free the buffer storage of a closed adapter, but only once callers have returned
 * every packet they still hold. This runs when the close completes, and again when a
 * caller releases its last packet.
 */
void
NetAdapterReclaimClosedBuffers(
    _In_ NetworkAdapter_t* adapter)
{
    // If the adapter is not closed, we cannot reclaim resources.
    if (adapter->State != NET_ADAPTER_CLOSED) {
        return;
    }

    // If buffer pool resources are still retained, we cannot reclaim them.
    if (adapter->Rx.Copy.Retained || adapter->Rx.PoolRetained) {
        return;
    }
    
    // TX and RX packets held by callers may still exist after the driver has closed
    // the session. The buffer manager keeps their storage valid until they are
    // returned, and the last release calls this function again to retry.
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
 * @brief Handle a successful CLOSE reply. Once the driver has confirmed the close it no
 * longer accesses the shared memory (DMA), so every lease managed by the worker
 * (queued or prepared) is released as cancelled. Packets that callers still hold,
 * received packets they kept and TX packets they are building, are not taken away;
 * they stay valid until the caller releases or cancels them.
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
        adapter->State = NET_ADAPTER_CLOSED;
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

    // Both pointers are required before the reply can be inspected or the
    // adapter state can be updated.
    if (!adapter || !reply) {
        return OS_EINVALPARAMS;
    }
    
    // Consume only the one outstanding response for this driver and serial;
    // old, duplicated, or misrouted replies must not advance this lifecycle.
    if (driver != adapter->Driver || !adapter->Control.Pending || serial != adapter->Control.Request.Serial) {
        return OS_ENOENT;
    }
    
    // A status outside the OS error range is malformed protocol data. For an
    // OPEN we then cannot tell whether the driver created a session, so keep the
    // request pending and quarantine instead of forgetting a session or memory
    // the driver may still be using.
    if (!OSERR_VALID(reply->Status)) {
        if (adapter->Control.Request.Operation == SERVICE_CTT_NETADAPTER_OPEN_ID) {
            adapter->LastError = OS_EPROTOCOL;
            adapter->State = NET_ADAPTER_QUARANTINED;
            return OS_EPROTOCOL;
        }
        NetAdapterMarkFailed(adapter, OS_EPROTOCOL);
        return OS_EPROTOCOL;
    }
    
    op = adapter->Control.Request.Operation;
    // A failed CLOSE is not proof that the remote side stopped using its
    // buffers. Keep the operation quarantined for explicit recovery; failures
    // of other operations use the normal failure path.
    if (reply->Status != OS_EOK) {
        if (op == SERVICE_CTT_NETADAPTER_CLOSE_ID) {
            adapter->LastError = reply->Status;
            adapter->State = NET_ADAPTER_QUARANTINED;
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
            // A successful OPEN must return a complete session identity so
            // later requests can be matched to the session and it can be closed
            // again. Without it, the session the driver created cannot be addressed.
            if (!reply->Session.id || !reply->Session.generation) {
                // The driver may still have created a session, so keep the request
                // pending and quarantine rather than forget about it.
                adapter->Control.Pending = true;
                adapter->State = NET_ADAPTER_QUARANTINED;
                adapter->LastError = OS_EPROTOCOL;
                return OS_EPROTOCOL;
            }
            adapter->Session = reply->Session;
            // OPEN requested no optional features, so accepting a nonzero
            // feature result would mean the driver enabled unrequested behavior.
            if (reply->Value) {
                status = OS_EPROTOCOL;
                break;
            }
            status = __UpdateLink(adapter, &reply->Link);
            // Validate the initial link before allocating pools; only enter
            // registration after both the link and local buffer setup succeed.
            if (status == OS_EOK) {
                status = NetAdapterSetupBuffers(adapter);
            }
            if (status == OS_EOK) {
                adapter->State = NET_ADAPTER_REGISTER_TX;
            }
            break;
        case SERVICE_CTT_NETADAPTER_REGISTER_POOL_ID:
            status = NetBuffersRegistered(adapter->Buffers,
                                          &adapter->Session,
                                          adapter->Control.Request.Pool.direction,
                                          reply->PoolId);
            // Register the opposite-direction pool next, then configure only
            // after both pool registrations have been accepted.
            if (status == OS_EOK) {
                adapter->State = adapter->Control.Request.Pool.direction == CTT_NETADAPTER_DIRECTION_TX
                                         ? NET_ADAPTER_REGISTER_RX
                                         : NET_ADAPTER_CONFIGURE;
            }
            break;
        case SERVICE_CTT_NETADAPTER_CONFIGURE_ID:
            adapter->State = NET_ADAPTER_PREPARE;
            break;
        case SERVICE_CTT_NETADAPTER_PREPARE_RUN_ID:
            adapter->State = NET_ADAPTER_PRIME;
            break;
        case SERVICE_CTT_NETADAPTER_START_RUN_ID:
            adapter->State = NET_ADAPTER_RUNNING;
            adapter->Drain.NextPoll = __NetAdapterDeadline(now, adapter->Config.PollMilliseconds);
            break;
        case SERVICE_CTT_NETADAPTER_STOP_RUN_ID: {
            NetBufferStats_t stats;
            NetBuffersGetStats(adapter->Buffers, &stats);
            // The reply is the sequence number of the last completion the driver
            // will report for this run. It cannot be lower than completions we have
            // already processed, or than the highest completion the driver reported.
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
    // Any malformed successful payload or local setup failure follows the
    // common failure path, which initiates close when a session exists.
    if (status != OS_EOK) {
        NetAdapterMarkFailed(adapter, status);
    }
    return status;
}
