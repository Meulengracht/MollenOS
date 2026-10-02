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
 * Local packet-lease states and worker-ownership helpers.
 * 
 */

#ifndef __NETD_ADAPTER_LEASE_H__
#define __NETD_ADAPTER_LEASE_H__

#include <stdlib.h>
#include <string.h>

// Include buffers.h for NetBufferLease_t
#include "buffers.h"

/** 
 * @brief Local scheduling/consumer ownership; buffers.c separately tracks DMA and
 * admission/completion facts. PREPARED means a descriptor may have been published,
 * not that its remote admission or completion is already known.
 *
 * TX: FREE -> TX_BUILDING -> QUEUED -> PREPARED -> FREE
 * RX: FREE -> QUEUED -> PREPARED -> RX_RETAINED -> FREE
 * RX QUEUED exists only while preparing a freshly acquired receive slot.
 * Declined retention returns RX_RETAINED to PREPARED for immediate recycling.
 * Stop/close can retire QUEUED/PREPARED after the appropriate ownership fence;
 * caller-owned TX_BUILDING/RX_RETAINED require explicit cancel/release.
 */
enum AdapterLeaseState {
    ADAPTER_LEASE_FREE,
    ADAPTER_LEASE_TX_BUILDING,
    ADAPTER_LEASE_QUEUED,
    ADAPTER_LEASE_PREPARED,
    ADAPTER_LEASE_RX_RETAINED
};

struct AdapterLease {
    NetBufferLease_t       Lease;
    uint64_t               Cookie;
    uint64_t               Order;
    enum AdapterLeaseState State;
    // A stop/start invalidates submission, but not cancellation.
    uint64_t               BuildRun;
    uint32_t               Length;
};

/**
 * @brief Only these states may be reclaimed by completion, rejection or safe close.
 * @param entry The adapter lease entry to check against.
 * @return True if the lease is currently worker-owned, false otherwise.
 */
static inline bool
__AdapterLeaseIsWorkerOwned(
    _In_ const struct AdapterLease* entry)
{
    return entry->State == ADAPTER_LEASE_QUEUED || entry->State == ADAPTER_LEASE_PREPARED;
}

#endif // __NETD_ADAPTER_LEASE_H__
