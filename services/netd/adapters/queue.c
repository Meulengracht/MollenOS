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
 * Tracking of packet leases and construction of RX/TX batch requests. A batch
 * never holds more packets than the batch size agreed with the driver. This file
 * only prepares requests; sending them over IPC is done in requests.c.
 * 
 */

#define __need_minmax
#include <os/osdefs.h>
#include "session.h"
#include "private.h"

static bool
__IsLeaseValid(
    _In_ NetworkAdapter_t*       adapter,
    _In_ const NetBufferLease_t* lease)
{
    // The lease must be:
    // - Associated with a valid session.
    // - Has a nonzero sequence number.
    // - Has a valid direction (TX or RX).
    // - Matches the current adapter session ID and generation.
    if (!lease->Session.id || !lease->Sequence ||
        (lease->Direction != CTT_NETADAPTER_DIRECTION_TX &&
         lease->Direction != CTT_NETADAPTER_DIRECTION_RX) ||
        lease->Session.id != adapter->Session.id ||
        lease->Session.generation != adapter->Session.generation) {
        return false;
    }
    return true;
}

/**
 * @brief Look up the adapter's lease entry for a pool lease. Before the lookup we check
 * that the lease belongs to the current session (both the session id and its
 * generation) and that it has a valid direction, since the direction decides
 * whether the TX or the RX table is used. Without the session check, a lease left
 * over from an older session could match an entry just because its slot number
 * and sequence number happen to be the same.
 */
struct AdapterLease*
NetAdapterFindLease(
    _In_ NetworkAdapter_t*       adapter,
    _In_ const NetBufferLease_t* lease)
{
    struct AdapterLease* entry;
    int                  index;

    if (!adapter || !lease || !adapter->Buffers) {
        return NULL;
    }

    // Validate the lease and session information before proceeding.
    if (!__IsLeaseValid(adapter, lease)) {
        return NULL;
    }

    index = lease->Direction == CTT_NETADAPTER_DIRECTION_TX ? NET_ADAPTER_TX : NET_ADAPTER_RX;
    if (!adapter->Queues[index].Leases || lease->Slot >= adapter->Queues[index].Slots) {
        return NULL;
    }
    
    entry = &adapter->Queues[index].Leases[lease->Slot];
    if (entry->State == ADAPTER_LEASE_FREE) {
        return NULL;
    }

    // Ensure the lease sequence matches before returning the entry.
    return entry->Lease.Sequence == lease->Sequence ? entry : NULL;
}

struct AdapterLease*
NetAdapterFindPacketLease(
    _In_ NetworkAdapter_t*                 adapter,
    _In_ const NetAdapterPacketIdentity_t* identity,
    _In_ enum ctt_netadapter_direction     direction,
    _In_ enum AdapterLeaseState            state)
{
    struct AdapterLease* entry;

    if (!___PacketHasBacking(adapter, identity, NET_ADAPTER_PACKET_POOL)) {
        return NULL;
    }

    if (identity->Storage.Pool.Direction != direction) {
        return NULL;
    }

    entry = NetAdapterFindLease(adapter, &identity->Storage.Pool);
    if (entry == NULL) {
        return NULL;
    }

    // Ensure the lease is in the requested state before returning it.
    return entry->State == state ? entry : NULL;
}

/** 
 * @brief Report the final result of a lease and give its slot back to the pool. For TX,
 * the caller is told the result through the Transmitted callback. For RX, the
 * packet is offered to the consumer: if the consumer accepts the packet directly
 * from the pool, the slot stays in use until the consumer releases it. If the
 * frame was only lent to the consumer for the duration of the callback, or was
 * copied, the slot is given back to the pool right away.
 */
