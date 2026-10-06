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
 * Packet-pool API and the rules for which component manages each lease.
 * 
 * netd packet pools: the packet data lives in shared memory that is kept for the whole
 * session, while the records of who manages each slot are kept in netd's private memory.
 *
 * One manager belongs to one netadapter session and manages one TX pool and one RX pool.
 * Do not create a new manager for a session that is still open: the new manager would
 * start its registration IDs over from the beginning, clashing with the IDs the driver
 * already knows. All calls, including any use of a lease, must run on the adapter's
 * executor, which runs one task at a time. This module deliberately does not take locks,
 * send RPCs, retry batches or map memory for DMA. The adapter client passes in RPC
 * results it has already validated; checking that the peer really is the expected
 * driver is a separate IPC task that has not been done yet.
 *
 * Slot lifecycle:
 *   FREE -> HELD -> PENDING -> DRIVER -> READY -> FREE
 *                         \-> READY (completion before admission)
 *                    rejection -> HELD
 * FREE slots are in the pool. HELD slots are managed by the caller. PENDING slots have
 * been prepared for the driver and are waiting for its admission reply (the driver's
 * answer saying whether it accepted the packet). DRIVER slots were accepted and are
 * managed by the driver. READY slots have a completion (the driver's final result for
 * the packet) and are managed by the caller again. If the completion arrives before the
 * admission reply, the slot goes straight from PENDING to READY. If the driver rejects
 * the packet, the slot goes back to HELD.
 * A READY slot cannot be released until its admission reply has arrived and been applied.
 * PENDING means it is unknown who manages the slot: even a send that failed may have
 * reached the driver, so the driver may be using the buffer.
 *
 * Received data must be read before the lease for it is released. The adapter's
 * retained-packet API lets a consumer keep a limited number of completed leases
 * until the consumer releases them.
 * Never keep a view after Release or Prepare. The adapter's TX builder API can give
 * exclusive access to the payload outside the executor while the lease stays HELD;
 * code that calls this low-level manager directly must still make sure all access
 * happens one call at a time.
 */
#ifndef __NETD_BUFFERS_H__
#define __NETD_BUFFERS_H__

#include <ddk/utils.h>
#include <ctt_netadapter_service.h>

// Number of bytes in the fixed Ethernet header included in every frame.
// Redefined from adapter.h to avoid inclusion
#define NET_ADAPTER_ETHERNET_HEADER_SIZE 14

// Callers cannot see inside the manager. Its records live in netd's private memory,
// never in shared memory, so the controller driver cannot modify them.
typedef struct NetBufferManager NetBufferManager_t;

/** 
 * @brief Resource limits chosen by the caller. All sizes are in bytes and all counts must be
 * nonzero. FrameCapacity is the largest frame a slot can hold; it includes the Ethernet
 * header but not the frame check sequence (FCS) or any private headers of the driver.
 * MemoryBudget is the most memory the manager may use. The shared memory it requests
 * (rounded up to whole pages), the manager itself, the per-slot records and the table
 * used to track completions are all counted against it. Memory the allocator or kernel
 * uses for its own bookkeeping is not counted. Everything is allocated when the manager
 * is created; nothing is allocated per packet.
 */
typedef struct NetBufferConfig {
    uint32_t TxSlots;
    uint32_t RxSlots;
    uint32_t FrameCapacity;
    uint64_t MemoryBudget;
} NetBufferConfig_t;

/** 
 * @brief A lease is netd's local handle to an acquired slot; it is never sent to the driver.
 * Sequence changes every time the slot is acquired, so a lease left over from an earlier
 * use of the slot cannot release or modify the slot after it has been reused.
 * Do not modify these fields.
 */
typedef struct NetBufferLease {
    struct ctt_netadapter_session Session;
    enum ctt_netadapter_direction Direction;
    uint32_t Slot;
    uint64_t Sequence;
} NetBufferLease_t;

/** 
 * @brief Temporary CPU access to a slot's data, valid only while the caller manages the lease.
 * Length is zero for HELD slots. Completed is set once the driver has reported the final
 * result for the packet; check Status and Detail before reading received bytes, because
 * a failed or cancelled packet contains no valid data. Capacity only covers the usable
 * frame area, never the headroom, tailroom or alignment padding around it.
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
 * @brief Counters used to decide when to stop accepting more work and for diagnostics; these
 * are not hardware statistics. ProcessedCompletion is the highest completion sequence
 * for which every earlier completion has also been processed. AcknowledgedCompletion is
 * the highest sequence the driver has confirmed as acknowledged.
 */
typedef struct NetBufferStats {
    uint32_t TxFree;
    uint32_t RxFree;
    uint32_t TxOutstanding;
    uint32_t RxOutstanding;
    uint32_t UnackedCompletions;
    uint64_t ProcessedCompletion;
    uint64_t AcknowledgedCompletion;
} NetBufferStats_t;

