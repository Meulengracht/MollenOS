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
 * Private session state and serialized scheduler helpers.
 * 
 * Private network-adapter session state and internal scheduler helpers.
 *
 * This header is intentionally not part of the public adapter API. The declarations
 * here are used by the netd adapter core to manage the session lifetime, who manages
 * each packet, retry deadlines and the request scheduler. The code assumes that only
 * one worker uses an adapter at a time: there are no locks, completions are never
 * processed from inside another call into the core, and the packet path needs no
 * extra synchronization. The netd registry orders calls coming from different
 * threads; host tests call the same core directly.
 *
 * Rules the whole implementation relies on:
 * - a running session talks to exactly one driver and one session identity; messages
 *   carrying any other identity are ignored
 * - packets and leases only move forward through their states; they never move back
 *   unless the driver sends a response that allows it
 * - when a batch or control request is resent, it keeps its original operation ID,
 *   even though the transport sends it in a new Gracht message with a new message ID
 * - stop and close take priority over normal packet traffic, and must be finished
 *   before queue slots are reused or the session is released
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
 * @brief Compute a deadline, stopping at UINT64_MAX instead of wrapping around.
 *
 * The scheduler uses deadlines to decide when a request, batch, ACK or drain should
 * be treated as lost and sent again. The time comes from a clock that never goes
 * backwards. If the sum wrapped around, the deadline would land in the past and
 * cause an immediate retry, so it is held at UINT64_MAX instead.
 *
 * @param now The current time in milliseconds.
 * @param interval The desired interval to add to the current time.
 * @return now + interval, or UINT64_MAX if adding the interval would overflow.
 */
static inline uint64_t
__NetAdapterDeadline(
    _In_ uint64_t now,
    _In_ uint32_t interval)
{
    return now > UINT64_MAX - interval ? UINT64_MAX : now + interval;
}

/**
 * @brief Check that a packet belongs to this adapter and uses the expected kind of storage.
 *
 * A packet is backed either by a lease from the shared pool or by one of the RX
 * copy slots, and the identity stores one or the other in a union. Lookup and release
 * code calls this before reading that union, so a packet from another adapter, or a
 * copy mistaken for a pool lease (or the other way around), is rejected.
 * @param adapter The network adapter the packet should belong to.
 * @param identity The identity of the packet to check.
 * @param backing The expected backing type.
 * @return true if the packet is backed by the specified type, false otherwise.
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
 * @brief Free the buffer storage of a closed adapter once nothing uses it anymore.
 *
 * After the driver has confirmed the close, callers may still hold received packets
 * or TX packets they were building. This does nothing until the adapter is CLOSED
 * and every such packet has been returned; then the pools, lease tables and RX copy
 * slots are freed. It is called again on each release, so the last one frees them.
 */
__EXTERN void
NetAdapterReclaimClosedBuffers(
    _In_ NetworkAdapter_t* adapter);

/**
 * @brief Offer one received buffer to the consumer.
 *
 * If a ReceivePacket callback is set and consumers hold fewer pool slots than they
 * are allowed to, the pool buffer itself is offered; if it is accepted, the lease is
 * managed by the consumer until it releases it. Otherwise the frame may be offered
 * as a copy in an RX copy slot, or passed to the Receive callback, which may only
 * use it during the call. With no callback at all, the frame is counted as dropped.
 *
 * @return true if the consumer accepted the pool buffer itself, so the lease must stay
 *         in use until the consumer releases it; false if the pool slot can be given
 *         back right away (the frame was copied, only lent out, or dropped).
 */
__EXTERN bool
NetAdapterDeliverReceive(
    _In_ NetworkAdapter_t*      adapter,
    _In_ struct AdapterLease*   entry,
    _In_ const NetBufferView_t* view);

/**
 * @brief Check whether a message's driver and session identity match this adapter.
 *
 * Returns true only if the driver, the session ID and the session generation all
 * match the adapter's current session. Used before acting on a message from the
 * driver, so a message from an older session, or one meant for another session, is
 * ignored instead of changing the state of packets in this one.
 */
__EXTERN bool
NetAdapterMatchesSession(
    _In_ const NetworkAdapter_t*              adapter,
    _In_ uuid_t                               driver,
    _In_ const struct ctt_netadapter_session* session);

/**
 * @brief Record a failure and start shutting the adapter down.
 *
 * The error is kept in LastError for diagnostics and any pending control request is
 * dropped. If a session is open, a close is requested and the adapter moves to
 * CLOSING, so the driver closes the session properly before anything is freed.
 * Without a session there is nothing to close, so the adapter goes straight to
 * FAILED. Either way the current session will not carry new packet traffic again.
 */
__EXTERN void
NetAdapterMarkFailed(
    _In_ NetworkAdapter_t* adapter,
    _In_ oserr_t           status);

