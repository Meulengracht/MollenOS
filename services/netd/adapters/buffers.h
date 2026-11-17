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
 * Packet-pool API and per-slot tracking rules.
 * 
 * Packet data lives in shared memory kept for the full lifetime of the session,
 * while the records tracking who holds each slot live in netd's private memory.
 *
 * One manager belongs to one driver session and manages one TX pool and one RX
 * pool. Slots are padded to match the driver's alignment and keep neighboring
 * slots on separate CPU cache lines so hardware DMA and CPU reads do not collide.
 *
 * Never create a second manager for a session that is still open, because a new
 * manager starts pool registration IDs back at 1 and would clash with IDs the
 * driver already has. All calls, including using a lease, must run one at a time
 * on the adapter's single worker loop. This module takes no locks, sends no IPC,
 * retries no batches, and sets up no DMA mappings itself.
 *
 * Slot lifecycle:
 *   FREE -> HELD -> PENDING -> DRIVER -> READY -> FREE
 *                         \-> READY (completion arrived before admission reply)
 *                    rejected -> HELD
 *
 *   FREE    - Available in the pool.
 *   HELD    - Acquired by netd (caller is writing TX data or preparing RX).
 *   PENDING - Sent to the driver, waiting for the driver to confirm it took it.
 *   DRIVER  - Accepted by the driver and actively in use by hardware.
 *   READY   - Driver finished the transfer and reported its final result.
 *
 * A READY slot cannot return to FREE until its admission reply arrives and gets
 * matched. PENDING means control over the buffer is uncertain: even a failed IPC
 * send might have reached the driver, so the driver could still touch that memory.
 *
 * Read received data before releasing its lease. Never keep a NetBufferView_t
 * pointer across NetBuffersRelease or NetBuffersPrepare. The TX builder API lets
 * callers write frame bytes outside the worker while the lease stays HELD, though
 * direct callers of this manager must run one call at a time.
 */
#ifndef __NETD_BUFFERS_H__
#define __NETD_BUFFERS_H__

#include <os/osdefs.h>
#include <ctt_netadapter_service.h>

// Fixed 14-byte Ethernet header size included in every frame.
// Redefined here so buffers.h does not have to include adapter.h.
#define NET_ADAPTER_ETHERNET_HEADER_SIZE 14

// Opaque handle. All manager state lives in netd's private heap, never in shared
// memory, so the driver cannot corrupt bookkeeping records.
typedef struct NetBufferManager NetBufferManager_t;

/** 
 * @brief Pool sizing and memory budget parameters.
 * All sizes are in bytes and every field must be nonzero. FrameCapacity is the
 * largest frame a slot can carry (including the 14-byte Ethernet header, without
 * hardware FCS or driver-private headers). MemoryBudget caps all memory used by
 * the manager: page-rounded shared pools, the manager struct, per-slot records,
 * and the completion table. Everything is allocated up front when created.
 */
typedef struct NetBufferConfig {
    uint32_t TxSlots;
    uint32_t RxSlots;
    uint32_t FrameCapacity;
    uint64_t MemoryBudget;
} NetBufferConfig_t;

/** 
 * @brief Local handle to an acquired pool slot. Never sent over IPC.
 * Sequence increments each time the slot is acquired, stopping an old handle
 * from touching or releasing the slot after it gets reused. Treat fields as
 * read-only.
 */
typedef struct NetBufferLease {
    struct ctt_netadapter_session Session;
    enum ctt_netadapter_direction Direction;
    uint32_t Slot;
    uint64_t Sequence;
} NetBufferLease_t;

/** 
 * @brief CPU view into a slot's frame area while the caller holds the lease.
 * Length is zero for HELD slots. Completed turns true once the driver reports a
 * final result. Always check Status and Detail before reading Data, because a
 * failed or cancelled transfer holds no valid frame bytes. Capacity covers only
 * the usable frame area, excluding device headroom, tailroom, and padding.
 */
