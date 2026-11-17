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
 * Keep received packets readable without holding onto driver RX pool slots.
 *
 * Consumers may only keep RxRetainedSlots pool buffers at a time. Keeping more
 * would starve the driver of receive buffers. Past that cap, this file copies
 * incoming frames into preallocated backup buffers (RxCopySlots) so the pool
 * slot can go straight back to the driver.
 *
 * Backup copy slots are allocated when the session opens because allocating on
 * the receive path could fail or stall. Each copy slot tracks a Retained flag
 * and a 64-bit Sequence number that increments on every reuse and stops at
 * UINT64_MAX (never wrapping to zero). That stops a stale release handle from
 * freeing a newer packet that reused the same slot.
 *
 * Returning true from ReceivePacket hands the copy to the consumer until
 * NetAdapterRxRelease. Returning false releases it right away and falls back
 * to the non-retained Receive callback.
 */

#include "session.h"
#include "private.h"

// Saturating increment so a 64-bit counter or sequence never wraps back to 0.
static void
__IncreaseWithoutRollover(
    _InOut_ uint64_t* counter)
{
    if (*counter != UINT64_MAX) {
        ++*counter;
    }
}

/** 
 * @brief Copy a received frame into a free backup copy slot and offer it to
 * ReceivePacket so the shared pool slot can go back to the driver immediately.
 * @param adapter Adapter that received the frame.
 * @param view CPU view of the received frame in the pool slot.
 * @return true only if the consumer accepted the copy, false otherwise.
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
        
        // Mark the copy slot busy and bump Sequence before calling out.
        data = adapter->Rx.Copy.Bytes +
               (size_t)i * (adapter->Mtu + NET_ADAPTER_ETHERNET_HEADER_SIZE);
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
        
        if (adapter->Callbacks.ReceivePacket(adapter->Callbacks.Context, &packet)) {
            return true;
        }
        
        // Consumer declined. Free this copy slot and fall back to Receive.
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
    // Hand out the zero-copy pool slot first while under RetentionLimit, then
    // fall back to backup copies once that cap is full.
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
            
            // Mark the lease as consumer-held before the callback runs.
            entry->State = ADAPTER_LEASE_RX_RETAINED;
            ++adapter->Rx.PoolRetained;
            
            if (adapter->Callbacks.ReceivePacket(adapter->Callbacks.Context, &packet)) {
                return true;
            }
            
            // Consumer declined. Undo the hold so the slot returns to the pool.
            entry->State = ADAPTER_LEASE_PREPARED;
            --adapter->Rx.PoolRetained;
        } else if (__AllocateCopyBuffer(adapter, view)) {
            // Consumer kept a copy, so the pool slot itself can return now.
            return false;
        }
    }
    
    // No ReceivePacket hook or it declined. Lend the bytes via Receive for the
    // duration of the call, or count the frame as dropped if Receive is unset.
    if (adapter->Callbacks.Receive) {
        adapter->Callbacks.Receive(adapter->Callbacks.Context, view->Data, view->Length);
    } else {
        __IncreaseWithoutRollover(&adapter->Rx.Dropped);
    }
    return false;
}

/**
 * @brief Release a backup copy packet back to the adapter's copy array.
 * The original RX pool slot was already returned when the copy was made, so
 * this only frees the copy slot. Checks adapter, copy backing, slot bounds,
 * and Sequence to reject double releases and stale handles.
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

    copy->Retained = false;
    --adapter->Rx.Copy.Retained;
    return OS_EOK;
}

/**
 * @brief Release a zero-copy pool packet back to the buffer manager.
 * Verifies the packet belongs to this adapter, points to an RX pool slot,
 * and is currently in ADAPTER_LEASE_RX_RETAINED.
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
    
    // Release the pool slot first so a failure leaves local tracking intact.
    status = NetBuffersRelease(adapter->Buffers, &entry->Lease);
    if (status != OS_EOK) {
        return status;
    }
    
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
            return OS_ENOENT; // Cleared or invalid backing tag, never read Storage union.
    }
    
    // Clear packet to prevent double-release, then free session buffers if
    // the adapter was already closed and this was the last held packet.
    memset(packet, 0, sizeof(*packet));
    NetAdapterReclaimClosedBuffers(adapter);
    return OS_EOK;
}