oserr_t
NetAdapterReleaseLease(
    _In_ NetworkAdapter_t*    adapter,
    _In_ struct AdapterLease* entry,
    _In_ oserr_t              status,
    _In_ bool                 receive)
{
    NetBufferView_t view;
    oserr_t         result;

    if (entry->State == ADAPTER_LEASE_RX_RETAINED) {
        // Never redeliver or revoke a lease that is managed by the consumer.
        return OS_EEXISTS;
    }

    if (!__AdapterLeaseIsManagedByWorker(entry)) {
        // TX builders and inactive entries are not managed by the worker and have no result.
        return OS_EBUSY;
    }
    
    result = NetBuffersView(adapter->Buffers, &entry->Lease, &view);
    if (result != OS_EOK) {
        return result;
    }
    
    if (entry->Lease.Direction == CTT_NETADAPTER_DIRECTION_TX) {
        if (adapter->Callbacks.Transmitted) {
            adapter->Callbacks.Transmitted(adapter->Callbacks.Context, entry->Cookie, status);
        }
    } else if (receive && status == OS_EOK) {
        if (NetAdapterDeliverReceive(adapter, entry, &view)) {
            return OS_EOK;
        }
    }
    
    result = NetBuffersRelease(adapter->Buffers, &entry->Lease);
    if (result == OS_EOK) {
        memset(entry, 0, sizeof(*entry));
    }
    return result;
}

oserr_t
NetAdapterReleaseCompletedLease(
    _In_ NetworkAdapter_t*       adapter,
    _In_ const NetBufferLease_t* lease)
{
    struct AdapterLease* entry = NetAdapterFindLease(adapter, lease);
    NetBufferView_t      view;
    oserr_t              oserr;
    
    if (!entry) {
        return OS_EPROTOCOL;
    }

    oserr = NetBuffersView(adapter->Buffers, lease, &view);
    if (oserr != OS_EOK || !view.Completed) {
        return OS_EPROTOCOL;
    }

    return NetAdapterReleaseLease(
        adapter,
        entry,
        view.Detail,
        !adapter->Intent.CloseRequested
    );
}

oserr_t
NetAdapterCompletePacket(
    _In_ NetworkAdapter_t*                       adapter,
    _In_ const struct ctt_netadapter_completion* completion)
{
    NetBufferLease_t lease;
    bool             ready;
    oserr_t          oserr;
            
    oserr = NetBuffersComplete(
        adapter->Buffers,
        &adapter->Session,
        completion,
        &lease,
        &ready
    );
    
    if (oserr == OS_EEXISTS) {
        return OS_EOK;
    } else if (oserr != OS_EOK) {
        return oserr;
    } else if (!ready) {
        return OS_EOK;
    }
    
    return NetAdapterReleaseCompletedLease(adapter, &lease);
}

static oserr_t
__CreateBuffers(
    _In_ NetworkAdapter_t* adapter,
    _In_ uint64_t          metadata)
{
    oserr_t           oserr;
    NetBufferConfig_t config = {
        adapter->Queues[NET_ADAPTER_TX].Slots,
        adapter->Queues[NET_ADAPTER_RX].Slots,
        adapter->Mtu + 14,
        adapter->Config.MemoryBudget - metadata
    };

    oserr = NetBuffersCreate(
        &adapter->Session,
        &adapter->Info,
        &config,
        &adapter->Buffers
    );
    if (oserr != OS_EOK) {
        return oserr;
    }
    
    for (int i = 0; i < 2; ++i) {
        adapter->Queues[i].Leases = calloc(adapter->Queues[i].Slots, sizeof(struct AdapterLease));
        if (!adapter->Queues[i].Leases) {
            return OS_EOOM;
        }
    }
    
    if (adapter->Rx.Copy.Count) {
        adapter->Rx.Copy.Entries = calloc(adapter->Rx.Copy.Count, sizeof(*adapter->Rx.Copy.Entries));
        adapter->Rx.Copy.Bytes = calloc(adapter->Rx.Copy.Count, adapter->Mtu + 14);
        if (!adapter->Rx.Copy.Entries || !adapter->Rx.Copy.Bytes) {
            return OS_EOOM;
        }
    }
    return OS_EOK;
}

/**
 * @brief Decide how many TX and RX slots the packet pools get, and check that everything
 * fits within the memory budget from the adapter configuration.
 *
 * The slot counts start from the configuration and are reduced to whatever the
 * driver reported it can handle. The memory budget has to pay for more than the
 * shared packet storage: the adapter structure itself, the per-slot lease tables
 * and the RX copy slots are all counted against it as well. Those are added up
 * first, and whatever is left of the budget is given to the packet pools.
 *
 * The driver can only keep a limited number of completions that we have not yet
 * acknowledged. Receive buffers waiting in the driver could use all of them, so we
 * post at most one less receive buffer than that limit. This leaves room for at
 * least one TX completion, so transmits keep working even when receive is busy.
 */
