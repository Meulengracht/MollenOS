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
 * This header is intentionally not part of the public adapter API. All of the
 * declarations here are used by the netd adapter core to manage session lifetime,
 * packet ownership, retry deadlines, and the request scheduler. The code assumes
 * a single serialized owner: there are no locks, no reentrant completion paths,
 * and no user-visible synchronization on the hot path. The netd registry provides
 * the cross-thread ordering guarantees; host tests drive the same core directly.
 *
 * Invariants that matter across the implementation:
 * - a running session owns a single authoritative driver/session identity
 * - packet and lease ownership transitions are monotonic and cannot be reversed
 *   without an explicit protocol response
 * - batch and control retries must preserve the original logical operation id
 *   even when the transport message is resent with a new Gracht frame id
 * - close/stop transitions are higher priority than ordinary packet traffic and
 *   must be serialized before queue reuse or session release
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
 * @brief Saturate a deadline instead of wrapping and creating premature retries.
 *
 * The adapter scheduler relies on monotonic timeouts to decide when a request,
 * batch, ACK, or drain is considered lost. Saturation keeps the deadline stable
 * at UINT64_MAX rather than wrapping around and causing immediate re-tries.
 *
 * @param now The current monotonic time in milliseconds.
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
 * @brief Determine whether a packet is backed by a specific memory ownership type.
 *
 * Packet buffers may be backed by different allocation or registration sources
 * depending on whether they are in the shared pool, owned directly by the driver,
 * or associated with a receive path. This check prevents cross-backing
 * ownership mismatches during completion processing and retirement.
 * @param adapter The network adapter that owns the packet.
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
 * @brief Release buffer storage that became detached from the session during shutdown.
 *
 * Closing or failing an adapter can leave packet buffers in a state where they no
 * longer have a live session or lease owner. This helper reclaims the storage
 * once all caller-owned packet views have been returned and the pool's lease gate
 * can safely release the underlying memory.
 */
__EXTERN void
NetAdapterReclaimClosedBuffers(
    _In_ NetworkAdapter_t* adapter);

/**
 * @brief Deliver one completed receive buffer to the consumer.
 *
 * Receive delivery is not just a memory operation: it also decides whether the
 * lease should remain retained for the caller while the packet is being processed.
 * A successful or completed delivery is still counted as a lease ownership handoff
 * until the caller returns the lease, so the adapter must not release it early.
 *
 * @return true if the pool lease must remain retained for the consumer,
 *         false if the lease can be released after this handoff completes.
 */
__EXTERN bool
NetAdapterDeliverReceive(
    _In_ NetworkAdapter_t*      adapter,
    _In_ struct AdapterLease*   entry,
    _In_ const NetBufferView_t* view);

/**
 * @brief Check whether a driver and session identity match this adapter.
 *
 * Used by the core registry and session lookup flow to ensure the caller is
 * addressing the correct driver/endpoint pair before issuing or replaying a
 * request. This is a guard against stale or cross-session packet ownership.
 */
__EXTERN bool
NetAdapterMatchesSession(
    _In_ const NetworkAdapter_t*              adapter,
    _In_ uuid_t                               driver,
    _In_ const struct ctt_netadapter_session* session);

/**
 * @brief Mark the adapter as failed and remember the root cause.
 *
 * Failures are terminal for the active session: they stop new work, preserve the
 * error for diagnostics, and force future scheduling decisions to avoid sending
 * more protocol traffic to a compromised adapter state.
 */
__EXTERN void
NetAdapterMarkFailed(
    _In_ NetworkAdapter_t* adapter,
    _In_ oserr_t           status);

/**
 * @brief Look up the lease that owns a specific packet buffer.
 *
 * This is the primary ownership check for packet completion and release paths.
 * It is used to validate that the completion or free request is referring to a
 * live buffer that belongs to this adapter instance and not a stale or detached
 * lease.
 */