typedef struct NetBufferView {
    void* Data;
    uint32_t Capacity;
    uint32_t Length;
    bool Completed;
    enum ctt_netadapter_completion_status Status;
    oserr_t Detail;
} NetBufferView_t;

/** 
 * @brief Snapshot of buffer pool usage and completion progress for diagnostics
 * and flow control. These are netd's local counters, separate from hardware stats.
 */
typedef struct NetBufferStats {
    uint32_t TxFree;                 // Free TX slots available for new packets.
    uint32_t RxFree;                 // Free RX slots available for posting to the driver.
    uint32_t TxOutstanding;          // TX slots currently pending admission or with the driver.
    uint32_t RxOutstanding;          // RX slots currently pending admission or with the driver.
    uint32_t UnackedCompletions;     // Completed transfers received locally but not yet confirmed retired.
    uint64_t ProcessedCompletion;    // Highest completion sequence processed with no earlier gaps.
    uint64_t AcknowledgedCompletion; // Highest completion sequence the driver has confirmed retired.
} NetBufferStats_t;

/** 
 * @brief Allocate zeroed TX and RX shared-memory pools and private slot tables.
 *
 * Checks driver limits, integer overflows, alignment, and MemoryBudget before
 * allocating anything. Neighboring slots are contiguous in virtual memory, though
 * physical pages may be scattered so the driver still handles its own DMA setup.
 * On failure, *managerOut stays null and any partial allocations are freed.
 * 
 * @param session Active driver session identity.
 * @param info Driver capabilities and memory limits.
 * @param config Slot counts, frame capacity, and total memory cap.
 * @param managerOut Receives the new buffer manager on success.
 * @return OS_EOK on success, OS_EINVALPARAMS for bad inputs, OS_EOVERFLOW if
 *         sizes exceed limits or MemoryBudget, OS_EOOM on allocation failure,
 *         or a shared-memory error.
 */
oserr_t
NetBuffersCreate(
    _In_  const struct ctt_netadapter_session* session,
    _In_  const struct ctt_netadapter_info*    info,
    _In_  const NetBufferConfig_t*             config,
    _Out_ NetBufferManager_t**                 managerOut);

/** 
 * @brief Destroy the buffer manager and release its shared memory.
 *
 * Allowed only when both pools are unregistered (or were never registered) and
 * every lease has been returned. Otherwise returns OS_EBUSY without changing
 * state. Request timeouts, driver faults, or stopping a run do not unregister a
 * pool, because the driver might still access the memory. Only explicit
 * unregister replies or a confirmed session close do.
 * 
 * @param manager Address of the manager pointer. Cleared to null on success.
 *                Passing *manager == NULL returns OS_EOK.
 * @return OS_EOK on success, OS_EINVALPARAMS if manager is null, or OS_EBUSY if
 *         pools are still registered or leases are still held.
 */
oserr_t
NetBuffersDestroy(
    _InOut_ NetBufferManager_t** manager);

/**
 * @brief Mark a pool as registering and return its registration request data.
 *
 * Call this before sending register_pool over IPC. Marking the pool early ensures
 * it stays protected if the reply gets lost after the driver already mapped the
 * memory. Request IDs stay fixed per session (1 for TX, 2 for RX) so retries
 * send the exact same parameters. Retired pools cannot be registered again.
 * 
 * @param manager Buffer manager containing the pool.
 * @param direction TX or RX pool to register.
 * @param registrationOut Receives the fixed registration ID for this pool.
 * @param poolOut Receives the pool geometry and shared-memory grant description.
 * @return OS_EOK on success, OS_EINVALPARAMS for bad inputs, or OS_ENOENT if
 *         the manager is closed or the pool was already retired.
 */
oserr_t
NetBuffersBeginRegistration(
    _In_    NetBufferManager_t*           manager,
    _In_    enum ctt_netadapter_direction direction,
    _Out_   uint64_t*                     registrationOut,
    _Out_   struct ctt_netadapter_pool*   poolOut);

