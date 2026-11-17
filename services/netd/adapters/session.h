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
 * Private adapter session state and internal scheduler helpers.
 *
 * Internal header for netd/adapters, excluded from the public API. The core
 * uses these functions to track session lifetime, slot state, retry timers,
 * and request scheduling.
 *
 * Everything here assumes single-threaded access per adapter. There are no
 * locks inside the adapter core, completions never fire reentrantly inside
 * another core call, and the packet path uses no extra synchronization. In
 * netd, adapters.c serializes calls under g_lock. Host tests drive these
 * functions directly on one thread.
 *
 * Core rules the adapter relies on:
 * - An active session talks to one driver and one session (ID + generation).
 *   Messages with any other identity are dropped.
 * - Packet leases only advance forward through their states unless a driver
 *   reply explicitly rejects a submission.
 * - Resent batches and control requests keep their original operation IDs,
 *   even though the transport sends them in a fresh Gracht message.
 * - Stop and close requests take priority over normal traffic and must finish
 *   before pool slots are reused or the session is freed.
 */
#ifndef __NETD_ADAPTER_SESSION_H__
#define __NETD_ADAPTER_SESSION_H__

#include <stdlib.h>
#include <string.h>

#include "lease.h"
#include "adapter.h"

// forward declarations
struct AdapterBatch;

/**
 * @brief Add interval to now, capping at UINT64_MAX instead of wrapping around.
 *
 * The scheduler uses deadlines to spot lost requests, batches, ACKs, and drains.
 * Time comes from a monotonic clock that never goes backwards. Wrapping around
 * would place the deadline in the past and trigger immediate retries.
 *
 * @param now Current monotonic time in milliseconds.
 * @param interval Delay to add in milliseconds.
 * @return now + interval, or UINT64_MAX if the sum would overflow.
 */
static inline uint64_t
__NetAdapterDeadline(
    _In_ uint64_t now,
    _In_ uint32_t interval)
{
    return now > UINT64_MAX - interval ? UINT64_MAX : now + interval;
}

/**
 * @brief Verify that a packet handle belongs to this adapter and uses the
 * expected backing storage (pool lease or backup RX copy).
 *
 * The packet identity stores either a pool lease or a copy slot inside a union.
 * Checking the tag first prevents reading the wrong union member or accepting a
 * packet from another adapter.
 *
 * @param adapter Adapter that should own the packet.
 * @param identity Packet identity to inspect.
 * @param backing Expected backing storage type.
 * @return true if adapter and identity match backing, false otherwise.
 */
static inline bool
___PacketHasBacking(
    _In_ const NetworkAdapter_t*           adapter,
    _In_ const NetAdapterPacketIdentity_t* identity,
    _In_ enum NetAdapterPacketBacking      backing)
{
    if (!adapter || !identity) {
        return false;
    }
    return identity->Owner == adapter && identity->Backing == backing;
}

/**
 * @brief Free a closed adapter's buffer pools, lease arrays, and RX copy slots
 * once callers have returned every held packet.
 *
 * Runs when close finishes, and again each time a caller releases or cancels a
 * held packet so the final return triggers cleanup. Does nothing if the adapter
 * is not CLOSED or if any packets are still held.
 *
 * @param adapter Adapter whose buffers may be ready to free.
 */
__EXTERN void
NetAdapterReclaimClosedBuffers(
    _In_ NetworkAdapter_t* adapter);

/**
 * @brief Deliver a completed RX frame to the consumer callbacks.
 *
 * If ReceivePacket is set and the consumer holds fewer than RetentionLimit pool
 * slots, offer the pool slot directly. If accepted, the slot stays held until
 * NetAdapterRxRelease. Past that limit, copy the frame into a backup RX copy
 * slot and offer that copy instead. If ReceivePacket is unset, declines, or
 * copy slots are full, fall back to Receive (valid only during the call), or
 * increment Rx.Dropped if no receive callback exists.
 *
 * @param adapter Adapter that received the frame.
 * @param entry Lease entry for the RX pool slot.
 * @param view CPU view of the completed RX buffer.
 * @return true if the consumer kept the pool slot itself, false if the pool
 *         slot can return to the pool right away.
 */
