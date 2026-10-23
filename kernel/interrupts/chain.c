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
 * A completion is a provider callback run after a device's interrupt handler.
 * It lets a bus provider acknowledge its own hardware without requiring the
 * device driver to know how that hardware works.
 */

#include <interrupts.h>
#include <interrupts_chain.h>
#include <interrupts_resources.h>
#include <heap.h>
#include <spinlock.h>
#include <string.h>
#include <limits.h>

/**
 * Tracks one callback and the kernel mappings used to execute it. It does not
 * reserve an interrupt vector. Each attached group vector adds one to References,
 * preventing explicit release until that vector has stopped using the callback.
 * Updating blocks attachment and other changes while mapping or cleanup runs.
 * Ready becomes true only after every required mapping has succeeded.
 */
struct InterruptCompletion {
    struct InterruptCompletion* Next;
    uuid_t                      Handle;
    uuid_t                      Owner;
    unsigned int                References;
    int                         Updating;
    int                         Ready;
    InterruptResourceTable_t    Resources;
};

static Spinlock_t                  g_completionLock = OS_SPINLOCK_INIT;
static struct InterruptCompletion* g_completions;
static uuid_t                      g_completionGeneration;

static struct InterruptCompletion*
__FindCompletion(
    _In_ uuid_t handle)
{
    struct InterruptCompletion* completion;

    // The caller holds g_completionLock, so another thread cannot remove an
    // object while we walk the list. Returning a pointer alone does not retain it.
    for (completion = g_completions; completion != NULL; completion = completion->Next) {
        if (completion->Handle == handle) {
            return completion;
        }
    }
    return NULL;
}

static oserr_t
__BeginCompletionUpdate(
    _In_  uuid_t                       handle,
    _Out_ struct InterruptCompletion** completionOut)
{
    struct InterruptCompletion* completion;
    oserr_t                     status = OS_EOK;

    // Check ownership and use together under the lock. Otherwise a group could
    // attach between our check and the start of a destructive mapping change.
    SpinlockAcquireIrq(&g_completionLock);
    completion = __FindCompletion(handle);
    if (completion == NULL) {
        status = OS_ENOENT;
    } else if (completion->Owner != InterruptCurrentOwner()) {
        status = OS_EPERMISSIONS;
    } else if (completion->Updating || completion->References != 0) {
        status = OS_EBUSY;
    } else {
        // Mapping may wait for memory and let another thread run. Mark the object
        // busy before unlocking; a spinlock must not stay held across such waits.
        completion->Updating = 1;
        *completionOut = completion;
    }
    SpinlockReleaseIrq(&g_completionLock);
    return status;
}

oserr_t
InterruptCompletionCreate(
    _Out_ uuid_t* handleOut)
{
    struct InterruptCompletion* completion;

    // Create the handle before binding any resources. If binding later fails,
    // the provider still has a handle with which to retry cleanup.
    completion = kmalloc(sizeof(*completion));
    if (completion == NULL) {
        return OS_EOOM;
    }

    memset(completion, 0, sizeof(*completion));
    completion->Owner = InterruptCurrentOwner();

    SpinlockAcquireIrq(&g_completionLock);
    if (g_completionGeneration == UINT32_MAX) {
        SpinlockReleaseIrq(&g_completionLock);
        kfree(completion);
        return OS_EOVERFLOW;
    }

    // Never reuse identities: a delayed request must not refer to a new
    // callback after its original provider has released the completion.
    completion->Handle = ++g_completionGeneration;
    completion->Next = g_completions;
    g_completions = completion;
    *handleOut = completion->Handle;
    SpinlockReleaseIrq(&g_completionLock);
    return OS_EOK;
}

