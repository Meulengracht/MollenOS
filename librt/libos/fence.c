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
#include <os/futex.h>
#include <limits.h>
#include <stdlib.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Shared fences require lock-free integer atomics");

/**
 * @brief Local fence handle with a reference count and completion storage address.
 *
 * Storage points to Local or to shared storage mapped by the caller. References
 * counts uses of this handle only. Other attached handles have separate counts,
 * so this count cannot determine when shared storage can be reset or unmapped.
 *
 * FutexFlags selects private or shared futex waits. Local is initialized only
 * for local fences. The handle must stay valid until the producer finishes
 * notifying waiters. See fence_private.h for state and memory ordering rules.
 */
typedef struct OSFence {
    atomic_uint       References;
    OSFenceStorage_t* Storage;
    unsigned int      FutexFlags;
    OSFenceStorage_t  Local;
} OSFence_t;

oserr_t
OSFenceInitializeShared(
    _Out_ OSFenceStorage_t* storage)
{
    if (storage == NULL) {
        WARNING("OSFenceInitializeShared: missing storage");
        return OS_EINVALPARAMS;
    }

    __OSFenceStorageInitialize(storage);
    return OS_EOK;
}

static oserr_t
__FenceCreate(
    _In_    OSFenceStorage_t* storage,
    _InOut_ OSFence_t**       fenceOut)
{
    OSFence_t* fence;

    if (fenceOut == NULL) {
        WARNING("__FenceCreate: missing fence output");
        return OS_EINVALPARAMS;
    }

    if (*fenceOut != NULL) {
        TRACE("__FenceCreate: output already contains fence %p", *fenceOut);
        return OS_EBUSY;
    }

    fence = malloc(sizeof(OSFence_t));
    if (fence == NULL) {
        ERROR("__FenceCreate: failed to allocate fence handle");
        return OS_EOOM;
    }

    atomic_init(&fence->References, 1);
    // Storage == &Local identifies local fences when resetting. Shared storage
    // may already be in use by another process and must not be initialized here.
    if (storage == NULL) {
        __OSFenceStorageInitialize(&fence->Local);
        fence->Storage = &fence->Local;
        fence->FutexFlags = FUTEX_FLAG_PRIVATE;
    } else {
        fence->Storage = storage;
        fence->FutexFlags = 0;
    }

    *fenceOut = fence;
    TRACE("__FenceCreate: fence %p, storage %p, flags 0x%x", fence, fence->Storage, fence->FutexFlags);
    return OS_EOK;
}

oserr_t
OSFenceCreate(
    _InOut_ OSFence_t** fenceOut)
{
    return __FenceCreate(NULL, fenceOut);
}

oserr_t
OSFenceAttachShared(
    _In_    OSFenceStorage_t* storage,
    _InOut_ OSFence_t**       fenceOut)
{
    if (storage == NULL) {
        WARNING("OSFenceAttachShared: missing storage");
        return OS_EINVALPARAMS;
    }
    return __FenceCreate(storage, fenceOut);
}

oserr_t
OSFenceAcquire(
    _In_ OSFence_t* fence)
{
    unsigned int references;

    if (fence == NULL) {
        WARNING("OSFenceAcquire: missing fence");
        return OS_EINVALPARAMS;
    }

    // The caller's reference prevents the handle from being freed. State orders
    // access to the result, so the reference count can use relaxed atomics.
    references = atomic_load_explicit(&fence->References, memory_order_relaxed);
    do {
        if (references == UINT_MAX) {
            ERROR("OSFenceAcquire: fence %p reference count overflow", fence);
            return OS_EOVERFLOW;
        }
    } while (!atomic_compare_exchange_weak_explicit(&fence->References, &references,
        references + 1, memory_order_relaxed, memory_order_relaxed));
    return OS_EOK;
}

