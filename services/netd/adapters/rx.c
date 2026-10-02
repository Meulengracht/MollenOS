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
 * Retain an RX packet without retaining the driver's pool slot.
 *
 * This path is used only after the pool-retention budget is full: keeping the
 * received pool lease would prevent that slot from being recycled for further
 * traffic. Instead, copy the frame into a bounded, adapter-owned slot so the
 * consumer can keep the data while the original RX slot is returned promptly.
 * The storage is preallocated because this runs on the receive path, where a
 * dynamic allocation could fail or add unpredictable latency. These copy slots
 * have their own identities and do not consume driver completion-journal
 * credits.
 *
 * A slot marked Retained is owned by a consumer and must not be overwritten.
 * Its monotonically increasing Sequence distinguishes a later use of the same
 * slot from an old release token; slots at UINT64_MAX are retired rather than
 * allowing the identity to wrap and alias a stale token. Returning true means
 * the callback accepted ownership. Returning false leaves ownership here, so
 * the caller can deliver the original borrowed view through the legacy callback.
 */

#include "session.h"
#include "private.h"

static void
__IncreaseWithoutRollover(
    _InOut_ uint64_t* counter)
{
    if (*counter != UINT64_MAX) {
        ++*counter;
    }
}

/** 
 * The pool retention budget is exhausted. Offer a copy from preallocated local
 * storage so the original RX slot can immediately return to the NIC. Copy slots
 * have separate identities and do not consume driver completion-journal credits.
 */
static bool
__AllocateCopyBuffer(
    _In_ NetworkAdapter_t*      adapter,
    _In_ const NetBufferView_t* view)
{
    for (uint32_t i = 0; i < adapter->RxCopyCount; ++i) {
        struct AdapterRxCopy* copy = &adapter->RxCopies[i];
        unsigned char*        data;
        
        if (copy->Retained || copy->Sequence == UINT64_MAX) {
            continue;
        }
        
        // The slot is available and its identity can still advance. Populate it
        // before publishing a token to the callback.
        data = adapter->RxCopyBytes + (size_t)i * (adapter->Mtu + 14);
        memcpy(data, view->Data, view->Length);
        __IncreaseWithoutRollover(&adapter->RxFallbackCopies);
        copy->Retained = true;
        ++copy->Sequence;
        ++adapter->RxCopyRetained;
        
        NetAdapterRxPacket_t packet = {
            .Data = data,
            .Length = view->Length,
            .Private = {
                .Owner = adapter,
                .Backing = NET_ADAPTER_PACKET_COPY,
                .Storage.Copy = {
                    .Slot = i,
                    .Sequence = copy->Sequence
                }
            }
        };
        
        // Acceptance transfers responsibility for eventually releasing this
        // exact slot/sequence pair to the consumer.
        if (adapter->Callbacks.ReceivePacket(adapter->Callbacks.Context, &packet)) {
            return true;
        }
        
        // Declining is not an ownership transfer. Do not offer the same frame a
        // second time; the borrowed callback below is the remaining fallback.
        copy->Retained = false;
        --adapter->RxCopyRetained;
        return false;
    }
    return false;
}

bool
NetAdapterDeliverReceive(
    _In_ NetworkAdapter_t*      adapter,
    _In_ struct AdapterLease*   entry,
    _In_ const NetBufferView_t* view)
{
    // Prefer handing off the original pool-backed data: it avoids a copy, but
    // retaining too many pool leases would starve the driver's RX ring. Bound
    // that zero-copy path and use independent copy storage once it is full.
    if (adapter->Callbacks.ReceivePacket) {
        if (adapter->RxPoolRetained < adapter->RxRetentionLimit) {
            NetAdapterRxPacket_t packet = {
                .Data = view->Data,
                .Length = view->Length,
                .Private = {
                    .Owner = adapter,
                    .Backing = NET_ADAPTER_PACKET_POOL,
                    .Storage.Pool = entry->Lease
                }
            };
            
            // Publish the retained state before calling out so the packet has a
            // valid owner as soon as the callback can accept it.
            entry->State = ADAPTER_LEASE_RX_RETAINED;
            ++adapter->RxPoolRetained;
            
            if (adapter->Callbacks.ReceivePacket(adapter->Callbacks.Context, &packet)) {
                // The consumer accepted this lease and must release it later.
                return true;
            }
            
            // Decline means no ownership transfer; restore the lease and budget
            // so this RX slot can follow the normal recycle path.
            entry->State = ADAPTER_LEASE_PREPARED;
            --adapter->RxPoolRetained;
        } else if (__AllocateCopyBuffer(adapter, view)) {
            // The consumer owns the copy, not this lease, so report success to
            // the caller while allowing the original pool slot to be recycled.
            return false;
        }
    }
    
    // No retained-packet handoff succeeded (or none was requested). The legacy
    // callback borrows the view only for this call, so the RX slot stays local.
    if (adapter->Callbacks.Receive) {
        adapter->Callbacks.Receive(adapter->Callbacks.Context, view->Data, view->Length);
    } else {
        // There is no consumer to receive the frame; account for its loss.
        __IncreaseWithoutRollover(&adapter->RxDropped);
    }
    return false;
}