/** 
 * @brief Allocate two zeroed shared memory pools (TX and RX) that a device driver can use,
 * plus the private records. Before anything is allocated, the configuration is checked
 * against the adapter limits, the size calculations are checked for overflow, and the
 * alignment and the total size within MemoryBudget are verified. The driver may still
 * need bounce buffers or scatter-gather mapping: slots that are next to each other in
 * the pool are not necessarily next to each other in physical memory.
 * On failure *managerOut is NULL and anything allocated so far is freed.
 * 
 * @param session The current network adapter session.
 * @param info The network adapter information.
 * @param config The configuration for the buffer pools.
 * @param managerOut Receives the created buffer manager on success.
 * @return OS_EOK on success, or an error code on failure.
 */
oserr_t
NetBuffersCreate(
    _In_  const struct ctt_netadapter_session* session,
    _In_  const struct ctt_netadapter_info*    info,
    _In_  const NetBufferConfig_t*             config,
    _Out_ NetBufferManager_t**                 managerOut);

/** 
 * @brief Free the manager. This is only allowed once both pools have been unregistered from
 * the driver (or were never offered for registration) AND every lease has been released;
 * otherwise OS_EBUSY is returned and nothing is changed. A registration timeout, a
 * driver fault or a stop request does not count as unregistering, because the driver
 * may still be using the memory in those cases. Only a successful unregister reply or a
 * confirmed session close does. On success *manager is set to NULL; passing a NULL
 * *manager is allowed.
 * 
 * @param manager The buffer manager to destroy.
 * @return OS_EOK on success, or an error code on failure.
 */
oserr_t
NetBuffersDestroy(
    _InOut_ NetBufferManager_t** manager);

/**
 * @brief Call this BEFORE sending register_pool. It marks the pool as registering, because once
 * the request is sent the driver may have registered the pool even if no reply arrives.
 * It returns the pool description and the registration request ID. These never change,
 * so a retry sends exactly the same request. The IDs are 1 for TX and 2 for RX within
 * this session. Once a pool has been unregistered it cannot be registered again or
 * replaced within the same manager.
 * 
 * @param manager The buffer manager containing the pool to register.
 * @param direction The direction of the pool (TX or RX).
 * @param registrationOut Receives the registration request ID.
 * @param poolOut Receives the pool description.
 * @return OS_EOK on success, or an error code on failure.
 */
oserr_t
NetBuffersBeginRegistration(
    _In_    NetBufferManager_t*           manager,
    _In_    enum ctt_netadapter_direction direction,
    _Out_   uint64_t*                     registrationOut,
    _Out_   struct ctt_netadapter_pool*   poolOut);

/**
 * @brief Apply a successful register_pool reply. A pool ID of zero, a pool ID already used by
 * the other pool, and replies for an older or different session are rejected. Receiving
 * the same reply more than once is harmless. If registration fails or its outcome is
 * unknown, the pool stays in the registering state until the session close is
 * confirmed: to be safe, we assume the driver may be using the memory until then.
 * 
 * @param manager The buffer manager containing the pool.
 * @param session The current network adapter session.
 * @param direction The direction of the pool (TX or RX).
 * @param poolId The ID of the pool that has been registered.
 * @return OS_EOK on success, or an error code on failure.
 */
oserr_t
NetBuffersRegistered(
    _In_ NetBufferManager_t*                  manager,
    _In_ const struct ctt_netadapter_session* session,
    _In_ enum ctt_netadapter_direction        direction,
    _In_ uint32_t                             poolId);

/**
 * @brief Apply a successful unregister_pool reply. Only call this after all completions have
 * been processed and the driver has confirmed the acknowledgement that covers them.
 * The pool cannot be unregistered while any of its packets are still with the driver.
 * Leases that are HELD or READY locally may still exist after unregistering, but they
 * still prevent Destroy until they are released.
 * 
 * @param manager The buffer manager containing the pool.
 * @param session The current network adapter session.
 * @param direction The direction of the pool (TX or RX).
 * @return OS_EOK on success, or an error code on failure.
 */
oserr_t
NetBuffersUnregistered(
    _In_ NetBufferManager_t*                  manager,
    _In_ const struct ctt_netadapter_session* session,
    _In_ enum ctt_netadapter_direction        direction);

/**
 * @brief Take a free slot from a registered pool in constant time. OS_EBUSY means the pool has
 * no free slots. The whole slot is zeroed on reuse, including the padding before and
 * after the frame. If the slot's sequence number would overflow, OS_EOVERFLOW is
 * returned instead of wrapping around, because a wrapped number could match an old lease.
 * 
 * @param manager The buffer manager containing the pool.
 * @param direction The direction of the pool (TX or RX).
 * @param leaseOut Receives the acquired lease on success.
 * @return OS_EOK on success, OS_EBUSY if no free slots are available, or an error code on failure.
 */