__EXTERN bool
NetAdapterDeliverReceive(
    _In_ NetworkAdapter_t*      adapter,
    _In_ struct AdapterLease*   entry,
    _In_ const NetBufferView_t* view);

/**
 * @brief Check whether a driver UUID and session match this adapter's active
 * session (nonzero session ID, same ID, same generation).
 * Used to drop stale messages left over from earlier sessions or drivers.
 *
 * @param adapter Adapter to check against.
 * @param driver Source driver UUID.
 * @param session Session ID and generation from the incoming message.
 * @return true if all fields match an active session, false otherwise.
 */
__EXTERN bool
NetAdapterMatchesSession(
    _In_ const NetworkAdapter_t*              adapter,
    _In_ uuid_t                               driver,
    _In_ const struct ctt_netadapter_session* session);

/**
 * @brief Record a fatal adapter error and begin tearing down the session.
 *
 * Saves status in LastError and drops any pending control request. If a driver
 * session is open, requests a close and moves to CLOSING so the driver confirms
 * before buffers are freed. Without an open session, moves straight to FAILED.
 *
 * @param adapter Adapter that hit the error.
 * @param status Error code to record.
 */
__EXTERN void
NetAdapterMarkFailed(
    _In_ NetworkAdapter_t* adapter,
    _In_ oserr_t           status);

/**
 * @brief Look up the worker's lease entry matching a pool lease.
 *
 * Validates that the lease matches the current session ID and generation, has
 * a valid direction, points to an active slot, and matches the slot's current
 * sequence number.
 *
 * @param adapter Adapter holding the lease tables.
 * @param lease Pool lease to look up.
 * @return Matching lease entry, or null if invalid or stale.
 */
__EXTERN struct AdapterLease*
NetAdapterFindLease(
    _In_ NetworkAdapter_t*       adapter,
    _In_ const NetBufferLease_t* lease);

/**
 * @brief Look up and validate a pool-backed packet handle returned by a caller.
 * Checks adapter match, pool backing, transfer direction, session, sequence,
 * and expected lease state.
 *
 * @param adapter Adapter expected to own the packet.
 * @param identity Packet identity from NetAdapterTxPacket_t or NetAdapterRxPacket_t.
 * @param direction Expected direction (TX or RX).
 * @param state Expected lease state (for example TX_BUILDING or RX_RETAINED).
 * @return Matching lease entry, or null if any check fails.
 */
__EXTERN struct AdapterLease*
NetAdapterFindPacketLease(
    _In_ NetworkAdapter_t*                 adapter,
    _In_ const NetAdapterPacketIdentity_t* identity,
    _In_ enum ctt_netadapter_direction     direction,
    _In_ enum AdapterLeaseState            state);

/**
 * @brief Settle a worker-managed lease and return its slot to the pool.
 *
 * For TX, invokes Transmitted with status. For RX, when receive is true and
 * status is OS_EOK, delivers the packet to the consumer first. If the consumer
 * keeps the pool slot directly, the slot stays held. Otherwise the pool slot
 * is released and cleared. Rejects leases held by callers (OS_EEXISTS for kept
 * RX packets, OS_EBUSY for TX_BUILDING or FREE entries).
 *
 * @param adapter Adapter owning the lease.
 * @param entry Lease entry to settle.
 * @param status Final status for the transfer.
 * @param receive true to deliver successful RX frames to the consumer, false to
 *                skip delivery (used during close or rejection).
 * @return OS_EOK on success, or an error code if the state refuses release.
 */
__EXTERN oserr_t
NetAdapterReleaseLease(
    _In_    NetworkAdapter_t*    adapter,
    _InOut_ struct AdapterLease* entry,
    _In_    oserr_t              status,
    _In_    bool                 receive);