/**
 * @brief Record a successful register_pool reply from the driver.
 *
 * Rejects zero pool IDs, duplicate IDs already assigned to the other pool, and
 * replies for a different session. Duplicate confirmations with the same poolId
 * return OS_EOK. If registration fails or times out, the pool stays marked
 * registering until the session closes so shared memory is never freed too soon.
 * 
 * @param manager Buffer manager containing the pool.
 * @param session Session ID and generation from the reply.
 * @param direction Pool direction that was registered.
 * @param poolId Driver-assigned nonzero pool ID.
 * @return OS_EOK on success, OS_EINVALPARAMS for bad inputs, OS_ENOENT if closed
 *         or retired, or OS_EPROTOCOL for conflicting state or pool IDs.
 */
oserr_t
NetBuffersRegistered(
    _In_ NetBufferManager_t*                  manager,
    _In_ const struct ctt_netadapter_session* session,
    _In_ enum ctt_netadapter_direction        direction,
    _In_ uint32_t                             poolId);

/**
 * @brief Mark a registered pool as retired after a successful unregister_pool
 * reply.
 *
 * Only valid once no packets remain with the driver, every completion has been
 * acknowledged and confirmed retired, and no slots are still waiting for late
 * admission replies. Local HELD or READY leases may still exist afterwards, and
 * will keep NetBuffersDestroy from freeing memory until released.
 * 
 * @param manager Buffer manager containing the pool.
 * @param session Session ID and generation from the reply.
 * @param direction Pool direction being unregistered.
 * @return OS_EOK on success, OS_EINVALPARAMS for bad state or inputs, or
 *         OS_EBUSY if packets or unacknowledged completions are still active.
 */
oserr_t
NetBuffersUnregistered(
    _In_ NetBufferManager_t*                  manager,
    _In_ const struct ctt_netadapter_session* session,
    _In_ enum ctt_netadapter_direction        direction);

/**
 * @brief Take a free slot from a registered pool in O(1) time.
 *
 * Clears the entire slot (including headroom and tailroom padding) so old frame
 * bytes never leak to the driver. Refuses to wrap a slot's 64-bit sequence back
 * to zero so stale leases can never match a reused slot.
 * 
 * @param manager Buffer manager containing the pool.
 * @param direction TX or RX pool to allocate from.
 * @param leaseOut Receives the new lease handle on success.
 * @return OS_EOK on success, OS_EBUSY if no slots are free, OS_ENOENT if closed
 *         or unregistered, OS_EOVERFLOW if sequence would wrap, or OS_EINVALPARAMS.
 */
oserr_t
NetBuffersAcquire(
    _In_  NetBufferManager_t*           manager,
    _In_  enum ctt_netadapter_direction direction,
    _Out_ NetBufferLease_t*             leaseOut);

/**
 * @brief Map a HELD or fully settled READY lease to a CPU data pointer.
 * Rejects slots still with the driver or waiting for an admission reply. The
 * returned pointer is a CPU address (not a bus DMA address) and must not be
 * kept after NetBuffersPrepare or NetBuffersRelease.
 * 
 * @param manager Buffer manager containing the slot.
 * @param lease Lease handle for the slot.
 * @param viewOut Receives the CPU view on success.
 * @return OS_EOK on success, OS_EINVALPARAMS for bad inputs or stale lease, or
 *         OS_EBUSY if the slot is with the driver or awaiting admission.
 */
oserr_t
NetBuffersView(
    _In_  NetBufferManager_t*     manager,
    _In_  const NetBufferLease_t* lease,
    _Out_ NetBufferView_t*        viewOut);

/**
 * @brief Return a HELD or settled READY lease back to the pool free list.
 * Double releases, stale leases, slots with the driver, and completed slots
 * still waiting for their admission reply are all rejected.
 * 
 * @param manager Buffer manager containing the slot.
 * @param lease Lease handle to release.
 * @return OS_EOK on success, OS_EINVALPARAMS for bad or stale lease, or
 *         OS_EBUSY if the slot cannot be released yet.
 */
