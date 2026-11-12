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

#ifndef __OS_FENCE_PRIVATE_H__
#define __OS_FENCE_PRIVATE_H__

#include <os/fence.h>

/**
 * @brief Completion states used by standalone fences and pool slots.
 *
 * State order: Idle -> Binding -> Pending -> Complete. Before submission, the
 * producer can cancel Binding back to Idle. Local fences can be reset after all
 * other users finish. Pool slots can be initialized again after the last reference
 * is released. Standalone shared fences have no shared reference count and cannot
 * be reset through the fence API.
 *
 * State and Waiters use sequentially consistent atomics to prevent missed wakes
 * when a waiter registers during completion. The producer writes Result before
 * storing Complete. Result is not atomic, so readers must load Complete before
 * reading it. Storage must stay valid until the wake operation finishes.
 */
enum __OSFenceState {
    // No submitted work. Poll and wait report OS_EINVALPARAMS.
    __OSFenceIdle,
    // Reserved by a producer, but work has not been submitted.
    __OSFenceBinding,
    // Work is pending. Poll returns OS_EINPROGRESS and wait may sleep.
    __OSFencePending,
    // Result can be read. Waking waiters may still be in progress or need a retry.
    __OSFenceComplete
};

/**
 * @brief Initializes unused completion storage.
 *
 * The caller must exclude all users before initializing or reusing storage.
 * Sets Idle, zero waiters and an OS_EOK result. Poll cannot read the result until
 * completion. References and mappings are not checked.
 *
 * @param storage Valid writable completion storage.
 */
void
__OSFenceStorageInitialize(
    _Out_ OSFenceStorage_t* storage);

/**
 * @brief Reads the result of a completed fence.
 *
 * Pointers must be valid and storage must not be reset, unmapped or reused during
 * the call. Loading Complete synchronizes with the producer before reading Result.
 * Does not wait or make a syscall. The job result is returned through resultOut.
 *
 * @param storage Completion storage, valid until return.
 * @param resultOut Job result on completion. Unchanged otherwise.
 * @return OS_EOK, OS_EINPROGRESS or OS_EINVALPARAMS for unsubmitted storage.
 */
oserr_t
__OSFenceStoragePoll(
    _In_  OSFenceStorage_t* storage,
    _Out_ oserr_t*          resultOut);

/**
 * @brief Waits while a submitted fence is pending.
 *
 * Pointers and mapping must remain valid until return. Registering a waiter does
 * not add a reference. State is checked again after registration, then by the
 * futex before sleeping. The waiter count is reduced on every registered exit,
 * also after timeout or interruption.
 *
 * @param storage Completion storage, valid until return.
 * @param futexFlags FUTEX_FLAG_PRIVATE for local storage, zero for shared.
 * @param deadline Absolute OS timestamp, or NULL to wait forever. Must stay valid
 *                 and unchanged until return. Every wait uses the same deadline.
 * @param resultOut Valid completion output, unchanged on failure.
 * @return Poll status, OS_EOVERFLOW if the waiter count cannot increase, or a kernel
 *         wait error. A completion observed after a wait takes precedence over
 *         its error. Neither timeouts nor other wait errors cancel the job.
 */
oserr_t
__OSFenceStorageWait(
    _In_  OSFenceStorage_t*    storage,
    _In_  unsigned int         futexFlags,
    _In_  const OSTimestamp_t* deadline,
    _Out_ oserr_t*             resultOut);

/**
 * @brief Sets the result and wakes registered waiters, or retries a failed wake.
 *
 * Storage must be valid and producer calls must be serialized. Result is written
 * before Complete, so consumers may finish before the wake returns. The producer
 * must keep storage valid even after all consumers release their references.
 * A failed wake leaves Complete and Result unchanged.
 *
 * @param storage Submitted storage, with one producer. Serialize signal calls and
 *                protect storage until notification succeeds, including retries.
 * @param futexFlags FUTEX_FLAG_PRIVATE for local storage, zero for shared.
 * @param result Final result. Retries must match the published result.
 * @return OS_EOK, OS_EINVALPARAMS for unsubmitted storage or a changed result,
 *         or notification error. Notification failure leaves the result published.
 */
oserr_t
__OSFenceStorageSignal(
    _In_ OSFenceStorage_t* storage,
    _In_ unsigned int      futexFlags,
    _In_ oserr_t           result);

#endif //!__OS_FENCE_PRIVATE_H__
