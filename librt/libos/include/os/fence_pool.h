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
 */

#ifndef __OS_FENCE_POOL_H__
#define __OS_FENCE_POOL_H__

#include <os/types/time.h>

_CODE_BEGIN

// Opaque forward declaration of internal data
struct __OSFencePoolSlot;

/**
 * @brief Pool, slot and generation identifying one completion.
 *
 * A pool contains a fixed number of reusable fences in shared memory. Pool
 * identifies the pool, Slot is its index and Generation changes each time the
 * slot is reused. Generation zero marks an empty reference. Initialize empty
 * references to zero. These values are used with the pool API. The standalone
 * API in os/fence.h uses allocated OSFence handles with separate reference counts.
 *
 * The slot counts references across processes. Before giving another user a
 * copy, call OSFencePoolAcquire while holding an existing reference. A reference
 * can also be transferred if the sender stops using its copy. Copying or sending
 * the fields alone does not add a reference. Each counted reference must be
 * released exactly once. Generation checks detect old values, but the caller
 * must still prevent concurrent release while using a reference.
 *
 * Pool IDs and bindings must stay valid until all users finish. Do not reuse an
 * ID while references to the previous pool can still arrive. Calls documented as
 * making no syscalls may still write errors or enabled traces to the system log.
 */
typedef struct OSFenceReference {
    // Pool ID. Also used as the index into the buffer access API's pools array.
    unsigned int Pool;
    // Zero-based slot index, smaller than the matching pool's Capacity.
    unsigned int Slot;
    // Nonzero generation assigned by TryTake. Unchanged until release.
    unsigned int Generation;
} OSFenceReference_t;

/**
 * @brief Local pool binding with addresses of the shared free bits and slots.
 *
 * Storage must be committed, cached, read/write shared memory before initializing
 * or binding. All processes must use the same structure layouts and atomic types.
 * Initialize once before sharing, then call OSFencePoolBind in each process with
 * its local mapped address. Binding does not map memory or add an SHM reference.
 * This structure contains local pointers and must not be sent to other processes.
 *
 * Only OSFencePoolBind may set these members. Keep the mapping and binding valid
 * until all local calls and references finish. All processes must finish before
 * unmapping or reinitializing storage. Process exit does not signal pending work
 * or release references. Callers must handle peer failure and follow the pool
 * protocol, as every writable mapping allows the shared state to be changed.
 */
typedef struct OSFencePool {
    // ID and slot count from the shared header. Fixed after initialization.
    unsigned int              ID;
    unsigned int              Capacity;
    // Each set bit identifies a slot available for TryTake.
    _Atomic(unsigned int)*    Available;
    // Local address of the shared slot array. Private to libos.
    struct __OSFencePoolSlot* Slots;
} OSFencePool_t;

/**
 * @brief Calculates the shared storage size, including free bits and slots.
 *
 * Use this size when allocating or mapping storage. Does not access memory.
 * The pool size is fixed while in use. Storage must be aligned to
 * _Alignof(max_align_t), and all processes must use the same ABI.
 *
 * @param capacity Nonzero slot count.
 * @return Required bytes, or zero for an invalid size or overflow.
 */
CRTDECL(size_t,
OSFencePoolSize(
    _In_ unsigned int capacity));

/**
 * @brief Initializes unused pool storage before sharing it.
 *
 * No process may use the storage during initialization. Writes the header and
 * makes all slots available with zero references. Share the SHM ID and offset
 * after initialization finishes. Reinitializing a pool discards its references
 * and results, so all previous users must have finished first.
 * Does not allocate or map memory.
 *
 * @param storage Writable shared range aligned to _Alignof(max_align_t).
 * @param size Available bytes, at least OSFencePoolSize(capacity).
 * @param poolID Agreed index of this pool in each process's binding array.
 * @param capacity Nonzero slot count. Pool size is fixed until teardown.
 * @return OS_EOK, or OS_EINVALPARAMS for NULL/misaligned storage, zero or oversized
 *         capacity, or insufficient bytes.
 */
CRTDECL(oserr_t,
OSFencePoolInitialize(
    _Out_ void*        storage,
    _In_  size_t       size,
    _In_  unsigned int poolID,
    _In_  unsigned int capacity));

/**
 * @brief Binds initialized pool storage at the local mapped address.
 *
 * Initialization must be complete and the header must stay unchanged while the
 * pool is in use. Checks the version and size before setting local pointers.
 * Does not allocate memory, reinitialize slots or validate existing references.
 *
 * @param storage Shared storage. The layout must stay valid and unchanged.
 * @param size Mapped bytes available from storage, covering the whole recorded pool.
 * @param poolOut Local output, separate from shared storage. Unchanged on error.
 *                Must not overwrite a binding still in use.
 * @return OS_EOK or OS_EINVALPARAMS for NULL arguments or invalid size, version
 *         or alignment.
 */
CRTDECL(oserr_t,
OSFencePoolBind(
    _In_  void*          storage,
    _In_  size_t         size,
    _Out_ OSFencePool_t* poolOut));

/**
 * @brief Takes an unused slot with one unsubmitted producer reference.
 *
 * On success, submit before allowing consumers to poll or wait. Signal after
 * work ends and release the producer reference after notification succeeds,
 * including any retries. If work is rejected before submission, release the
 * unsubmitted reference to return the slot. Add consumer references with
 * OSFencePoolAcquire. Does not allocate, make syscalls or wait.
 * Slots that reach the last generation are removed from use instead of wrapping.
 *
 * @param pool Live binding. The bounded free-bit search does not wait.
 * @param referenceOut Empty reference slot, unchanged on failure.
 * @return OS_EOK, OS_EBUSY for an occupied output or if the scan finds no slot,
 *         or OS_EINVALPARAMS for NULL arguments. Concurrent calls may take the
 *         available slots before this scan reaches them.
 */
