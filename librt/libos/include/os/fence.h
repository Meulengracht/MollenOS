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
 */

#ifndef __OS_FENCE_H__
#define __OS_FENCE_H__

#include <os/types/time.h>
#include <stdatomic.h>

_CODE_BEGIN

/**
 * @brief Completion status for one job, shared by one or more consumers.
 *
 * The producer starts the job and reports its result. Consumers poll or wait for
 * that result. The fence does not start or stop hardware, schedule work or allow
 * buffer access. CPU and DMA access still require their normal begin/end calls.
 *
 * Producer sequence:
 * - Create a local fence or attach initialized shared storage.
 * - Reserve the idle fence with OSFencePrepare.
 * - If submission fails, call OSFenceCancel. Otherwise call OSFenceSubmit before
 *   work can complete, then allow consumers to poll or wait.
 * - End the work and required CPU/DMA access before calling OSFenceSignal.
 *   Keep the producer reference until signal succeeds, including retries.
 *
 * Each user must hold a reference until its calls finish. Use OSFenceAcquire
 * before giving another user the pointer, and OSFenceRelease when that user is
 * done. Copying the pointer alone does not add a reference. Producer calls must
 * be serialized, also across processes. Consumers may poll or wait concurrently.
 *
 * Poll and wait return OS_EOK when resultOut contains the job result. The job
 * may still have failed. A wait timeout does not cancel it. Calls documented as
 * making no syscalls may still write errors or enabled traces to the system log.
 */
DECL_STRUCT(OSFence);

/**
 * @brief Shared completion state without process-local pointers.
 *
 * All processes must use the same ABI, including structure layout, atomic types
 * and error values. Storage must be naturally aligned in committed, cached,
 * read/write shared memory (SHM). Device registers and uncached memory cannot
 * be used. Initialize once before sending the SHM ID and byte offset to peers.
 * Each peer maps at least sizeof(OSFenceStorage_t) bytes at that offset
 * and calls OSFenceAttachShared with its local address. Notify consumers after
 * submission. Attaching alone does not make the fence ready to poll or wait.
 *
 * All members are private to libos. Waiters counts registered wait calls. State
 * controls when the job result in Result can be read. SHM attachments are counted
 * separately. All processes must follow the protocol, as any writable mapping
 * allows the shared state to be changed.
 */
typedef struct OSFenceStorage {
    atomic_uint Waiters;
    atomic_int  State;
    oserr_t     Result;
} OSFenceStorage_t;

/**
 * @brief Initializes unused shared storage before sharing it with other processes.
 *
 * No process may use the storage during initialization. The fence starts idle
 * and must be submitted before polling or waiting. Does not allocate memory,
 * map SHM or validate the mapping.
 *
 * @param storage Naturally aligned, writable shared storage. No concurrent users.
 * @return OS_EOK or OS_EINVALPARAMS for NULL storage.
 */
CRTDECL(oserr_t,
OSFenceInitializeShared(
    _Out_ OSFenceStorage_t* storage));

/**
 * @brief Attaches a local handle to initialized shared completion storage.
 *
 * Each process must keep its SHM mapping valid until all local references and
 * calls finish. Keep an attachment until peers confirm that they have attached.
 * Sending the SHM ID adds no reference. Shared fences cannot be reset through
 * this API. Do not reinitialize storage while another process can still use it.
 * Process exit does not signal the fence. Callers must handle peer failure.
 *
 * Allocates a handle with one local reference and leaves the shared state
 * unchanged. Each handle has a separate reference count. Adding a fence reference
 * does not keep the SHM mapping valid.
 *
 * @param storage This process's mapped address of initialized shared storage.
 * @param fenceOut *fenceOut must be NULL on entry. Unchanged on failure.
 * @return OS_EOK, OS_EBUSY if the output is occupied, OS_EINVALPARAMS for NULL
 *         storage or output, or OS_EOOM if allocation fails.
 */
CRTDECL(oserr_t,
OSFenceAttachShared(
    _In_    OSFenceStorage_t* storage,
    _InOut_ OSFence_t**       fenceOut));

/**
 * @brief Creates an idle completion fence local to this process.
 *
 * Allocates the handle and completion storage together, with one reference.
 * The pointer is valid only in this process. Before reuse, wait for completion,
 * release every other reference and call OSFenceReset.
 *
 * @param fenceOut *fenceOut must be NULL on entry. Unchanged on failure.
 * @return OS_EOK, OS_EBUSY if the output is occupied, OS_EINVALPARAMS for NULL
 *         output, or OS_EOOM if allocation fails.
 */
CRTDECL(oserr_t,
OSFenceCreate(
    _InOut_ OSFence_t** fenceOut));

/**
 * @brief Adds a local reference to a fence.
 *
 * The caller must hold an existing reference until this call returns. On success,
 * the pointer can be given to another user, who must release the added reference
 * when done. Failure leaves the count unchanged. Does not validate a released
 * pointer or create an SHM attachment.
 *
 * @param fence Fence with an existing reference held until return.
 * @return OS_EOK, OS_EINVALPARAMS for NULL, or OS_EOVERFLOW if the local reference
 *         count cannot be increased.
 */
CRTDECL(oserr_t,
OSFenceAcquire(
    _In_ OSFence_t* fence));