/**
 * @brief Settle and release a lease whose completion has already been recorded
 * in the buffer manager.
 * Suppresses RX delivery once CloseRequested is set.
 *
 * @param adapter Adapter owning the lease.
 * @param lease Pool lease from NetBuffersComplete or NetBuffersAdmission.
 * @return OS_EOK on success, OS_EPROTOCOL if not found or incomplete, or a
 *         release error.
 */
__EXTERN oserr_t
NetAdapterReleaseCompletedLease(
    _In_ NetworkAdapter_t*       adapter,
    _In_ const NetBufferLease_t* lease);

/**
 * @brief Validate and record one completion event from the driver.
 * Ignores duplicate completions cleanly and waits for an earlier admission
 * reply if the completion arrived first. Once ready, settles and releases the
 * lease.
 *
 * @param adapter Adapter receiving the completion.
 * @param completion Completion record from the driver.
 * @return OS_EOK when processed or waiting on admission, OS_EBUSY if earlier
 *         completions are missing, or OS_EPROTOCOL on invalid data.
 */
__EXTERN oserr_t
NetAdapterCompletePacket(
    _In_ NetworkAdapter_t*                       adapter,
    _In_ const struct ctt_netadapter_completion* completion);

/**
 * @brief Size and allocate the TX and RX buffer pools, lease tables, and backup
 * RX copy slots after OPEN succeeds.
 * Caps configured counts against driver limits and MemoryBudget, and keeps RX
 * below max_unacked_completions so receive buffers never starve TX completions.
 *
 * @param adapter Adapter that just finished OPEN.
 * @return OS_EOK on success, OS_EPROTOCOL or OS_ENOTSUPPORTED for unusable
 *         driver limits, OS_EOVERFLOW if MemoryBudget is too small, or OS_EOOM.
 */
__EXTERN oserr_t
NetAdapterSetupBuffers(
    _InOut_ NetworkAdapter_t* adapter);

/**
 * @brief Cancel all TX frames still sitting in the local queue (ADAPTER_LEASE_QUEUED).
 * Reports OS_ECANCELLED via Transmitted and returns each slot to the pool.
 * Packets already packed into a batch (PREPARED) are left alone until settled
 * by a completion or session close.
 *
 * @param adapter Adapter whose queued TX frames should be cancelled.
 */
__EXTERN void
NetAdapterCancelQueued(
    _InOut_ NetworkAdapter_t* adapter);

/**
 * @brief Pack queued TX frames or fresh RX slots into a batch request.
 *
 * For RX, acquires free slots until active RX buffers reach Rx.Target. For TX,
 * picks queued frames oldest first by Order. Each packet transitions to
 * PREPARED before returning so it stays protected if sending fails.
 *
 * @param adapter Adapter building the batch.
 * @param rx true for an RX refill batch, false for a TX batch.
 * @param request Batch request struct to populate.
 * @return OS_EOK when at least one packet was added, OS_ENOENT if no packets
 *         are available to send, OS_EOVERFLOW if NextBatch ran out, or an
 *         error from the buffer manager.
 */
__EXTERN oserr_t
NetAdapterBuildBatch(
    _In_    NetworkAdapter_t*    adapter,
    _In_    bool                 rx,
    _InOut_ NetAdapterRequest_t* request);

/**
 * @brief Find a stored batch in Window.Batches by its batch ID.
 * Used to match incoming admission events and resend overdue batches. Returns
 * null once the driver retires the batch and frees its slot.
 *
 * @param adapter Adapter holding the batch window.
 * @param id Batch ID to search for.
 * @return Matching batch slot, or null if not found.
 */
__EXTERN struct AdapterBatch*
NetAdapterFindBatch(
    _In_ NetworkAdapter_t* adapter,
    _In_ uint64_t          id);


#endif
