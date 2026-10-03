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
 * @brief The worker uses these states to track who manages a lease locally: the
 * caller, the TX queue, the driver or an RX consumer. DMA, admission and completion
 * are tracked separately by buffers.c. PREPARED means the packet has been placed in
 * a batch that may already have been sent to the driver; it does not mean the driver
 * has accepted or finished it yet.
 *
 * TX: FREE -> TX_BUILDING -> QUEUED -> PREPARED -> FREE
 * RX: FREE -> QUEUED -> PREPARED -> RX_RETAINED -> FREE
 * RX leases are only QUEUED briefly, while a newly acquired receive slot is being
 * added to a batch. If the consumer declines to keep a received packet, the lease
 * goes from RX_RETAINED back to PREPARED so its slot can be reused right away.
 * When the adapter stops or closes, QUEUED and PREPARED leases are freed once the
 * driver is known to no longer use them. TX_BUILDING and RX_RETAINED leases are
 * managed by the caller, who must cancel or release them explicitly.
 */
enum AdapterLeaseState {
    ADAPTER_LEASE_FREE,
    ADAPTER_LEASE_TX_BUILDING,
    ADAPTER_LEASE_QUEUED,
    ADAPTER_LEASE_PREPARED,
    ADAPTER_LEASE_RX_RETAINED
};

/**
 * @brief This structure contains the adapter's local view of one pool slot. Each
 * direction keeps one entry per slot, indexed by the lease's slot number, to track
 * who currently manages the packet (caller, TX queue, driver or RX consumer). For TX it
 * also carries the caller's completion cookie, frame length and queue order.
 */
struct AdapterLease {
    NetBufferLease_t       Lease;
    uint64_t               Cookie;
    uint64_t               Order;
    enum AdapterLeaseState State;
    // Run the TX lease was acquired in. After a stop and restart it can no
    // longer be submitted, but it can still be cancelled.
    uint64_t               BuildRun;
    uint32_t               Length;
};

/**
 * @brief Only leases managed by the worker may be reclaimed by completion, rejection
 * or safe close.
 * @param entry The adapter lease entry to check against.
 * @return True if the lease is currently managed by the worker, false otherwise.
 */
static inline bool
__AdapterLeaseIsManagedByWorker(
    _In_ const struct AdapterLease* entry)
{
    return entry->State == ADAPTER_LEASE_QUEUED || entry->State == ADAPTER_LEASE_PREPARED;
}

#endif // __NETD_ADAPTER_LEASE_H__
