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
 * Packet lease lookup and RX/TX batch building.
 *
 * A batch holds at most the number of packets agreed with the driver. This file
 * builds batch requests and manages pool budgets, while requests.c sends them
 * over Gracht.
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
    // Require a nonzero sequence, valid direction, and an exact match on the
    // active session ID and generation.
    if (!lease->Session.id || !lease->Sequence ||
        (lease->Direction != CTT_NETADAPTER_DIRECTION_TX &&
         lease->Direction != CTT_NETADAPTER_DIRECTION_RX) ||
        lease->Session.id != adapter->Session.id ||
        lease->Session.generation != adapter->Session.generation) {
        return false;
    }
    return true;
}

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

    // Check session identity first so a lease from an old session cannot match
    // an entry that happens to share the same slot and sequence number.
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

    return entry->State == state ? entry : NULL;
}

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
        // Consumer holds this packet and must return it via NetAdapterRxRelease.
        return OS_EEXISTS;
    }

    if (!__AdapterLeaseIsManagedByWorker(entry)) {
        // Callers control TX_BUILDING and FREE slots, so the worker leaves them alone.
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
        // If the consumer keeps the pool slot itself, leave it held and return now.
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

    // Stop offering newly completed RX packets once a session close begins.
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
    
    // Duplicate completions return OS_EEXISTS. Wait silently if !ready because
    // the admission reply has not arrived yet.
    if (oserr == OS_EEXISTS) {
        return OS_EOK;
    } else if (oserr != OS_EOK) {
        return oserr;
    } else if (!ready) {
        return OS_EOK;
    }
    
    return NetAdapterReleaseCompletedLease(adapter, &lease);
}

// Allocate the shared packet pools, worker lease arrays, and backup RX copy buffers.
static oserr_t
__CreateBuffers(
    _In_ NetworkAdapter_t* adapter,
    _In_ uint64_t          metadata)
{
    oserr_t           oserr;
    NetBufferConfig_t config = {
        adapter->Queues[NET_ADAPTER_TX].Slots,
        adapter->Queues[NET_ADAPTER_RX].Slots,
        adapter->Mtu + NET_ADAPTER_ETHERNET_HEADER_SIZE,
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
        adapter->Rx.Copy.Bytes = calloc(
            adapter->Rx.Copy.Count,
            adapter->Mtu + NET_ADAPTER_ETHERNET_HEADER_SIZE
        );
        if (!adapter->Rx.Copy.Entries || !adapter->Rx.Copy.Bytes) {
            return OS_EOOM;
        }
    }
    return OS_EOK;
}

oserr_t
NetAdapterSetupBuffers(
    _In_ NetworkAdapter_t* adapter)
{
    struct AdapterQueue* tx = &adapter->Queues[NET_ADAPTER_TX];
    struct AdapterQueue* rx = &adapter->Queues[NET_ADAPTER_RX];
    uint64_t             metadata;
    uint32_t             reserve;
    uint64_t             copyUnit;

    // Cap slot counts to what the driver supports, and leave at least one slot
    // in max_unacked_completions for TX so receive traffic never locks out send.
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
    
    // Subtract adapter struct, lease tables, and backup RX copy buffers from
    // MemoryBudget first so the remaining budget goes to the shared pools.
    metadata = sizeof(*adapter) + 
        ((uint64_t)tx->Slots + rx->Slots) *
        sizeof(struct AdapterLease);
    
    // Always reserve enough RX slots for the driver's minimum so consumers
    // holding kept packets cannot starve the receive ring.
    reserve = adapter->Info.min_rx_slots ? adapter->Info.min_rx_slots : 1;
    adapter->Rx.RetentionLimit = MIN(adapter->Config.RxRetainedSlots, rx->Slots - reserve);
    adapter->Rx.Copy.Count = MIN(adapter->Config.RxCopySlots, rx->Slots);
   
    copyUnit = sizeof(struct AdapterRxCopy) + (uint64_t)adapter->Mtu + NET_ADAPTER_ETHERNET_HEADER_SIZE;
    if (adapter->Rx.Copy.Count && copyUnit > (UINT64_MAX - metadata) / adapter->Rx.Copy.Count) {
        return OS_EOVERFLOW;
    }
    
    metadata += copyUnit * adapter->Rx.Copy.Count;
    if (metadata >= adapter->Config.MemoryBudget || metadata > SIZE_MAX) {
        return OS_EOVERFLOW;
    }
    return __CreateBuffers(adapter, metadata);
}

void
NetAdapterCancelQueued(
    _In_ NetworkAdapter_t* adapter)
{
    struct AdapterQueue* tx = &adapter->Queues[NET_ADAPTER_TX];

    if (!tx->Leases) {
        return;
    }
    
    // Only cancel frames still waiting in the local queue. Frames already packed
    // into batches (PREPARED) stay untouched until completed or session close.
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
        // Stop acquiring RX slots once the posted count hits Rx.Target.
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
            .Length = adapter->Mtu + NET_ADAPTER_ETHERNET_HEADER_SIZE
        };
    } else {
        // Pick the queued TX frame with the lowest Order so frames leave FIFO
        // even when pool slots are reused out of order.
        for (uint32_t i = 0; i < adapter->Queues[NET_ADAPTER_TX].Slots; ++i) {
            struct AdapterLease* candidate = &adapter->Queues[NET_ADAPTER_TX].Leases[i];
            if (candidate->State == ADAPTER_LEASE_QUEUED &&
                (!entry || candidate->Order < entry->Order)) {
                entry = candidate;
            }
        }
        if (!entry) {
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
    // Only newly acquired RX slots are released on prepare failure. TX frames
    // stay queued for the next batch attempt.
    if (rx) {
        (void)NetBuffersRelease(adapter->Buffers, &entry->Lease);
        memset(entry, 0, sizeof(*entry));
    }
}

oserr_t
NetAdapterBuildBatch(
    _In_ NetworkAdapter_t*    adapter,
    _In_ bool                 rx,
    _In_ NetAdapterRequest_t* request)
{
    NetBufferStats_t stats;
    
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

        // Mark the slot PENDING in the buffer manager before returning so it
        // stays protected even if the IPC send fails or times out.
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