oserr_t
NetBuffersRelease(
    _In_ NetBufferManager_t*     manager,
    _In_ const NetBufferLease_t* lease);

/**
 * @brief Build a driver packet descriptor from a HELD slot and move it to PENDING.
 *
 * Call this before sending the batch over IPC. Once sending starts, the driver
 * may touch the buffer even if the IPC call fails or times out. A slot can only
 * be prepared once per lease: on retry, resend the same descriptor, and if the
 * driver rejects the packet, release and reacquire the slot to get a fresh
 * sequence number.
 *
 * TX length must be between NET_ADAPTER_ETHERNET_HEADER_SIZE and FrameCapacity.
 * RX length must equal FrameCapacity. Enforces per-direction active caps and
 * leaves room inside max_unacked_completions so RX never starves TX.
 * 
 * @param manager Buffer manager containing the slot.
 * @param lease HELD lease to prepare.
 * @param length Frame bytes for TX, or FrameCapacity for RX.
 * @param packetOut Receives the wire descriptor on success.
 * @return OS_EOK on success, OS_EBUSY if driver queues or completion window are
 *         full, OS_ENOENT if closed or unregistered, or OS_EINVALPARAMS.
 */
oserr_t
NetBuffersPrepare(
    _In_  NetBufferManager_t*            manager,
    _In_  const NetBufferLease_t*        lease,
    _In_  uint32_t                       length,
    _Out_ struct ctt_netadapter_packet*  packetOut);

/**
 * @brief Apply the driver's admission reply saying whether it accepted or
 * rejected one submitted packet.
 *
 * Returning OS_EOK means the reply was valid and applied, check admission->status
 * to see whether the driver accepted the packet. Rejected slots return to HELD so
 * the caller can release them. Never fake a rejection after an IPC timeout,
 * because the driver may already have accepted the packet.
 *
 * If the packet's completion already arrived ahead of this reply, *readyOut is
 * set to true so the caller can deliver and release it now.
 * 
 * @param manager Buffer manager containing the slot.
 * @param session Session ID and generation from the event.
 * @param admission Admission record from the driver.
 * @param leaseOut Receives the matching lease on success.
 * @param readyOut Set to true if a completion was already waiting for this reply.
 * @return OS_EOK on success, OS_EEXISTS for a matching duplicate, OS_ENOENT if
 *         closed, OS_EPROTOCOL for invalid or conflicting replies, or OS_EINVALPARAMS.
 */
oserr_t
NetBuffersAdmission(
    _In_  NetBufferManager_t*                    manager,
    _In_  const struct ctt_netadapter_session*   session,
    _In_  const struct ctt_netadapter_admission* admission,
    _Out_ NetBufferLease_t*                      leaseOut,
    _Out_ bool*                                  readyOut);

/**
 * @brief Validate and record one completion (final transfer result) from the
 * driver.
 *
 * Returns OS_EEXISTS if this sequence was already processed so the caller does
 * not deliver the same packet twice. Completions may arrive out of order within
 * max_unacked_completions ahead of ProcessedCompletion. Further ahead returns
 * OS_EBUSY so the caller can drain the missing completions first.
 *
 * If a completion beats its admission reply, it is saved internally with
 * *readyOut set to false until NetBuffersAdmission matches that reply.
 *
 * @param manager Buffer manager containing the slot.
 * @param session Session ID and generation from the event.
 * @param completion Driver completion record.
 * @param leaseOut Receives the matching lease on success.
 * @param readyOut Set to true if the packet can be delivered right away.
 * @return OS_EOK on success, OS_EEXISTS if already processed, OS_EBUSY if
 *         earlier completions are missing, OS_ENOENT if closed, or OS_EPROTOCOL.
 */
oserr_t
NetBuffersComplete(
    _In_  NetBufferManager_t*                     manager,
    _In_  const struct ctt_netadapter_session*    session,
    _In_  const struct ctt_netadapter_completion* completion,
    _Out_ NetBufferLease_t*                       leaseOut,
    _Out_ bool*                                   readyOut);