void
OSFenceRelease(
    _InOut_ OSFence_t** fence)
{
    OSFence_t* released;

    if (fence == NULL || *fence == NULL) {
        return;
    }

    released = *fence;
    *fence = NULL;

    // The last decrement must follow accesses through earlier references before
    // freeing the handle. Shared storage is mapped separately and stays mapped.
    if (atomic_fetch_sub_explicit(&released->References, 1, memory_order_acq_rel) == 1) {
        TRACE("OSFenceRelease: freeing fence %p", released);
        free(released);
    }
}

oserr_t
OSFenceReset(
    _In_ OSFence_t* fence)
{
    int state;

    if (fence == NULL) {
        WARNING("OSFenceReset: missing fence");
        return OS_EINVALPARAMS;
    }

    // The local count does not include other processes. Shared storage cannot
    // be reset because another process may still be using it.
    if (fence->Storage != &fence->Local) {
        TRACE("OSFenceReset: fence %p uses shared storage", fence);
        return OS_ENOTSUPPORTED;
    }

    // The caller must exclude concurrent calls on its remaining reference.
    // The producer keeps a reference until waiter notification finishes.
    if (atomic_load_explicit(&fence->References, memory_order_acquire) != 1) {
        TRACE("OSFenceReset: fence %p has additional references", fence);
        return OS_EBUSY;
    }

    state = atomic_load(&fence->Storage->State);
    if (state == __OSFencePending || state == __OSFenceBinding) {
        TRACE("OSFenceReset: fence %p is active (state %d)", fence, state);
        return OS_EBUSY;
    }

    // Poll cannot read Result until Complete, so it can be left here. The next
    // signal writes a new result before setting Complete.
    atomic_store(&fence->Storage->State, __OSFenceIdle);
    return OS_EOK;
}

oserr_t
OSFencePoll(
    _In_  OSFence_t* fence,
    _Out_ oserr_t*   resultOut)
{
    if (fence == NULL || resultOut == NULL) {
        TRACE("OSFencePoll: missing fence or result output");
        return OS_EINVALPARAMS;
    }
    return __OSFenceStoragePoll(fence->Storage, resultOut);
}

oserr_t
OSFenceWait(
    _In_  OSFence_t*           fence,
    _In_  const OSTimestamp_t* deadline,
    _Out_ oserr_t*             resultOut)
{
    if (fence == NULL || resultOut == NULL) {
        WARNING("OSFenceWait: missing fence or result output");
        return OS_EINVALPARAMS;
    }
    return __OSFenceStorageWait(fence->Storage, fence->FutexFlags, deadline, resultOut);
}

oserr_t
OSFencePrepare(
    _In_ OSFence_t* fence)
{
    int state = __OSFenceIdle;

    if (fence == NULL) {
        WARNING("OSFencePrepare: missing fence");
        return OS_EINVALPARAMS;
    }

    // Binding reserves the fence for this producer. Waiters cannot sleep yet,
    // as submission may still fail and return the fence to Idle.
    if (!atomic_compare_exchange_strong(&fence->Storage->State, &state, __OSFenceBinding)) {
        TRACE("OSFencePrepare: fence %p is not idle (state %d)", fence, state);
        return OS_EBUSY;
    }
    return OS_EOK;
}

void
OSFenceCancel(
    _In_ OSFence_t* fence)
{
    // The caller must have prepared the fence without submitting it. No wake
    // is needed because waiting on Binding is not allowed.
    atomic_store(&fence->Storage->State, __OSFenceIdle);
}

void
OSFenceSubmit(
    _In_ OSFence_t* fence)
{
    // Prepare must have succeeded. Once pending, the producer must signal a
    // result even if the work fails to start. Hardware is started separately.
    atomic_store(&fence->Storage->State, __OSFencePending);
}

oserr_t
OSFenceSignal(
    _In_ OSFence_t* fence,
    _In_ oserr_t    result)
{
    if (fence == NULL) {
        WARNING("OSFenceSignal: missing fence");
        return OS_EINVALPARAMS;
    }
    return __OSFenceStorageSignal(fence->Storage, fence->FutexFlags, result);
}
