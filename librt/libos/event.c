/**
 * Copyright 2023, Philip Meulengracht
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

#include <ddk/utils.h>
#include <os/event.h>
#include <internal/_syscalls.h>
#include <internal/_tls.h>
#include <os/futex.h>
#include <os/handle.h>
#include <stdlib.h>

struct Event {
    _Atomic(int)* SyncAddress;
    unsigned int  MaxValue;
};

static void __EventDestroy(struct OSHandle*);

const OSHandleOps_t g_eventOps = {
        .Destroy = __EventDestroy
};

static struct Event*
__EventNew(
        _In_ unsigned int maxValue)
{
    struct Event* event;

    event = malloc(sizeof(struct Event));
    if (event == NULL) {
        return NULL;
    }
    event->SyncAddress = NULL,
    event->MaxValue = maxValue;
    return event;
}

oserr_t
OSEvent(
        _In_  unsigned int initialValue,
        _In_  unsigned int maxValue,
        _Out_ OSHandle_t*  handleOut)
{
    struct Event* event;
    uuid_t        handleID;
    oserr_t       oserr;

    event = __EventNew(maxValue);
    if (event == NULL) {
        return OS_EOOM;
    }

    oserr = Syscall_EventCreate(initialValue, 0, &handleID, &event->SyncAddress);
    if (oserr != OS_EOK) {
        free(event);
        return oserr;
    }

    oserr = OSHandleWrap(
            handleID,
            OSHANDLE_EVENT,
            event,
            true,
            handleOut
    );
    if (oserr != OS_EOK) {
        Syscall_HandleDestroy(handleID);
        free(event);
        return oserr;
    }
    return OS_EOK;
}

oserr_t
OSTimeoutEvent(
        _In_  unsigned int timeout,
        _Out_ OSHandle_t*  handleOut)
{
    struct Event* event;
    uuid_t        handleID;
    oserr_t       oserr;

    event = __EventNew(0);
    if (event == NULL) {
        return OS_EOOM;
    }

    oserr = Syscall_EventCreate(timeout, 2, &handleID, &event->SyncAddress);
    if (oserr != OS_EOK) {
        free(event);
        return oserr;
    }

    oserr = OSHandleWrap(
            handleID,
            OSHANDLE_EVENT,
            event,
            true,
            handleOut
    );
    if (oserr != OS_EOK) {
        Syscall_HandleDestroy(handleID);
        free(event);
        return oserr;
    }
    return OS_EOK;
}

oserr_t
OSEventLock(
        _In_ OSHandle_t*  handle,
        _In_ unsigned int options)
{
    OSFutexParameters_t parameters;
    struct Event*       event = handle->Payload;
    oserr_t             oserr;
    int                 value;

    parameters.Futex0   = event->SyncAddress;
    parameters.Flags    = FUTEX_FLAG_WAIT;
    parameters.Deadline = NULL;

    // The counter is only lowered by the compare-exchange below, and only
    // from a value of 1 or more. So it can never go below 0, also when a wait
    // fails.
    for (;;) {
        value = atomic_load(event->SyncAddress);
        if (value < 1) {
            if (options & OSEVENT_LOCK_NONBLOCKING) {
                return OS_EBUSY;
            }

            // Sleep while the counter still has the value we read. The kernel
            // refuses the wait with OS_EINTERRUPTED when the counter changed
            // before we got to sleep. Then we read it again. Any other failed
            // wait is returned, and the counter is left as it is.
            parameters.Expected0 = value;
            oserr = OSFutex(&parameters, __tls_current()->async_context);
            if (oserr == OS_EINTERRUPTED && atomic_load(event->SyncAddress) != value) {
                continue;
            }
            if (oserr != OS_EOK) {
                return oserr;
            }
            continue;
        }

        // Another thread may take the unit first. The exchange then fails and
        // we start over with the new value.
        if (atomic_compare_exchange_strong(event->SyncAddress, &value, value - 1)) {
            return OS_EOK;
        }
    }
}

oserr_t
OSEventUnlock(
        _In_ OSHandle_t*  handle,
        _In_ unsigned int count)
{
    OSFutexParameters_t parameters;
    struct Event*       event = handle->Payload;
    int                 currentValue;
    unsigned int        i;
    oserr_t             oserr;

    parameters.Futex0    = event->SyncAddress;
    parameters.Expected0 = 0;
    parameters.Flags     = FUTEX_FLAG_WAKE;

    // Raise the counter by up to count units, but never above MaxValue.
    // Expected0 counts the units that were added, and is the number of
    // waiters to wake. A failed exchange loads the current value and tries
    // again, so it adds nothing to Expected0. OSEventLock never lowers the
    // counter below 0. The check for it only keeps the unsigned comparison
    // with MaxValue correct.
    currentValue = atomic_load(event->SyncAddress);
    for (i = 0; i < count && currentValue >= 0 && (unsigned int)currentValue < event->MaxValue; ) {
        if (atomic_compare_exchange_weak(event->SyncAddress, &currentValue, currentValue + 1)) {
            currentValue++;
            parameters.Expected0++;
            i++;
        }
    }

    // OS_EINCOMPLETE tells the caller that nothing was added, because the
    // counter was already at its maximum or count was 0.
    if (parameters.Expected0 == 0) {
        return OS_EINCOMPLETE;
    }

    // The units are added at this point, so the result stays OS_EOK. A wake
    // with nobody asleep returns OS_ENOENT, which is normal.
    oserr = OSFutex(&parameters, __tls_current()->async_context);
    if (oserr != OS_EOK && oserr != OS_ENOENT) {
        ERROR("OSEventUnlock: waking waiters on event %u failed: %u", handle->ID, oserr);
    }
    return OS_EOK;
}

int
OSEventValue(
        _In_ OSHandle_t* handle)
{
    struct Event* event;

    if (handle == NULL) {
        return 0;
    }

    event = handle->Payload;
    return atomic_load(event->SyncAddress);
}

static void __EventDestroy(struct OSHandle* handle)
{
    (void)Syscall_HandleDestroy(handle->ID);
    free(handle->Payload);
}