/**
 * @brief Copy current pool slot counts and completion sequence progress.
 * Safe no-op if either pointer is null.
 *
 * @param manager Buffer manager to inspect.
 * @param statsOut Receives the copied statistics.
 */
void
NetBuffersGetStats(
    _In_  const NetBufferManager_t* manager,
    _Out_ NetBufferStats_t*         statsOut);

/**
 * @brief Record that the driver has retired completions up to sequence.
 *
 * Only acknowledge up to ProcessedCompletion (the highest sequence with no
 * earlier gaps), otherwise the driver could discard missing completions. Call
 * this only after the driver's progress report confirms retirement, never right
 * after sending an ACK. Confirming an older sequence is a harmless no-op.
 * 
 * @param manager Buffer manager containing the completion window.
 * @param session Session ID and generation from the event.
 * @param sequence Highest completion sequence confirmed retired by the driver.
 * @return OS_EOK on success, OS_ENOENT if closed, or OS_EINVALPARAMS if the
 *         session mismatches or sequence exceeds ProcessedCompletion.
 */
oserr_t
NetBuffersConfirmAcknowledged(
    _In_ NetBufferManager_t*                  manager,
    _In_ const struct ctt_netadapter_session* session,
    _In_ uint64_t                             sequence);

/**
 * @brief Retire both pools after a confirmed close reply for this session.
 *
 * A confirmed close means the driver promises it has stopped touching shared
 * memory. Any slots still in PENDING or DRIVER move to READY with status
 * OS_ECANCELLED so callers can release them cleanly. Leases already held by
 * callers (such as kept RX packets) remain valid until released. Never call
 * this after a stop reply alone, since stopping does not release shared pools.
 * 
 * @param manager Buffer manager to close.
 * @param session Session ID and generation from the close reply.
 * @return OS_EOK on success, or OS_EINVALPARAMS if the session does not match.
 */
oserr_t
NetBuffersClosed(
    _In_ NetBufferManager_t*                  manager,
    _In_ const struct ctt_netadapter_session* session);

/**
 * @brief Check an admission record without modifying any manager state.
 * Runs the same checks as NetBuffersAdmission. Call this on every entry of a
 * batch admission event before applying any of them so bad data never leaves an
 * event half applied. Callers must also reject duplicate packet IDs within the
 * same event. Must run on the adapter's single worker loop.
 * 
 * @param manager Buffer manager to validate against.
 * @param session Session ID and generation from the event.
 * @param admission Admission record to check.
 * @return OS_EOK if valid, OS_EEXISTS for a matching repeat, OS_ENOENT if
 *         closed, OS_EPROTOCOL for bad data, or OS_EINVALPARAMS.
 */
oserr_t
NetBuffersValidateAdmission(
    _In_ NetBufferManager_t*                    manager,
    _In_ const struct ctt_netadapter_session*   session,
    _In_ const struct ctt_netadapter_admission* admission);

/**
 * @brief Check a completion record without modifying any manager state.
 * Runs the same checks as NetBuffersComplete. Call this on every entry of a
 * completion event before applying any of them so bad data never leaves an event
 * half applied. Callers must also reject duplicate packet IDs within the same
 * event. Must run on the adapter's single worker loop.
 *
 * @param manager Buffer manager to validate against.
 * @param session Session ID and generation from the event.
 * @param completion Completion record to check.
 * @return OS_EOK if valid, OS_EEXISTS if already processed, OS_EBUSY if out
 *         of window (missing earlier completions), OS_ENOENT if closed,
 *         OS_EPROTOCOL for bad data, or OS_EINVALPARAMS.
 */
oserr_t
NetBuffersValidateCompletion(
    _In_ NetBufferManager_t*                     manager,
    _In_ const struct ctt_netadapter_session*    session,
    _In_ const struct ctt_netadapter_completion* completion);

#endif
