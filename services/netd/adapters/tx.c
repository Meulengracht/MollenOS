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
 * A caller reserves a TX pool slot, writes a frame directly into shared memory,
 * and then either queues it with Submit or drops it with Cancel. Until then,
 * only the caller touches that slot.
 */

#include <os/osdefs.h>
#include "session.h"
#include "private.h"

static bool
__AdapterCanTransmit(
    _In_ const NetworkAdapter_t* adapter)
{
    // Transmit is allowed only when the adapter is actively running, no stop or
    // close has been asked for, and the physical link is up.
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

    // Refuse new TX slots once stopping, closing, or link-down starts.
    if (!__AdapterCanTransmit(adapter)) {
        memset(packet, 0, sizeof(NetAdapterTxPacket_t));
        return OS_ENOTCONNECTED;
    }

    // Take a free TX slot from the shared pool before exposing its buffer.
    oserr = NetBuffersAcquire(
        adapter->Buffers,
        CTT_NETADAPTER_DIRECTION_TX,
        &lease
    );
    if (oserr != OS_EOK) {
        memset(packet, 0, sizeof(NetAdapterTxPacket_t));
        return oserr;
    }

    // Resolve the CPU pointer for the slot. Release the lease if lookup fails.
    oserr = NetBuffersView(adapter->Buffers, &lease, &view);
    if (oserr != OS_EOK) {
        NetBuffersRelease(adapter->Buffers, &lease);
        memset(packet, 0, sizeof(NetAdapterTxPacket_t));
        return oserr;
    }
    
    // Mark this slot as currently being written by the caller and tag it with
    // the current traffic run (Window.Run).
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

    if (!packet) {
        return OS_EINVALPARAMS;
    }

    // Verify this handle belongs to this adapter and still sits in TX_BUILDING.
    // Already submitted or cancelled handles fail with OS_ENOENT.
    entry = NetAdapterFindPacketLease(
        adapter,
        &packet->Private,
        CTT_NETADAPTER_DIRECTION_TX,
        ADAPTER_LEASE_TX_BUILDING
    );
    if (!entry) {
        return OS_ENOENT;
    }

    // Re-read the real slot capacity from the pool instead of trusting
    // packet->Capacity, which sits in caller-writable struct memory.
    status = NetBuffersView(adapter->Buffers, &entry->Lease, &view);
    if (status != OS_EOK) {
        return status;
    }

    // Check that the frame covers at least the 14-byte Ethernet header and fits
    // within both the negotiated MTU and the slot size.
    if (length < NET_ADAPTER_ETHERNET_HEADER_SIZE ||
        length > adapter->Mtu + NET_ADAPTER_ETHERNET_HEADER_SIZE ||
        length > view.Capacity) {
        return OS_EINVALPARAMS;
    }

    // Reject packets built during an earlier run before a stop/restart. The
    // caller must cancel those instead.
    if (!__AdapterCanTransmit(adapter) || entry->BuildRun != adapter->Window.Run) {
        return OS_ENOTCONNECTED;
    }

    // Frames send oldest first by QueueOrder. Refuse to wrap past UINT64_MAX
    // so new packets can never sort ahead of older ones.
    if (adapter->Tx.QueueOrder == UINT64_MAX) {
        return OS_EOVERFLOW;
    }

    // Hand the slot to the TX queue and clear the caller's packet struct so
    // it cannot be submitted or cancelled again.
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

    if (!packet) {
        return OS_EINVALPARAMS;
    }

    // Only cancel a slot still in TX_BUILDING so a stale handle can never free
    // a slot that was already queued or reused.
    entry = NetAdapterFindPacketLease(
        adapter,
        &packet->Private,
        CTT_NETADAPTER_DIRECTION_TX,
        ADAPTER_LEASE_TX_BUILDING
    );
    if (!entry) {
        return OS_ENOENT;
    }

    // Return the slot to the pool first. If the adapter is already closed and
    // this was the final held packet, free the session's remaining buffers.
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

    // Validate length bounds before taking a TX slot from the pool.
    if (!adapter || !frame || length < NET_ADAPTER_ETHERNET_HEADER_SIZE ||
        length > adapter->Mtu + NET_ADAPTER_ETHERNET_HEADER_SIZE) {
        return OS_EINVALPARAMS;
    }

    // Use the Acquire/Submit path so copied and zero-copy sends share the same
    // validation and queuing logic.
    status = NetAdapterTxAcquire(adapter, &packet);
    if (status != OS_EOK) {
        return status;
    }

    memcpy(packet.Data, frame, length);
    status = NetAdapterTxSubmit(adapter, &packet, length, cookie);

    // Give the slot back if queuing failed (for example, link dropped mid-call).
    if (status != OS_EOK) {
        NetAdapterTxCancel(adapter, &packet);
    }
    return status;
}