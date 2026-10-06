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
 * Building TX packets and handing them over for transmission.
 *
 * A caller reserves a TX pool slot, writes a frame into it, and then either submits
 * it to the TX queue or cancels it. Until then the slot is managed by the caller.
 */

#include <os/osdefs.h>
#include "session.h"
#include "private.h"

static bool
__AdapterCanTransmit(
    _In_ const NetworkAdapter_t* adapter)
{
    // For an adapter to be able to transmit
    // - Must be in running state
    // - Stop must not be requested
    // - Close must not be requested
    // - Link must be up
    return adapter->State == NET_ADAPTER_RUNNING && !adapter->Intent.StopRequested &&
           !adapter->Intent.CloseRequested && adapter->Link.status == CTT_NETADAPTER_LINK_STATUS_UP;
}

oserr_t
NetAdapterTxAcquire(
    _In_  NetworkAdapter_t*     adapter,
    _Out_ NetAdapterTxPacket_t* packet)
{
    NetBufferLease_t lease;
    NetBufferView_t  view;
    oserr_t          oserr;

    if (!adapter || !packet) {
        return OS_EINVALPARAMS;
    }

    // Only hand out a TX slot while the adapter is running, is not being stopped or
    // closed, and the link is up.
    if (!__AdapterCanTransmit(adapter)) {
        memset(packet, 0, sizeof(NetAdapterTxPacket_t));
        return OS_ENOTCONNECTED;
    }

    // Reserve a TX slot before exposing its payload to the caller.
    oserr = NetBuffersAcquire(
        adapter->Buffers,
        CTT_NETADAPTER_DIRECTION_TX,
        &lease
    );
    if (oserr != OS_EOK) {
        memset(packet, 0, sizeof(NetAdapterTxPacket_t));
        return oserr;
    }

    // Resolve the acquired lease to its writable view; return it if lookup fails.
    oserr = NetBuffersView(adapter->Buffers, &lease, &view);
    if (oserr != OS_EOK) {
        NetBuffersRelease(adapter->Buffers, &lease);
        memset(packet, 0, sizeof(NetAdapterTxPacket_t));
        return oserr;
    }
    
    // Record the slot as being written by the caller, and remember which run of the
    // adapter (Window.Run, increased on every start) it was taken in.
    adapter->Queues[NET_ADAPTER_TX].Leases[lease.Slot] = (struct AdapterLease){
        .Lease = lease,
        .State = ADAPTER_LEASE_TX_BUILDING,
        .BuildRun = adapter->Window.Run
    };
    
    *packet = (NetAdapterTxPacket_t){
        .Data = view.Data,
        .Capacity = view.Capacity,
        .Private = {
            .Owner = adapter,
            .Backing = NET_ADAPTER_PACKET_POOL,
            .Storage.Pool = lease
        }
    };
    return OS_EOK;
}

oserr_t
NetAdapterTxSubmit(
    _In_    NetworkAdapter_t*     adapter,
    _InOut_ NetAdapterTxPacket_t* packet,
    _In_    uint32_t              length,
    _In_    uint64_t              cookie)
{
    struct AdapterLease* entry;
    NetBufferView_t      view;
    oserr_t              status;

    // A packet from NetAdapterTxAcquire is required.
    if (!packet) {
        return OS_EINVALPARAMS;
    }

    // The packet must belong to this adapter and identify a TX slot that is still
    // being written by the caller, not one already submitted or cancelled.
    entry = NetAdapterFindPacketLease(
        adapter,
        &packet->Private,
        CTT_NETADAPTER_DIRECTION_TX,
        ADAPTER_LEASE_TX_BUILDING
    );
    if (!entry) {
        return OS_ENOENT;
    }

    // Look up the slot's real size in the pool instead of trusting the Capacity
    // field in the packet, which the caller could have changed.
    status = NetBuffersView(adapter->Buffers, &entry->Lease, &view);
    if (status != OS_EOK) {
        return status;
    }

    // The frame must hold at least an Ethernet header, carry no more than
    // the MTU as payload, and fit in the slot.
    if (length < NET_ADAPTER_ETHERNET_HEADER_SIZE ||
        length > adapter->Mtu + NET_ADAPTER_ETHERNET_HEADER_SIZE ||
        length > view.Capacity) {
        return OS_EINVALPARAMS;
    }

    // A slot acquired before the adapter was stopped and started again can no
    // longer be submitted; the caller has to cancel it.
    if (!__AdapterCanTransmit(adapter) || entry->BuildRun != adapter->Window.Run) {
        return OS_ENOTCONNECTED;
    }

    // Frames are sent in order of this counter. If it wrapped around to zero, a new
    // frame would look older than frames already queued, so refuse instead.
    if (adapter->Tx.QueueOrder == UINT64_MAX) {
        return OS_EOVERFLOW;
    }

    // Put the slot on the TX queue. From now on the worker manages it, so the
    // caller's packet structure is cleared.
    entry->Cookie = cookie;
    entry->Length = length;
    entry->Order = adapter->Tx.QueueOrder++;
    entry->State = ADAPTER_LEASE_QUEUED;
    memset(packet, 0, sizeof(*packet));

    return OS_EOK;
}

oserr_t
NetAdapterTxCancel(
    _In_    NetworkAdapter_t*     adapter,
    _InOut_ NetAdapterTxPacket_t* packet)
{
    struct AdapterLease* entry;
    oserr_t              status;

    // A packet from NetAdapterTxAcquire is required.
    if (!packet) {
        return OS_EINVALPARAMS;
    }

    // Only cancel a slot the caller is still writing. An old or already submitted
    // packet must not free a slot that is now used by someone else.
    entry = NetAdapterFindPacketLease(
        adapter,
        &packet->Private,
        CTT_NETADAPTER_DIRECTION_TX,
        ADAPTER_LEASE_TX_BUILDING
    );
    if (!entry) {
        return OS_ENOENT;
    }

    // Only forget the slot once the pool has taken it back. If the adapter is
    // already closed and this was the last packet held, its buffers can be freed.
    status = NetBuffersRelease(adapter->Buffers, &entry->Lease);
    if (status == OS_EOK) {
        memset(entry, 0, sizeof(*entry));
        memset(packet, 0, sizeof(*packet));
        NetAdapterReclaimClosedBuffers(adapter);
    }
    return status;
}

oserr_t
NetAdapterSend(
    _In_ NetworkAdapter_t* adapter,
    _In_ const void*       frame,
    _In_ uint32_t          length,
    _In_ uint64_t          cookie)
{
    NetAdapterTxPacket_t packet;
    oserr_t              status;

    // Reject frames that are shorter than an Ethernet header or longer than the MTU
    // allows before taking a pool slot.
    if (!adapter || !frame || length < NET_ADAPTER_ETHERNET_HEADER_SIZE ||
        length > adapter->Mtu + NET_ADAPTER_ETHERNET_HEADER_SIZE) {
        return OS_EINVALPARAMS;
    }

    // Copy the frame into a slot taken the same way as NetAdapterTxAcquire, so copied
    // and directly written frames are checked and queued in exactly the same way.
    status = NetAdapterTxAcquire(adapter, &packet);
    if (status != OS_EOK) {
        return status;
    }

    memcpy(packet.Data, frame, length);
    status = NetAdapterTxSubmit(adapter, &packet, length, cookie);

    // If submission failed, the slot is still ours to manage; give it back.
    if (status != OS_EOK) {
        NetAdapterTxCancel(adapter, &packet);
    }
    return status;
}