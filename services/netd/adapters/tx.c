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
 * TX packet construction and ownership transfer.
 *
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
    return adapter->State == NET_ADAPTER_RUNNING && !adapter->StopRequested &&
           !adapter->CloseRequested && adapter->Link.status == CTT_NETADAPTER_LINK_STATUS_UP;
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

    // A builder may be issued only while the session and carrier can transmit.
    if (!__AdapterCanTransmit(adapter)) {
        return OS_ENOTCONNECTED;
    }

    // Reserve a TX slot before exposing its payload to the caller.
    oserr = NetBuffersAcquire(
        adapter->Buffers,
        CTT_NETADAPTER_DIRECTION_TX,
        &lease
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Resolve the acquired lease to its writable view; return it if lookup fails.
    oserr = NetBuffersView(adapter->Buffers, &lease, &view);
    if (oserr != OS_EOK) {
        NetBuffersRelease(adapter->Buffers, &lease);
        return oserr;
    }
    
    // Record the lease in the adapter's lease table as a building TX lease.
    adapter->Leases[0][lease.Slot] = (struct AdapterLease){
        .Lease = lease,
        .State = ADAPTER_LEASE_TX_BUILDING,
        .BuildRun = adapter->Run
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

    // Submission requires the caller's acquired packet token.
    if (!packet) {
        return OS_EINVALPARAMS;
    }

    // Accept only a live TX builder token belonging to this adapter.
    entry = NetAdapterFindPacketLease(
        adapter,
        &packet->Private,
        CTT_NETADAPTER_DIRECTION_TX,
        ADAPTER_LEASE_TX_BUILDING
    );
    if (!entry) {
        return OS_ENOENT;
    }

    // Validate against the authoritative pool view, not caller-editable capacity.
    status = NetBuffersView(adapter->Buffers, &entry->Lease, &view);
    if (status != OS_EOK) {
        return status;
    }

    // Enforce Ethernet frame bounds against the actual slot capacity.
    if (length < 14 || length > adapter->Mtu + 14 || length > view.Capacity) {
        return OS_EINVALPARAMS;
    }

    // Stop/restart invalidates builders from the previous run.
    if (!__AdapterCanTransmit(adapter) || entry->BuildRun != adapter->Run) {
        return OS_ENOTCONNECTED;
    }

    // Queue order must not wrap and make a new frame appear older than queued work.
    if (adapter->QueueOrder == UINT64_MAX) {
        return OS_EOVERFLOW;
    }

    // Transfer the builder lease to the serialized TX queue.
    entry->Cookie = cookie;
    entry->Length = length;
    entry->Order = adapter->QueueOrder++;
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

    // Cancellation requires the caller's acquired packet token.
    if (!packet) {
        return OS_EINVALPARAMS;
    }

    // Do not let a stale or already-submitted token release another lease.
    entry = NetAdapterFindPacketLease(
        adapter,
        &packet->Private,
        CTT_NETADAPTER_DIRECTION_TX,
        ADAPTER_LEASE_TX_BUILDING
    );
    if (!entry) {
        return OS_ENOENT;
    }

    // Clear ownership metadata only after the pool accepts the release.
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

    // Reject invalid Ethernet frames before acquiring a pool slot.
    if (!adapter || !frame || length < 14 || length > adapter->Mtu + 14) {
        return OS_EINVALPARAMS;
    }

    // Reuse the zero-copy builder path so ownership and ordering stay identical.
    status = NetAdapterTxAcquire(adapter, &packet);
    if (status != OS_EOK) {
        return status;
    }

    memcpy(packet.Data, frame, length);
    status = NetAdapterTxSubmit(adapter, &packet, length, cookie);

    // A failed submission leaves the builder lease caller-owned; return it here.
    if (status != OS_EOK) {
        NetAdapterTxCancel(adapter, &packet);
    }
    return status;
}