oserr_t
InterruptCompletionBind(
    _In_ uuid_t                    handle,
    _In_ InterruptResourceTable_t* resources)
{
    struct InterruptCompletion* completion;
    oserr_t                     status;

    status = InterruptValidateResources(resources);
    if (status != OS_EOK) {
        return status;
    }
    
    status = __BeginCompletionUpdate(handle, &completion);
    if (status != OS_EOK) {
        return status;
    }

    // A failed bind can leave mappings that still need cleanup. Do not overwrite
    // that record with a new callback, even though the old one is not Ready.
    if (completion->Ready || completion->Resources.Handler != NULL) {
        status = OS_EBUSY;
    } else {
        // Providers use the same mapping rules as device handlers. Keep any
        // unfinished cleanup in Resources, but allow attachment only on success.
        completion->Resources.HandleResource = resources->HandleResource;
        status = InterruptResolveResources(handle, resources, &completion->Resources);
        completion->Ready = status == OS_EOK;
    }
    SpinlockAcquireIrq(&g_completionLock);
    completion->Updating = 0;
    SpinlockReleaseIrq(&g_completionLock);
    return status;
}

oserr_t
InterruptCompletionRelease(
    _In_ uuid_t handle)
{
    struct InterruptCompletion*  completion;
    struct InterruptCompletion** link;
    oserr_t                      status;

    status = __BeginCompletionUpdate(handle, &completion);
    if (status != OS_EOK) {
        return status;
    }

    // The update check accepted only an object with no group references. Groups
    // drop those references after their handlers have stopped using the callback.
    // Clear Ready before cleanup so an unmap failure cannot allow attachment.
    completion->Ready = 0;
    status = InterruptReleaseResources(handle, &completion->Resources);
    SpinlockAcquireIrq(&g_completionLock);
    if (status != OS_EOK) {
        // Leave the handle and remaining mappings in the list. The provider can
        // retry Release, and no resource becomes unreachable after this failure.
        completion->Updating = 0;
        SpinlockReleaseIrq(&g_completionLock);
        return status;
    }

    link = &g_completions;
    while (*link != completion) {
        link = &(*link)->Next;
    }
    *link = completion->Next;
    
    SpinlockReleaseIrq(&g_completionLock);
    kfree(completion);
    return OS_EOK;
}

oserr_t
InterruptCompletionAcquire(
    _In_  uuid_t                       handle,
    _Out_ struct InterruptCompletion** completionOut)
{
    struct InterruptCompletion* completion;
    oserr_t                     status = OS_EOK;

    // Mapping updates happen outside this lock with Updating set. Test that
    // flag before reading Ready, which the mapper may still be changing.
    // Reject a full reference count too: wrapping to zero would incorrectly
    // allow Release to remove a callback that groups are still using.
    SpinlockAcquireIrq(&g_completionLock);
    completion = __FindCompletion(handle);
    if (completion == NULL) {
        status = OS_ENOENT;
    } else if (completion->Owner != InterruptCurrentOwner()) {
        status = OS_EPERMISSIONS;
    } else if (completion->Updating || !completion->Ready || completion->References == UINT_MAX) {
        status = OS_EBUSY;
    } else {
        // Count one use per group vector, including when several vectors share
        // this callback. Release must wait until every such use has ended.
        completion->References++;
        *completionOut = completion;
    }
    SpinlockReleaseIrq(&g_completionLock);
    return status;
}

void
InterruptCompletionPut(
    _In_ struct InterruptCompletion* completion)
{
    if (completion == NULL) {
        return;
    }
    // Dropping the last group reference does not destroy the object. The provider
    // keeps its handle and explicitly releases it when no longer needed.
    SpinlockAcquireIrq(&g_completionLock);
    completion->References--;
    SpinlockReleaseIrq(&g_completionLock);
}

void
InterruptCompletionInvoke(
    _In_ struct InterruptCompletion* completion)
{
    if (completion != NULL) {
        // The caller has already run the device handler and is still counted as
        // an active interrupt reader, so normal unbinding waits for this call too.
        // The group retains this pointer: no handle lookup or lock is needed here.
        // Run even if the device found no work; upstream hardware may still need
        // acknowledgement. Its result does not change the device handler's result.
        completion->Resources.Handler(GetFastInterruptTable(), &completion->Resources);
    }
}
