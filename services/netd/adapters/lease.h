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
 * Local packet-lease states and worker helpers.
 * 
 */

#ifndef __NETD_ADAPTER_LEASE_H__
#define __NETD_ADAPTER_LEASE_H__

#include <stdlib.h>
#include <string.h>

// Include buffers.h for NetBufferLease_t
#include "buffers.h"

/** 
 * @brief Tracks who currently handles a pool slot on the adapter worker side.
 *
 * Low-level shared memory state (DMA, admission, and completion) lives inside
 * buffers.c. This enum tracks the higher-level handoff between callers, the
 * worker queues, and the driver.
 *
 *   FREE        - Unused entry in the adapter's lease array.
 *   TX_BUILDING - Caller acquired a TX slot via NetAdapterTxAcquire and is
 *                 still writing frame bytes into it.
 *   QUEUED      - Caller submitted a TX frame (or worker grabbed an RX slot)
 *                 and it is waiting to be packed into a batch.
 *   PREPARED    - Packed into a batch that may already have been sent to the
 *                 driver. Does not mean the driver accepted or finished it yet.
 *   RX_RETAINED - Consumer accepted a received packet from ReceivePacket and
 *                 must release it later with NetAdapterRxRelease.
 *
 * Typical paths:
 *   TX: FREE -> TX_BUILDING -> QUEUED -> PREPARED -> FREE
 *   RX: FREE -> QUEUED -> PREPARED -> RX_RETAINED -> FREE
 *
 * RX leases sit in QUEUED only briefly while a newly acquired slot is being
 * packed into a batch. If the consumer declines a packet, its lease returns
 * from RX_RETAINED to PREPARED so the slot can go straight back to the pool.
 *
 * When stopping or closing, QUEUED and PREPARED leases are released once the
 * driver stops using them. TX_BUILDING and RX_RETAINED leases belong to callers,
 * who must cancel or release them explicitly.
 */
enum AdapterLeaseState {
    ADAPTER_LEASE_FREE,
    ADAPTER_LEASE_TX_BUILDING,
    ADAPTER_LEASE_QUEUED,
    ADAPTER_LEASE_PREPARED,
    ADAPTER_LEASE_RX_RETAINED
};

/**
 * @brief Worker-side tracking record for a single TX or RX pool slot.
 * Each direction keeps one array of these indexed by the slot number.
 */
struct AdapterLease {
    NetBufferLease_t       Lease;     // Pool handle tying this entry to the buffer manager.
    uint64_t               Cookie;    // TX caller value passed back to Transmitted.
    uint64_t               Order;     // Monotonic TX queue order so reused slots still send FIFO.
    enum AdapterLeaseState State;     // Who currently holds this slot.
    // Traffic run (Window.Run) when this TX lease was acquired. If the adapter
    // stops and restarts, the caller can still cancel it, never submit it.
    uint64_t               BuildRun;
    uint32_t               Length;    // Frame length in bytes for TX, or full slot capacity for RX.
};

/**
 * @brief Check whether the worker (not a caller or RX consumer) controls a lease.
 * Only worker-managed leases can be freed automatically on completion, batch
 * rejection, or session close.
 *
 * @param entry Lease entry to check.
 * @return true if the lease is QUEUED or PREPARED, false otherwise.
 */
static inline bool
__AdapterLeaseIsManagedByWorker(
    _In_ const struct AdapterLease* entry)
{
    return entry->State == ADAPTER_LEASE_QUEUED || entry->State == ADAPTER_LEASE_PREPARED;
}

#endif // __NETD_ADAPTER_LEASE_H__