/**
 * @brief Releases a reference and clears its slot.
 *
 * All calls using this reference must have finished. Each successful create,
 * attach or acquire needs exactly one release. The last release frees the handle
 * and its local storage. Shared storage stays mapped and unchanged. Releasing a
 * reference does not cancel or signal work.
 *
 * @param fence Reference slot. NULL or empty slots are a no-op. The producer
 *              must keep its reference until notification succeeds.
 */
CRTDECL(void,
OSFenceRelease(
    _InOut_ OSFence_t** fence));

/**
 * @brief Resets an idle or completed local fence for reuse.
 *
 * Exactly one reference must remain, with no concurrent calls using it. The
 * reference count cannot detect calls that share the same pointer. The previous
 * result becomes unavailable until the next job completes. Shared fences cannot
 * be reset because local counts do not track users in other processes.
 *
 * @param fence Only remaining reference. Acquire, poll and wait must also be
 *              excluded while resetting.
 * @return OS_EOK, OS_EBUSY for a prepared or pending fence or extra references,
 *         OS_ENOTSUPPORTED for shared storage, or OS_EINVALPARAMS for NULL.
 */
CRTDECL(oserr_t,
OSFenceReset(
    _In_ OSFence_t* fence));

/**
 * @brief Polls a submitted fence without allocation or a syscall.
 *
 * On OS_EOK, resultOut contains the job result and the producer's CPU writes
 * before signal are visible to this caller. Device cache synchronization and
 * CPU access checks must still be done separately.
 *
 * @param fence Live reference held throughout the call.
 * @param resultOut Job result on completion. Unchanged otherwise.
 * @return OS_EOK for completion, OS_EINPROGRESS while pending, OS_EINVALPARAMS
 *         for an idle/unsubmitted fence or bad arguments.
 */
CRTDECL(oserr_t,
OSFencePoll(
    _In_  OSFence_t*  fence,
    _Out_ oserr_t*    resultOut));

/**
 * @brief Waits for completion. Uses a futex while the fence is pending.
 *
 * Completed fences return immediately. Multiple callers can wait on the same
 * fence. If completion is observed after a kernel wait, the result is returned
 * even if the wait reported a timeout or another error.
 *
 * @param fence Reference held until return. The fence must not be reset meanwhile.
 * @param deadline Absolute OS timestamp, or NULL to wait forever. Keep the
 *                 timestamp valid and unchanged until return.
 * @param resultOut Job result on completion. Unchanged on wait failure.
 * @return OSFencePoll results, OS_EOVERFLOW if the registered waiter count cannot
 *         be increased, or a kernel wait error such as timeout/interruption.
 *         A timeout does not cancel the job or release the caller's reference.
 */
CRTDECL(oserr_t,
OSFenceWait(
    _In_  OSFence_t*           fence,
    _In_  const OSTimestamp_t* deadline,
    _Out_ oserr_t*             resultOut));

/**
 * @brief Reserves an idle fence before submitting work.
 *
 * Only one producer can prepare the fence. Poll and wait return OS_EINVALPARAMS
 * until it is submitted. Does not add a reference or start device work.
 *
 * @param fence Live reference. On success, the producer must cancel or submit.
 *              Serialize its cancel, submit and signal calls. Keep a reference
 *              until signal succeeds, including retries after notification failure.
 * @return OS_EOK, OS_EBUSY if not idle, or OS_EINVALPARAMS for NULL.
 */
CRTDECL(oserr_t,
OSFencePrepare(
    _In_ OSFence_t* fence));

/**
 * @brief Returns a prepared fence to idle after a rejected submission.
 *
 * The caller must have prepared the fence without submitting work. Calls must
 * be serialized with submit and signal. These preconditions are not checked.
 * Does not stop work, wake consumers or release a reference.
 *
 * @param fence Successfully prepared fence, not yet submitted. Only the producer
 *              that prepared the fence may cancel it.
 */
CRTDECL(void,
OSFenceCancel(
    _In_ OSFence_t* fence));

/**
 * @brief Makes a prepared fence pending before work starts.
 *
 * Prepare must have succeeded, and calls must be serialized with cancel and
 * signal. These preconditions are not checked. Does not start hardware. After
 * submission, a job that fails to start must be signaled with an error result.
 * Cancel can only be used before submission.
 *
 * @param fence Successfully prepared fence. Only its producer may submit it.
 *              Notify consumers of submission before they poll or wait.
 */
CRTDECL(void,
OSFenceSubmit(
    _In_ OSFence_t* fence));

/**
 * @brief Sets a submitted job's result and wakes all waiters.
 *
 * All work covered by the fence must have ended, including required CPU/DMA
 * access cleanup. Only one producer may write the result. Consumers can see
 * completion before this call finishes waking waiters. If notification fails,
 * the result stays visible. Keep the storage and producer reference valid until
 * notification succeeds, including retries with the same result.
 *
 * @param fence Submitted fence. Signal calls must be serialized.
 * @param result Completion status, including a job failure or cancellation.
 *               Prior CPU writes become visible to consumers observing completion.
 *               Repeating signal with the same result retries notification.
 *               The result cannot be changed after completion.
 * @return OS_EOK, OS_EINVALPARAMS for NULL, an unsubmitted fence or changed result,
 *         or a notification error. Notification failure leaves the result published.
 */
CRTDECL(oserr_t,
OSFenceSignal(
    _In_ OSFence_t* fence,
    _In_ oserr_t    result));

_CODE_END
#endif //!__OS_FENCE_H__
