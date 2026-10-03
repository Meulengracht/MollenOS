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
 * Let a consumer keep an RX packet without keeping the driver's pool slot busy.
 *
 * This path is only used once consumers already hold as many pool slots as they
 * are allowed to. Holding on to yet another pool slot would leave the driver with
 * fewer receive buffers. Instead, the frame is copied into one of a fixed number
 * of copy slots managed by the adapter, so the consumer can keep the data while
 * the original RX slot goes straight back to the driver.
 * The copy slots are allocated up front because this runs on the receive path,
 * where allocating memory could fail or add unpredictable delays. Copy slots have
 * their own identities and do not count against the number of completions the
 * driver can have waiting for acknowledgement.
 *
 * A copy slot marked Retained is managed by a consumer and must not be overwritten.
 * Each slot also has a Sequence number that goes up by one every time the slot is
 * reused. A packet records the number it was given, so a late or repeated release of
 * an old packet can be told apart from the packet currently in the slot. Once a slot's
 * number reaches UINT64_MAX the slot is never used again, because wrapping back to
 * zero could make an old packet look current. When the ReceivePacket callback returns
 * true, the consumer now manages the copy. When it returns false, nothing changes, and
 * the caller falls back to lending the original frame through the Receive callback.
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
 * Called when consumers already hold as many pool slots as they are allowed to. Copy
 * the frame into a free copy slot and offer that copy to the consumer instead, so the
 * original receive buffer can go back to the driver right away. Copy slots are
 * separate from the pool and do not use up any of the completions the driver may
 * have waiting for acknowledgement.
 * Returns true only if the consumer accepted the copy.
 */
static bool
__AllocateCopyBuffer(
    _In_ NetworkAdapter_t*      adapter,
    _In_ const NetBufferView_t* view)
{
    for (uint32_t i = 0; i < adapter->Rx.Copy.Count; ++i) {
        struct AdapterRxCopy* copy = &adapter->Rx.Copy.Entries[i];
        unsigned char*        data;
        
        if (copy->Retained || copy->Sequence == UINT64_MAX) {
            continue;
        }
        
        // The slot is free and its Sequence can still be increased. Fill it and
        // mark it in use before the consumer gets to see the packet.
        data = adapter->Rx.Copy.Bytes + (size_t)i * (adapter->Mtu + 14);
        memcpy(data, view->Data, view->Length);
        __IncreaseWithoutRollover(&adapter->Rx.FallbackCopies);
        copy->Retained = true;
        ++copy->Sequence;
        ++adapter->Rx.Copy.Retained;
        
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
        
        // If the consumer accepts, it now manages this copy and must later release
        // it; the slot number and Sequence in the packet identify exactly this use.
        if (adapter->Callbacks.ReceivePacket(adapter->Callbacks.Context, &packet)) {
            return true;
        }
        
        // The consumer declined, so the slot is free again. Do not try another copy
        // slot for the same frame; the caller falls back to the Receive callback.
        copy->Retained = false;
        --adapter->Rx.Copy.Retained;
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
    // Prefer handing the consumer the original pool buffer, since that avoids a
    // copy. But every pool slot a consumer keeps is a receive buffer the driver
    // cannot use, so only a limited number may be kept this way; after that,
    // packets are copied into the separate copy slots instead.
    if (adapter->Callbacks.ReceivePacket) {
        if (adapter->Rx.PoolRetained < adapter->Rx.RetentionLimit) {
            NetAdapterRxPacket_t packet = {
                .Data = view->Data,
                .Length = view->Length,
                .Private = {
                    .Owner = adapter,
                    .Backing = NET_ADAPTER_PACKET_POOL,
                    .Storage.Pool = entry->Lease
                }
            };
            
            // Mark the lease as kept by a consumer before calling out, so its state
            // is already correct the moment the callback accepts it.
            entry->State = ADAPTER_LEASE_RX_RETAINED;
            ++adapter->Rx.PoolRetained;
            
            if (adapter->Callbacks.ReceivePacket(adapter->Callbacks.Context, &packet)) {
                // The consumer accepted this lease and must release it later.
                return true;
            }
            
            // The consumer declined, so undo the marking and the count above. The
            // slot then goes back to the driver as usual.
            entry->State = ADAPTER_LEASE_PREPARED;
            --adapter->Rx.PoolRetained;
        } else if (__AllocateCopyBuffer(adapter, view)) {
            // The consumer kept the copy, not this pool slot. Return false so the
            // caller gives the pool slot back to the driver.
            return false;
        }
    }
    
    // The consumer did not keep the frame, or has no ReceivePacket callback. Lend
    // the frame through Receive, which may only read it during the call, so the
    // pool slot can be reused afterwards.
    if (adapter->Callbacks.Receive) {
        adapter->Callbacks.Receive(adapter->Callbacks.Context, view->Data, view->Length);
    } else {
        // There is no consumer to receive the frame; account for its loss.
        __IncreaseWithoutRollover(&adapter->Rx.Dropped);
    }
    return false;
}

/**
 * Release a packet that the consumer kept as a copy, so its copy slot can be used
 * again. Nothing goes back to the driver here: the original receive buffer was
 * already returned when the copy was made. This only ends the consumer's use of the
 * copy slot.
 *
 * Before changing anything, check that the packet belongs to this adapter, is a copy,
 * names a valid slot, and carries the slot's current Sequence. The Sequence check
 * rejects releasing the same packet twice, and rejects an old packet whose slot has
 * since been reused for another frame.
 */
static oserr_t
__HandlePacketCopyRelease(
    _In_ NetworkAdapter_t*     adapter,
    _In_ NetAdapterRxPacket_t* packet)
{
    struct AdapterRxCopy* copy;

    if (!___PacketHasBacking(adapter, &packet->Private, NET_ADAPTER_PACKET_COPY)) {
        return OS_ENOENT;
    }

    if (!adapter->Rx.Copy.Entries || packet->Private.Storage.Copy.Slot >= adapter->Rx.Copy.Count) {
        return OS_ENOENT;
    }
    
    copy = &adapter->Rx.Copy.Entries[packet->Private.Storage.Copy.Slot];
    if (!copy->Retained || copy->Sequence != packet->Private.Storage.Copy.Sequence) {
        return OS_ENOENT;
    }

    // The consumer is done with these bytes, so this slot can be overwritten by
    // a later fallback packet and no longer counts against the copy budget.
    copy->Retained = false;
    --adapter->Rx.Copy.Retained;
    return OS_EOK;
}

/**
 * Release a packet that the consumer kept directly from the pool, by giving its RX
 * slot back to the buffer manager. The adapter's record for that slot must still show
 * it as kept by a consumer. If not, the packet belongs to another adapter, is from an
 * earlier use of the slot, was already released, or was never kept by a consumer, and
 * the release is refused.
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
    
    // Give the slot back to the buffer manager first. If that fails, leave the
    // record and the count unchanged, since the slot was not actually released.
    status = NetBuffersRelease(adapter->Buffers, &entry->Lease);
    if (status != OS_EOK) {
        return status;
    }
    
    // The lease is now returned, so discard its lookup record and lower the
    // count of pool slots held by consumers.
    memset(entry, 0, sizeof(*entry));
    --adapter->Rx.PoolRetained;
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
            return OS_ENOENT; // Cleared or unrecognized packet; never read its Storage.
    }
    
    // Clear the packet so it cannot be released twice. If the adapter is already
    // closed and this was the last packet held, its buffers can now be freed.
    memset(packet, 0, sizeof(*packet));
    NetAdapterReclaimClosedBuffers(adapter);
    return OS_EOK;
}
