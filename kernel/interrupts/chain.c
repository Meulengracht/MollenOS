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
 * Bus callbacks run after the device's interrupt handler to acknowledge the
 * bus hardware. The device driver only needs to handle its own interrupt.
 */

#include <interrupts.h>
#include <interrupts_chain.h>
#include <interrupts_resources.h>
#include <heap.h>
#include <spinlock.h>
#include <string.h>
#include <limits.h>

/**
 * @brief A bus callback and the kernel mappings needed to run it.
 * Each attached group vector adds one to References. All vectors must stop
 * using the callback before it can be released. No interrupt vector is reserved.
 * Updating blocks attachment and other changes while mapping or cleanup runs.
 * Ready becomes true only after every required mapping has succeeded.
 */
struct InterruptBusHandler {
    struct InterruptBusHandler* Next;
    uuid_t                      Handle;
    uuid_t                      Owner;
    unsigned int                References;
    int                         Updating;
    int                         Ready;
    InterruptResourceTable_t    Resources;
};

static Spinlock_t                  g_busHandlersLock = OS_SPINLOCK_INIT;
static struct InterruptBusHandler* g_busHandlers;
static uuid_t                      g_busHandlerIdGen;

static struct InterruptBusHandler*
__FindCompletion(
    _In_ uuid_t handle)
{
    struct InterruptBusHandler* handler;

    // The caller must hold g_busHandlersLock while walking the list.
    // This lookup does not add a reference to the callback.
    for (handler = g_busHandlers; handler != NULL; handler = handler->Next) {
        if (handler->Handle == handle) {
            return handler;
        }
    }
    return NULL;
}

static oserr_t
__BeginCompletionUpdate(
    _In_  uuid_t                       handle,
    _Out_ struct InterruptBusHandler** handlerOut)
{
    struct InterruptBusHandler* handler;
    oserr_t                     status = OS_EOK;

    // Check the calling process and group references under the same lock.
    // A group must not attach between this check and the mapping changes.
    SpinlockAcquireIrq(&g_busHandlersLock);
    handler = __FindCompletion(handle);
    if (handler == NULL) {
        status = OS_ENOENT;
    } else if (handler->Owner != InterruptCurrentOwner()) {
        status = OS_EPERMISSIONS;
    } else if (handler->Updating || handler->References != 0) {
        status = OS_EBUSY;
    } else {
        // Mapping may wait for memory. Mark the callback busy before releasing
        // the lock, as we cannot hold a spinlock while waiting.
        handler->Updating = 1;
        *handlerOut = handler;
    }
    SpinlockReleaseIrq(&g_busHandlersLock);
    return status;
}

oserr_t
InterruptBusHandlerCreate(
    _Out_ uuid_t* handleOut)
{
    struct InterruptBusHandler* handler;

    // Create the handle before binding resources so the bus can retry cleanup
    // if binding fails.
    handler = kmalloc(sizeof(struct InterruptBusHandler));
    if (handler == NULL) {
        return OS_EOOM;
    }

    memset(handler, 0, sizeof(struct InterruptBusHandler));
    handler->Owner = InterruptCurrentOwner();

    SpinlockAcquireIrq(&g_busHandlersLock);
    if (g_busHandlerIdGen == UINT32_MAX) {
        SpinlockReleaseIrq(&g_busHandlersLock);
        kfree(handler);
        return OS_EOVERFLOW;
    }

    // Never reuse handles. A delayed request must not find a new callback
    // after the original callback has been released.
    handler->Handle = ++g_busHandlerIdGen;
    handler->Next = g_busHandlers;
    g_busHandlers = handler;
    *handleOut = handler->Handle;
    SpinlockReleaseIrq(&g_busHandlersLock);
    return OS_EOK;
}