oserr_t
NetBuffersAcquire(
    _In_  NetBufferManager_t*           manager,
    _In_  enum ctt_netadapter_direction direction,
    _Out_ NetBufferLease_t*             leaseOut);

/**
 * @brief Get CPU access to a slot that is HELD, or READY with its admission reply already
 * applied. Slots managed by the driver, or where it is unknown who manages them, are
 * rejected. The pointer is a CPU address, not a DMA address, and must not be kept after
 * the slot is prepared for the driver or released.
 * 
 * @param manager The buffer manager containing the pool.
 * @param lease The lease for the slot to view.
 * @param viewOut Receives the CPU-accessible view of the slot on success.
 * @return OS_EOK on success, or an error code on failure.
 */
oserr_t
NetBuffersView(
    _In_  NetBufferManager_t*     manager,
    _In_  const NetBufferLease_t* lease,
    _Out_ NetBufferView_t*        viewOut);

/**
 * @brief Return a HELD or READY lease to the free list. Releasing the same lease twice, or
 * releasing with a lease from an earlier use of the slot, fails. Slots managed by the
 * driver, slots where that is unknown, and completed slots still waiting for their
 * admission reply return OS_EBUSY.
 * 
 * @param manager The buffer manager containing the pool.
 * @param lease The lease to release.
 * @return OS_EOK on success, OS_EBUSY if the slot cannot be released, or an error code on failure.
 */
oserr_t
NetBuffersRelease(
    _In_ NetBufferManager_t*     manager,
    _In_ const NetBufferLease_t* lease);

/**
 * @brief Build the packet descriptor for a HELD slot and mark the slot PENDING. Do this BEFORE
 * the RPC is sent, because from the moment sending starts the driver may be using the
 * buffer, even if no reply ever arrives. TX length must be at least
 * NET_ADAPTER_ETHERNET_HEADER_SIZE (the Ethernet header size) and no more than
 * FrameCapacity; RX length must equal FrameCapacity. A lease can only
 * be prepared once: if the RPC has to be retried, keep the returned descriptor and send
 * it again. If the driver rejects the packet, release the slot and acquire it again
 * before submitting it again, so it gets a new sequence number.
 * The driver can only keep a limited number of completions that we have not yet
 * acknowledged, so packets with the driver plus completions received but not yet
 * acknowledged may never exceed that number. Each direction also has its own limit on
 * packets with the driver, and RX is kept at least one below the completion limit so a
 * TX can always be sent. OS_EBUSY can therefore mean one of these limits was reached,
 * even when a free local slot exists.
 * 
 * @param manager The buffer manager containing the pool.
 * @param lease The lease for the slot to prepare.
 * @param length The length of the packet.
 * @param packetOut Receives the prepared packet descriptor on success.
 * @return OS_EOK on success, OS_EBUSY if the driver cannot accept more packets, or an error code on failure.
 */
oserr_t
NetBuffersPrepare(
    _In_  NetBufferManager_t*            manager,
    _In_  const NetBufferLease_t*        lease,
    _In_  uint32_t                       length,
    _Out_ struct ctt_netadapter_packet*  packetOut);

/**
 * @brief Apply the driver's admission reply for one packet, telling whether it accepted or
 * rejected the submission. OS_EOK means the reply was applied, not that the packet was
 * accepted: check admission->status for that. A rejected packet goes back to HELD and
 * is managed by the caller again. Never pretend a packet was rejected after a transport
 * timeout: the request may have reached the driver, which may then be using the buffer.
 * Outputs are only filled in on success. readyOut is set when the completion arrived
 * before this reply and was kept waiting for it, so the packet can now be delivered.
 * 
 * @param manager The buffer manager containing the pool.
 * @param session The session associated with the packet.
 * @param admission The driver's admission reply for the packet.
 * @param leaseOut Receives the lease for the packet on success.
 * @param readyOut Set to true if the packet is ready to be delivered.
 * @return OS_EOK on success, or an error code on failure.
 */
oserr_t
NetBuffersAdmission(
    _In_  NetBufferManager_t*                    manager,
    _In_  const struct ctt_netadapter_session*   session,
    _In_  const struct ctt_netadapter_admission* admission,
    _Out_ NetBufferLease_t*                      leaseOut,
    _Out_ bool*                                  readyOut);