CRTDECL(oserr_t,
OSFencePoolTryTake(
    _In_    const OSFencePool_t* pool,
    _InOut_ OSFenceReference_t*  referenceOut));

/**
 * @brief Adds a shared reference to a completion.
 *
 * The caller must hold an existing reference until return. Adds one to the
 * shared count. On success, copy the fields to the new user's reference slot.
 * Failure leaves the count unchanged. Unsubmitted references can be copied this
 * way, but consumers must be notified of submission before polling or waiting.
 *
 * @param pool Matching live pool binding.
 * @param reference Existing reference. Must not be released during the call.
 * @return OS_EOK, OS_ENOENT for an old generation or zero reference count,
 *         OS_EOVERFLOW if the count cannot be increased, or OS_EINVALPARAMS for NULL,
 *         an empty reference, a different pool ID, or an out-of-range slot.
 */
CRTDECL(oserr_t,
OSFencePoolAcquire(
    _In_ const OSFencePool_t*      pool,
    _In_ const OSFenceReference_t* reference));

/**
 * @brief Releases one reference and clears it on success.
 *
 * All calls using this reference must have returned. The last release makes a
 * completed or unsubmitted slot available for reuse. The producer must keep its
 * reference until signal and notification finish, even when consumers can already
 * see the result. Does not signal work, unmap SHM or change other references.
 *
 * @param pool Matching binding. NULL is allowed for a NULL or empty reference.
 * @param reference Reference slot. NULL or empty slots are a no-op.
 * @return OS_EOK, OS_EINVALPARAMS for a mismatched binding or reference,
 *         OS_ENOENT for an old reference, or OS_EBUSY for the last pending
 *         reference. Errors leave the reference unchanged.
 */
CRTDECL(oserr_t,
OSFencePoolRelease(
    _In_    const OSFencePool_t* pool,
    _InOut_ OSFenceReference_t*  reference));

/**
 * @brief Makes a taken completion pending before consumers poll or wait.
 *
 * TryTake must have succeeded. The producer must serialize submit and signal
 * calls. This changes the state without starting work. After submission, even
 * rejected or cancelled jobs must be signaled with a result before the last
 * reference can be released.
 *
 * @param pool Matching binding.
 * @param reference Unsubmitted producer reference.
 * @return OS_EOK, OS_EBUSY if no longer taken and unsubmitted, OS_EINVALPARAMS
 *         for an invalid binding or reference, or OS_ENOENT for an old reference.
 */
CRTDECL(oserr_t,
OSFencePoolSubmit(
    _In_ const OSFencePool_t*      pool,
    _In_ const OSFenceReference_t* reference));

/**
 * @brief Polls a submitted completion without allocation or a syscall.
 *
 * On OS_EOK, resultOut contains the job result and the producer's CPU writes
 * before signal are visible to this caller. Device cache synchronization and
 * CPU access to a DMA buffer must still be handled separately.
 *
 * @param pool Matching binding.
 * @param reference Live reference held throughout this call.
 * @param resultOut Job result on completion. Unchanged otherwise.
 * @return OS_EOK when complete, OS_EINPROGRESS while pending, OS_EINVALPARAMS for
 *         invalid arguments or unsubmitted state, or OS_ENOENT for an old reference.
 */
CRTDECL(oserr_t,
OSFencePoolPoll(
    _In_  const OSFencePool_t*      pool,
    _In_  const OSFenceReference_t* reference,
    _Out_ oserr_t*                  resultOut));

/**
 * @brief Waits for completion using a shared futex.
 *
 * The caller must hold a reference until return. The waiter count alone does not
 * keep the slot valid. Completed fences return immediately. Multiple processes
 * can wait on the same slot, also when mapped at different local addresses.
 * Does not allocate memory in libos.
 *
 * @param pool Matching binding.
 * @param reference Reference held until return.
 * @param deadline Absolute OS timestamp, or NULL to wait forever.
 *                 Keep the timestamp valid and unchanged until return.
 * @param resultOut Job result on completion. Unchanged on failure.
 * @return Poll status, OS_EOVERFLOW for too many registered waiters, or a kernel
 *         wait error. Completion observed after waking takes precedence over a
 *         simultaneous wait error. Errors neither cancel work nor release a reference.
 */
CRTDECL(oserr_t,
OSFencePoolWait(
    _In_  const OSFencePool_t*      pool,
    _In_  const OSFenceReference_t* reference,
    _In_  const OSTimestamp_t*      deadline,
    _Out_ oserr_t*                  resultOut));

/**
 * @brief Sets the result and wakes waiters.
 *
 * All work and required access cleanup must have ended. Only one producer may
 * signal the completion. Consumers can read the result and release their
 * references before notification returns. Keep the producer reference and pool
 * mapping valid until this call succeeds. If notification fails, retry with the
 * same result. The producer reference must be released separately after success.
 *
 * @param pool Matching binding.
 * @param reference Submitted producer reference. Serialize signal calls.
 * @param result Job result. Repeating signal with the same result retries
 *               notification. The result cannot be changed after completion.
 * @return OS_EOK, OS_EINVALPARAMS for an invalid reference, unsubmitted state or
 *         changed result on retry, OS_ENOENT for an old reference, or a kernel
 *         notification error. Notification failure leaves completion published.
 */
CRTDECL(oserr_t,
OSFencePoolSignal(
    _In_ const OSFencePool_t*      pool,
    _In_ const OSFenceReference_t* reference,
    _In_ oserr_t                   result));

_CODE_END
#endif //!__OS_FENCE_POOL_H__