oserr_t
InterruptBusHandlerBind(
    _In_ uuid_t                    handle,
    _In_ InterruptResourceTable_t* resources)
{
    struct InterruptBusHandler* handler;
    oserr_t                     status;

    status = InterruptValidateResources(resources);
    if (status != OS_EOK) {
        return status;
    }
    
    status = __BeginCompletionUpdate(handle, &handler);
    if (status != OS_EOK) {
        return status;
    }

    // A failed bind may leave mappings behind. Cleanup must finish before
    // binding another callback, even when Ready is clear.
    if (handler->Ready || handler->Resources.Handler != NULL) {
        status = OS_EBUSY;
    } else {
        // Bus callbacks use the same mapping rules as device handlers.
        // Keep any remaining mappings in Resources so cleanup can be retried.
        // Allow attachment only after all mappings succeed.
        handler->Resources.HandleResource = resources->HandleResource;
        status = InterruptResolveResources(handle, resources, &handler->Resources);
        handler->Ready = status == OS_EOK;
    }
    SpinlockAcquireIrq(&g_busHandlersLock);
    handler->Updating = 0;
    SpinlockReleaseIrq(&g_busHandlersLock);
    return status;
}

oserr_t
InterruptBusHandlerRelease(
    _In_ uuid_t handle)
{
    struct InterruptBusHandler*  handler;
    struct InterruptBusHandler** link;
    oserr_t                      status;

    status = __BeginCompletionUpdate(handle, &handler);
    if (status != OS_EOK) {
        return status;
    }

    // No groups reference the callback now. Each group drops its reference
    // after its handlers have stopped using the callback.
    // Clear Ready before cleanup so an unmap failure cannot allow attachment.
    handler->Ready = 0;
    status = InterruptReleaseResources(handle, &handler->Resources);
    SpinlockAcquireIrq(&g_busHandlersLock);
    if (status != OS_EOK) {
        // Keep the handle and remaining mappings so the bus can retry release.
        handler->Updating = 0;
        SpinlockReleaseIrq(&g_busHandlersLock);
        return status;
    }

    link = &g_busHandlers;
    while (*link != handler) {
        link = &(*link)->Next;
    }
    *link = handler->Next;
    
    SpinlockReleaseIrq(&g_busHandlersLock);
    kfree(handler);
    return OS_EOK;
}

oserr_t
InterruptBusHandlerAcquire(
    _In_  uuid_t                       handle,
    _Out_ struct InterruptBusHandler** handlerOut)
{
    struct InterruptBusHandler* handler;
    oserr_t                     status = OS_EOK;

    // Mapping runs outside this lock with Updating set. Check Updating before
    // reading Ready, as the mapping code may still be changing it.
    // The reference count must not wrap to zero, which would allow release
    // while groups are still using the callback.
    SpinlockAcquireIrq(&g_busHandlersLock);
    handler = __FindCompletion(handle);
    if (handler == NULL) {
        status = OS_ENOENT;
    } else if (handler->Owner != InterruptCurrentOwner()) {
        status = OS_EPERMISSIONS;
    } else if (handler->Updating || !handler->Ready || handler->References == UINT_MAX) {
        status = OS_EBUSY;
    } else {
        // Each group vector needs a reference, even if several share this
        // callback. All references must be dropped before release.
        handler->References++;
        *handlerOut = handler;
    }
    SpinlockReleaseIrq(&g_busHandlersLock);
    return status;
}

void
InterruptBusHandlerPut(
    _In_ struct InterruptBusHandler* handler)
{
    if (handler == NULL) {
        return;
    }
    // The bus must release the handle after the last group stops using it.
    SpinlockAcquireIrq(&g_busHandlersLock);
    handler->References--;
    SpinlockReleaseIrq(&g_busHandlersLock);
}

void
InterruptBusHandlerInvoke(
    _In_ struct InterruptBusHandler* handler)
{
    if (handler != NULL) {
        // The device handler has finished, but the caller is still an active
        // interrupt reader. Unbinding therefore waits for this callback too.
        // The group's reference keeps this pointer valid without a lookup or lock.
        // Run even if the device found no work, as the bus hardware may still
        // need acknowledgement. Ignore the result to preserve the device's result.
        handler->Resources.Handler(GetFastInterruptTable(), &handler->Resources);
    }
}