oserr_t
NetAdapterSetupBuffers(
    _In_ NetworkAdapter_t* adapter)
{
    struct AdapterQueue* tx = &adapter->Queues[NET_ADAPTER_TX];
    struct AdapterQueue* rx = &adapter->Queues[NET_ADAPTER_RX];
    uint64_t             metadata;
    uint32_t             reserve;
    uint64_t             copyUnit;

    tx->Slots = MIN(adapter->Config.TxSlots, adapter->Info.max_slots_per_pool);
    rx->Slots = MIN(adapter->Config.RxSlots, adapter->Info.max_slots_per_pool);
    adapter->Rx.Target = MIN(rx->Slots, adapter->Info.max_outstanding_rx);
    if (!adapter->Info.max_unacked_completions) {
        return OS_EPROTOCOL;
    }
    
    adapter->Rx.Target = MIN(adapter->Rx.Target, adapter->Info.max_unacked_completions - 1);
    if (!adapter->Rx.Target || adapter->Rx.Target < adapter->Info.min_rx_slots) {
        return OS_ENOTSUPPORTED;
    }
    
    // The lease tables are paid for from the same memory budget as the packet pools.
    // The slot counts were capped above, so this 64-bit multiplication cannot
    // overflow on either 32-bit or 64-bit targets.
    metadata = sizeof(*adapter) + 
        ((uint64_t)tx->Slots + rx->Slots) *
        sizeof(struct AdapterLease);
    
    // Consumers may hold on to RX pool slots instead of returning them immediately.
    // Always keep some slots back (the driver's minimum, or at least one) so the
    // driver still has receive buffers even when consumers hold as many as allowed.
    reserve = adapter->Info.min_rx_slots ? adapter->Info.min_rx_slots : 1;
    adapter->Rx.RetentionLimit = MIN(adapter->Config.RxRetainedSlots, rx->Slots - reserve);
    adapter->Rx.Copy.Count = MIN(adapter->Config.RxCopySlots, rx->Slots);
   
    copyUnit = sizeof(struct AdapterRxCopy) + (uint64_t)adapter->Mtu + 14;
    if (adapter->Rx.Copy.Count && copyUnit > (UINT64_MAX - metadata) / adapter->Rx.Copy.Count) {
        return OS_EOVERFLOW;
    }
    
    metadata += copyUnit * adapter->Rx.Copy.Count;
    if (metadata >= adapter->Config.MemoryBudget || metadata > SIZE_MAX) {
        return OS_EOVERFLOW;
    }
    return __CreateBuffers(adapter, metadata);
}

/**
 * @brief Cancel the TX frames that are still waiting in the local queue. Frames that have
 * already been prepared for the driver are left alone: they may already have been
 * sent, so we cannot know whether the driver is using them until it reports a
 * completion or the session is closed.
 */
void
NetAdapterCancelQueued(
    _In_ NetworkAdapter_t* adapter)
{
    struct AdapterQueue* tx = &adapter->Queues[NET_ADAPTER_TX];

    if (!tx->Leases) {
        return;
    }
    
    for (uint32_t i = 0; i < tx->Slots; ++i) {
        struct AdapterLease* entry = &tx->Leases[i];
        if (entry->State == ADAPTER_LEASE_QUEUED) {
            (void)NetAdapterReleaseLease(adapter, entry, OS_ECANCELLED, false);
        }
    }
}

