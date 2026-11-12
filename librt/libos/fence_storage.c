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

//#define __TRACE

#include "fence_private.h"
#include <ddk/utils.h>
#include <time.h>
#include <internal/_tls.h>
#include <os/futex.h>
#include <limits.h>

/**
 * @file
 * @brief Completion state and waits for standalone fences and pool slots.
 *
 * Callers must keep storage valid. These helpers do not allocate memory, manage
 * references or validate mappings. Poll reads mapped storage. Wait and signal
 * call the kernel when sleeping or waking registered waiters. Errors and enabled
 * traces may also write to the system log. Once Complete is set, the result stays
 * visible even if waking waiters fails.
 */
void
__OSFenceStorageInitialize(
    _Out_ OSFenceStorage_t* storage)
{
    atomic_init(&storage->Waiters, 0);
    atomic_init(&storage->State, __OSFenceIdle);
    storage->Result = OS_EOK;
}

oserr_t
__OSFenceStoragePoll(
    _In_  OSFenceStorage_t* storage,
    _Out_ oserr_t*          resultOut)
{
    int state;

    // The producer writes Result before setting Complete. Load State first
    // so the non-atomic Result read follows that write.
    state = atomic_load(&storage->State);
    if (state == __OSFencePending) {
        return OS_EINPROGRESS;
    }

    if (state != __OSFenceComplete) {
        return OS_EINVALPARAMS;
    }

    *resultOut = storage->Result;
    return OS_EOK;
}

oserr_t
__OSFenceStorageWait(
    _In_  OSFenceStorage_t*    storage,
    _In_  unsigned int         futexFlags,
    _In_  const OSTimestamp_t* deadline,
    _Out_ oserr_t*             resultOut)
{
    OSAsyncContext_t*   async;
    OSFutexParameters_t parameters = { 0 };
    unsigned int        waiters;
    oserr_t             status;
    oserr_t             waitStatus = OS_EOK;

    status = __OSFenceStoragePoll(storage, resultOut);
    if (status != OS_EINPROGRESS) {
        return status;
    }

    // Register before entering the kernel. Counting only sleeping threads could
    // let signal skip a wake while this thread is preparing to wait.
    waiters = atomic_load(&storage->Waiters);
    do {
        if (waiters == UINT_MAX) {
            ERROR("__OSFenceStorageWait: storage %p waiter count overflow", storage);
            return OS_EOVERFLOW;
        }
    } while (!atomic_compare_exchange_weak(&storage->Waiters, &waiters, waiters + 1));

    async = __tls_current()->async_context;
    parameters.Futex0 = &storage->State;
    parameters.Expected0 = __OSFencePending;
    parameters.Flags = FUTEX_FLAG_WAIT | futexFlags;
    parameters.Deadline = (OSTimestamp_t*)deadline;
    
    for (;;) {
        // Registration must precede this check so signal sees the waiter if we
        // have not seen Complete. The futex checks again under its queue lock
        // in case completion occurs between this check and the kernel wait.
        status = __OSFenceStoragePoll(storage, resultOut);
        if (status != OS_EINPROGRESS) {
            break;
        }

        // Completion must be checked before returning a wait error. The producer
        // may have finished while the kernel was returning the error.
        if (waitStatus != OS_EOK) {
            if (waitStatus == OS_ETIMEOUT || waitStatus == OS_EINTERRUPTED) {
                TRACE("__OSFenceStorageWait: storage %p wait ended (%u)", storage, waitStatus);
            } else {
                ERROR("__OSFenceStorageWait: storage %p wait failed (%u)", storage, waitStatus);
            }
            status = waitStatus;
            break;
        }

        if (async != NULL) {
            // Reset the thread's async context before another wait. Each attempt
            // uses the original absolute deadline.
            OSAsyncContextInitialize(async);
        }

        waitStatus = OSFutex(&parameters, async);
    }

    // Remove this waiter on every exit, also after timeout or interruption.
    // The caller must still hold its reference while the count is updated.
    atomic_fetch_sub(&storage->Waiters, 1);
    return status;
}

static oserr_t
__OSFenceStorageWake(
    _In_ OSFenceStorage_t* storage,
    _In_ unsigned int      futexFlags)
{
    OSFutexParameters_t parameters = { 0 };
    oserr_t             status;

    // No syscall is needed with zero waiters. State and Waiters are sequentially
    // consistent, so a new waiter will see Complete on its next check.
    if (atomic_load(&storage->Waiters) == 0) {
        return OS_EOK;
    }

    parameters.Futex0 = &storage->State;
    parameters.Flags = FUTEX_FLAG_WAKE | futexFlags;
    parameters.Expected0 = INT_MAX;

    status = OSFutex(&parameters, NULL);
    if (status == OS_ENOENT) {
        // Registered waiters may not have entered the kernel yet or may have
        // timed out. If none are queued, there is nothing left to wake.
        return OS_EOK;
    }

    if (status != OS_EOK) {
        ERROR("__OSFenceStorageWake: storage %p wake failed (%u); completion remains published",
            storage, status);
    }
    return status;
}

oserr_t
__OSFenceStorageSignal(
    _In_ OSFenceStorage_t* storage,
    _In_ unsigned int      futexFlags,
    _In_ oserr_t           result)
{
    int state;

    state = atomic_load(&storage->State);
    if (state == __OSFenceComplete) {
        // Only the wake can be retried. Consumers may already be reading Result,
        // so it must not change after Complete.
        if (storage->Result != result) {
            WARNING("__OSFenceStorageSignal: storage %p retry result %u differs from published result %u",
                storage, result, storage->Result);
            return OS_EINVALPARAMS;
        }
        return __OSFenceStorageWake(storage, futexFlags);
    }

    if (state != __OSFencePending) {
        WARNING("__OSFenceStorageSignal: storage %p is not submitted (state %d)", storage, state);
        return OS_EINVALPARAMS;
    }

    // Producer calls must be serialized. Set the result before waking waiters.
    // If wake fails, Complete and Result must stay unchanged for readers.
    storage->Result = result;
    atomic_store(&storage->State, __OSFenceComplete);
    
    TRACE("__OSFenceStorageSignal: storage %p completed with result %u", storage, result);
    return __OSFenceStorageWake(storage, futexFlags);
}