/**
 * @brief Check and apply a completion (the driver's final result for a packet), returning the
 * packet's lease and whether it is ready to be delivered. OS_EEXISTS means this
 * completion was already processed, so it must not be delivered again. Completions may
 * arrive out of order, but no more than max_unacked_completions ahead of the oldest one
 * not yet processed; OS_EBUSY means earlier completions are missing and must be
 * received first. An invalid completion never changes who manages the slot.
 * If the completion arrives before the admission reply, it is stored but readyOut is
 * false; the packet is not passed to the network stack until the reply arrives.
 *
 * @param manager The buffer manager containing the pool.
 * @param session The session associated with the packet.
 * @param completion The driver's completion for the packet.
 * @param leaseOut Receives the lease for the packet on success.
 * @param readyOut Set to true if the packet is ready to be delivered.
 * @return OS_EOK on success, OS_EEXISTS if the completion was already processed, OS_EBUSY if earlier completions are missing, or an error code on failure.
 */
oserr_t
NetBuffersComplete(
    _In_  NetBufferManager_t*                     manager,
    _In_  const struct ctt_netadapter_session*    session,
    _In_  const struct ctt_netadapter_completion* completion,
    _Out_ NetBufferLease_t*                       leaseOut,
    _Out_ bool*                                   readyOut);

/**
 * @brief Acknowledgements to the driver cover every completion up to a given sequence, so only
 * acknowledge up to ProcessedCompletion: the highest sequence for which every earlier
 * completion has also been processed. Acknowledging past a gap could make the driver
 * discard a completion we have not seen yet. Call NetBuffersConfirmAcknowledged only
 * after the driver confirms it has freed those completions, never just after sending
 * the request. Confirming a sequence older than one already confirmed is harmless.
 * 
 * @param manager The buffer manager containing the pool.
 * @param session The session associated with the acknowledgements.
 * @param sequence The highest sequence number for which all earlier completions have been processed.
 * @return OS_EOK on success, or an error code on failure.
 */
void
NetBuffersGetStats(
    _In_  const NetBufferManager_t* manager,
    _Out_ NetBufferStats_t*         statsOut);

/**
 * @brief Confirm that the driver has acknowledged all completions up to a given sequence.
 * 
 * @param manager The buffer manager containing the pool.
 * @param session The session associated with the acknowledgements.
 * @param sequence The highest sequence number for which all earlier completions have been processed.
 * @return OS_EOK on success, or an error code on failure.
 */
oserr_t
NetBuffersConfirmAcknowledged(
    _In_ NetBufferManager_t*                  manager,
    _In_ const struct ctt_netadapter_session* session,
    _In_ uint64_t                             sequence);

/**
 * @brief Apply ONLY a successful close reply for this exact session. A successful close is the
 * point where the driver guarantees it no longer uses any of the pool memory. Both pools
 * are marked as unregistered. Packets that were still with the driver, or waiting for
 * an admission reply, become READY with status CANCELLED so their callers can release
 * the leases. Leases already managed by the caller, including received packets, remain
 * valid. No new acquisitions or submissions are allowed afterwards. Do not call this on
 * a stop reply alone: stopping does not guarantee the driver is done with the memory.
 * 
 * @param manager The buffer manager containing the pool.
 * @param session The session associated with the close reply.
 * @return OS_EOK on success, or an error code on failure.
 */
oserr_t
NetBuffersClosed(
    _In_ NetBufferManager_t*                  manager,
    _In_ const struct ctt_netadapter_session* session);

/**
 * @brief Check an admission reply or a completion without changing anything. The functions that
 * apply them run exactly the same checks. Use these to check every entry of an incoming
 * event before applying any of them, so a bad entry cannot leave the event half applied;
 * the caller should also reject events that mention the same packet more than once.
 * These calls must still run on the executor, one at a time with all pool operations.
 * 
 * @param manager The buffer manager containing the pool.
 * @param session The session associated with the admission or completion.
 * @param admission The admission reply to validate.
 * @return OS_EOK on success, or an error code on failure.
 */
oserr_t
NetBuffersValidateAdmission(
    _In_ NetBufferManager_t*                    manager,
    _In_ const struct ctt_netadapter_session*   session,
    _In_ const struct ctt_netadapter_admission* admission);

/**
 * @brief Check a completion without changing anything. The functions that apply 
 * completions run exactly the same checks. Use this to check every entry of an 
 * incoming completion before applying any of them, so a bad entry cannot 
 * leave the completion half applied; the caller should also reject completions 
 * that mention the same packet more than once.
 * These calls must still run on the executor, one at a time with all pool operations.
 *
 * @param manager The buffer manager containing the pool.
 * @param session The session associated with the completion.
 * @param completion The completion to validate.
 * @return OS_EOK on success, or an error code on failure.
 */
oserr_t
NetBuffersValidateCompletion(
    _In_ NetBufferManager_t*                     manager,
    _In_ const struct ctt_netadapter_session*    session,
    _In_ const struct ctt_netadapter_completion* completion);

#endif