static oserr_t
__AcquireBatchLease(
    _In_ NetworkAdapter_t*      adapter,
    _In_ bool                   rx,
    _In_ NetAdapterRequest_t*   request,
    _In_ NetBufferStats_t*      stats,
    _Out_ struct AdapterLease** outEntry)
{
    struct AdapterLease* entry = NULL;
    oserr_t              oserr = OS_EOK;
    NetBufferLease_t     lease;
    
    if (rx) {
        if (stats->RxOutstanding + request->Count >= adapter->Rx.Target) {
            return OS_EBUSY;
        }
        
        oserr = NetBuffersAcquire(adapter->Buffers, CTT_NETADAPTER_DIRECTION_RX, &lease);
        if (oserr != OS_EOK) {
            return oserr;
        }
        entry = &adapter->Queues[NET_ADAPTER_RX].Leases[lease.Slot];
        
        *entry = (struct AdapterLease){
            .Lease = lease,
            .State = ADAPTER_LEASE_QUEUED,
            .Length = adapter->Mtu + 14
        };
    } else {
        // Once slots have been reused, a lower slot number does not mean the frame
        // was queued earlier, so search for the lowest queue order instead.
        for (uint32_t i = 0; i < adapter->Queues[NET_ADAPTER_TX].Slots; ++i) {
            struct AdapterLease* candidate = &adapter->Queues[NET_ADAPTER_TX].Leases[i];
            if (candidate->State == ADAPTER_LEASE_QUEUED &&
                (!entry || candidate->Order < entry->Order)) {
                entry = candidate;
            }
        }
        if (!entry) {
            // No queued TX frames available
            return OS_EBUSY;
        }
    }

    *outEntry = entry;
    return oserr;
}

static void
__ReleaseBatchLease(
    _In_ NetworkAdapter_t*    adapter,
    _In_ bool                 rx,
    _In_ struct AdapterLease* entry)
{
    if (rx) {
        (void)NetBuffersRelease(adapter->Buffers, &entry->Lease);
        memset(entry, 0, sizeof(*entry));
    }
}

/**
 * @brief Fill a batch request with packets for the driver. For RX, new receive slots are
 * taken from the pool until the number of receive buffers given to the driver
 * reaches the RX target. For TX, queued frames are taken oldest first. Each packet
 * is marked PREPARED before the request is sent, because from the moment we start
 * sending, the driver may be using the buffer even if we never receive a reply.
 */
oserr_t
NetAdapterBuildBatch(
    _In_ NetworkAdapter_t*    adapter,
    _In_ bool                 rx,
    _In_ NetAdapterRequest_t* request)
{
    NetBufferStats_t stats;
    
    // Update the adapter buffer stats
    NetBuffersGetStats(adapter->Buffers, &stats);
    if (adapter->Window.NextBatch == UINT64_MAX) {
        return OS_EOVERFLOW;
    }
    
    request->Count = 0;
    while (request->Count < adapter->Window.BatchSize) {
        struct AdapterLease* entry = NULL;
        oserr_t              oserr = OS_EOK;

        oserr = __AcquireBatchLease(adapter, rx, request, &stats, &entry);
        if (oserr == OS_EBUSY) {
            break;
        } else if (oserr != OS_EOK) {
            return oserr;
        }

        oserr = NetBuffersPrepare(
            adapter->Buffers,
            &entry->Lease,
            entry->Length,
            &request->Packets[request->Count]
        );
        if (oserr != OS_EOK) {
            __ReleaseBatchLease(adapter, rx, entry);
            if (oserr == OS_EBUSY) {
                break;
            }
            return oserr;
        }
        
        entry->State = ADAPTER_LEASE_PREPARED;
        request->Count++;
    }
    
    if (!request->Count) {
        return OS_ENOENT;
    }
    
    request->Operation = rx ? SERVICE_CTT_NETADAPTER_POST_RX_BATCH_ID
                            : SERVICE_CTT_NETADAPTER_SUBMIT_TX_BATCH_ID;
    request->Value = adapter->Window.NextBatch;
    return OS_EOK;
}

/**
 * @brief Find the stored copy of a batch that was sent earlier, so it can be resent or
 * matched with a reply from the driver. A batch keeps its window slot even after
 * the driver has accepted it; the slot is only freed once the driver confirms it
 * is completely finished with the batch (it has retired it).
 */
struct AdapterBatch*
NetAdapterFindBatch(
    _In_ NetworkAdapter_t* adapter,
    _In_ uint64_t          id)
{
    for (uint32_t i = 0; i < adapter->Window.Size; ++i) {
        if (adapter->Window.Batches[i].Used && adapter->Window.Batches[i].Request.Value == id) {
            return &adapter->Window.Batches[i];
        }
    }
    return NULL;
}