/**
 * @brief Find the adapter's lease entry for a pool lease.
 *
 * Returns NULL unless the lease belongs to the current session, its slot is in use
 * and its sequence number matches. Completion and release paths use this to make
 * sure they act on a packet that is still in use in this adapter, and not on one
 * left over from an earlier use of the same slot or from an earlier session.
 */
__EXTERN struct AdapterLease*
NetAdapterFindLease(
    _In_ NetworkAdapter_t*       adapter,
    _In_ const NetBufferLease_t* lease);

/**
 * @brief Find the lease entry for a pool-backed packet that a caller handed back.
 *
 * Returns NULL if the packet belongs to another adapter, is backed by a copy slot,
 * has a different direction, or its lease is not in the expected state. Release and
 * cancel paths use this to check that a packet token returned by a caller is still
 * valid before acting on it.
 */
__EXTERN struct AdapterLease*
NetAdapterFindPacketLease(
    _In_ NetworkAdapter_t* adapter,
    _In_ const NetAdapterPacketIdentity_t* identity,
    _In_ enum ctt_netadapter_direction direction,
    _In_ enum AdapterLeaseState state);

/**
 * @brief Report the final result of a lease managed by the worker and give its slot back.
 *
 * For TX, the result is reported through the Transmitted callback. For RX, when
 * receive is true and the status is OS_EOK, the packet is first offered to the
 * consumer; if the consumer keeps the pool buffer, the slot stays in use until it is
 * released. In every other case the slot goes back to the pool. Leases held by a
 * caller are refused: OS_EEXISTS for a kept RX packet, OS_EBUSY otherwise.
 */
__EXTERN oserr_t
NetAdapterReleaseLease(
    _In_    NetworkAdapter_t*    adapter,
    _InOut_ struct AdapterLease* entry,
    _In_    oserr_t              status,
    _In_    bool                 receive);

/**
 * @brief Release a lease for which the driver has reported a completion.
 *
 * The buffer manager must already have recorded the completion for this lease;
 * otherwise OS_EPROTOCOL is returned. The lease is then released with the status
 * from that completion. Received packets are only offered to the consumer while no
 * close has been requested.
 */
__EXTERN oserr_t
NetAdapterReleaseCompletedLease(
    _In_ NetworkAdapter_t*       adapter,
    _In_ const NetBufferLease_t* lease);

/**
 * @brief Process one completion record from the driver.
 *
 * The buffer manager checks the record against the current session and records it;
 * a duplicate of a completion we already have is ignored. A completion can arrive
 * before the driver has told us it accepted the packet, in which case it is held
 * until that is known. Otherwise the lease is released with the completion status.
 */
__EXTERN oserr_t
NetAdapterCompletePacket(
    _In_ NetworkAdapter_t*                       adapter,
    _In_ const struct ctt_netadapter_completion* completion);

/**
 * @brief Size and allocate the adapter's packet pools after the session is opened.
 *
 * Decides how many TX and RX slots to use, within the driver's limits, and checks
 * that the pools, lease tables and RX copy slots all fit in the configured memory
 * budget. This has to happen before the pools are registered with the driver; until
 * then no RX or TX batches can be sent.
 */
__EXTERN oserr_t
NetAdapterSetupBuffers(
    _InOut_ NetworkAdapter_t* adapter);

/**
 * @brief Cancel TX frames that are still waiting in the local queue.
 *
 * Used when the adapter stops or shuts down. Each queued frame is reported as
 * OS_ECANCELLED through Transmitted and its slot is freed. Frames that were already
 * placed in a batch (PREPARED) are left alone, because the driver may already be
 * using them; they are settled by a completion or by closing the session.
 */
__EXTERN void
NetAdapterCancelQueued(
    _InOut_ NetworkAdapter_t* adapter);

/**
 * @brief Fill a request with packets for a new RX or TX batch.
 *
 * For RX, new receive slots are taken from the pool until the number of receive
 * buffers given to the driver reaches the RX target. For TX, queued frames are taken
 * oldest first. Each packet is marked PREPARED, and the request gets the next batch
 * ID. That ID stays the same if the batch has to be resent. Returns OS_ENOENT if
 * there was nothing to put in the batch.
 */
__EXTERN oserr_t
NetAdapterBuildBatch(
    _In_    NetworkAdapter_t*    adapter,
    _In_    bool                 rx,
    _InOut_ NetAdapterRequest_t* request);

/**
 * @brief Find a batch in the window by its batch ID.
 *
 * Used to match an answer from the driver with the batch it belongs to, and to
 * resend a batch unchanged. Returns NULL if no window slot holds that batch, for
 * example because the driver has already confirmed it is finished with it.
 */
__EXTERN struct AdapterBatch*
NetAdapterFindBatch(
    _In_ NetworkAdapter_t* adapter,
    _In_ uint64_t          id);


#endif