__EXTERN struct AdapterLease*
NetAdapterFindLease(
    _In_ NetworkAdapter_t*       adapter,
    _In_ const NetBufferLease_t* lease);

/**
 * @brief Find the lease associated with a packet identity in a given direction/state.
 *
 * Completion and retry recovery often need to search by packet identity, not just
 * by raw lease id. This function narrows the lookup to the relevant direction and
 * lifecycle state so the scheduler can correctly match a completion to the
 * correct outstanding packet work.
 */
__EXTERN struct AdapterLease*
NetAdapterFindPacketLease(
    _In_ NetworkAdapter_t* adapter,
    _In_ const NetAdapterPacketIdentity_t* identity,
    _In_ enum ctt_netadapter_direction direction,
    _In_ enum AdapterLeaseState state);

/**
 * @brief Release a lease and update the adapter state according to the result.
 *
 * This is the common exit path for packet completion, receive delivery, and
 * error-handling recovery. It preserves the adapter's accounting invariants by
 * ensuring that success, failure, and receive-vs-transmit semantics all map to
 * the correct lease retirement or retention decision.
 */
__EXTERN oserr_t
NetAdapterReleaseLease(
    _In_    NetworkAdapter_t*    adapter,
    _InOut_ struct AdapterLease* entry,
    _In_    oserr_t              status,
    _In_    bool                 receive);

/**
 * @brief Release a lease that has already completed successfully.
 *
 * This is the fast path for packets that have already been observed as completed
 * by the transport layer. It avoids reprocessing a packet already accounted for
 * in the completion stream while still preserving the adapter's credit and
 * retirement bookkeeping.
 */
__EXTERN oserr_t
NetAdapterReleaseCompletedLease(
    _In_ NetworkAdapter_t*       adapter,
    _In_ const NetBufferLease_t* lease);

/**
 * @brief Process a driver completion record and advance adapter accounting.
 *
 * Driver completions are authoritative for packet progress. This helper updates
 * counters and ownership state based on the completion metadata so the request
 * scheduler can decide whether additional ACK, drain, or batch work is needed.
 */
__EXTERN oserr_t
NetAdapterCompletePacket(
    _In_ NetworkAdapter_t*                       adapter,
    _In_ const struct ctt_netadapter_completion* completion);

/**
 * @brief Set up the adapter's shared packet buffer state after a pool is opened.
 *
 * Buffer setup is part of the lifecycle handshake: without the shared buffer pool
 * state, there is no stable packet accounting and the scheduler cannot issue
 * RX/TX batches or credit transitions safely.
 */
__EXTERN oserr_t
NetAdapterSetupBuffers(
    _InOut_ NetworkAdapter_t* adapter);

/**
 * @brief Cancel queued work that must be abandoned during a stop or teardown.
 *
 * A stop request is a transition from live packet processing to draining/teardown.
 * Any queued but not yet admitted work must be withdrawn so that the run cannot
 * continue to enqueue packets after the stop barrier has been requested.
 */
__EXTERN void
NetAdapterCancelQueued(
    _InOut_ NetworkAdapter_t* adapter);

/**
 * @brief Build a request object for a new RX or TX batch.
 *
 * The scheduler uses a stable logical request identity for retries. This helper
 * prepares the request contents, including the batch metadata and packet
 * accounting, before the request is admitted to the transport.
 */
__EXTERN oserr_t
NetAdapterBuildBatch(
    _In_    NetworkAdapter_t*    adapter,
    _In_    bool                 rx,
    _InOut_ NetAdapterRequest_t* request);

/**
 * @brief Find a previously created batch by its serialized request id.
 *
 * Batch replay and recovery depend on looking up the exact logical batch that was
 * admitted earlier. Matching by the batch id preserves ordering and lets the
 * scheduler distinguish between a fresh batch and a retried outstanding one.
 */
__EXTERN struct AdapterBatch*
NetAdapterFindBatch(
    _In_ NetworkAdapter_t* adapter,
    _In_ uint64_t          id);


#endif