/**
 * Release a retained copy by making its adapter-owned slot reusable. Unlike a
 * pool-backed packet, this does not return a driver lease: the frame already
 * lives in the adapter's separate copy storage, and only the consumer's claim
 * on that storage is ending.
 *
 * Validate the owner, backing tag, slot, and generation before changing state.
 * The sequence check rejects duplicate or stale tokens if the slot has since
 * been reused for another packet.
 */
static oserr_t
__HandlePacketCopyRelease(
    _In_ NetworkAdapter_t*     adapter,
    _In_ NetAdapterRxPacket_t* packet)
{
    struct AdapterRxCopy* copy;

    if (!NetAdapterPacketHasBacking(adapter, &packet->Private, NET_ADAPTER_PACKET_COPY)) {
        return OS_ENOENT;
    }

    if (!adapter->RxCopies || packet->Private.Storage.Copy.Slot >= adapter->RxCopyCount) {
        return OS_ENOENT;
    }
    
    copy = &adapter->RxCopies[packet->Private.Storage.Copy.Slot];
    if (!copy->Retained || copy->Sequence != packet->Private.Storage.Copy.Sequence) {
        return OS_ENOENT;
    }

    // The consumer is done with these bytes, so this slot can be overwritten by
    // a later fallback packet and no longer counts against the copy budget.
    copy->Retained = false;
    --adapter->RxCopyRetained;
    return OS_EOK;
}

/**
 * Release a retained pool-backed packet by returning its RX lease to the buffer
 * manager. The adapter's lease entry must still identify this packet as
 * retained; otherwise the token is foreign, stale, already released, or no
 * longer in the state this operation is allowed to release.
 */
static oserr_t
__HandlePacketPoolRelease(
    _In_ NetworkAdapter_t*     adapter,
    _In_ NetAdapterRxPacket_t* packet)
{
    struct AdapterLease* entry;
    oserr_t              status;
    
    entry = NetAdapterFindPacketLease(
        adapter,
        &packet->Private,
        CTT_NETADAPTER_DIRECTION_RX,
        ADAPTER_LEASE_RX_RETAINED
    );
    if (!entry) {
        return OS_ENOENT;
    }
    
    // Return the underlying lease first. If that fails, preserve the retained
    // entry and count so the adapter does not claim a release that never happened.
    status = NetBuffersRelease(adapter->Buffers, &entry->Lease);
    if (status != OS_EOK) {
        return status;
    }
    
    // The lease is now returned, so discard its lookup record and free one unit
    // of the bounded pool-retention budget.
    memset(entry, 0, sizeof(*entry));
    --adapter->RxPoolRetained;
    return OS_EOK;
}

oserr_t
NetAdapterRxRelease(
    _In_ NetworkAdapter_t*     adapter,
    _In_ NetAdapterRxPacket_t* packet)
{
    oserr_t oserr;

    if (!adapter || !packet) {
        return OS_EINVALPARAMS;
    }
    
    switch (packet->Private.Backing) {
        case NET_ADAPTER_PACKET_COPY: {
            oserr = __HandlePacketCopyRelease(adapter, packet);
            if (oserr != OS_EOK) {
                return oserr;
            }
            break;
        }
        case NET_ADAPTER_PACKET_POOL: {
            oserr = __HandlePacketPoolRelease(adapter, packet);
            if (oserr != OS_EOK) {
                return oserr;
            }
            break;
        }
        default:
            return OS_ENOENT; // Cleared or unknown tags never select a union member.
    }
    
    // Clear the packet and release any associated resources.
    memset(packet, 0, sizeof(*packet));
    NetAdapterReclaimClosedBuffers(adapter);
    return OS_EOK;
}
