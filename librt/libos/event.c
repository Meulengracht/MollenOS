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
        Syscall_DestroyHandle(handleID);
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
        Syscall_DestroyHandle(handleID);
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
    oserr_t             oserr = OS_EOK;
    int                 value;

    parameters.Futex0   = event->SyncAddress;
    parameters.Flags    = FUTEX_FLAG_WAIT;
    parameters.Deadline = NULL;

    for (;;) {
        value = atomic_load(event->SyncAddress);
        while (value < 1) {
            if (options & OSEVENT_LOCK_NONBLOCKING) {
                return OS_EBUSY;
            }

            parameters.Expected0 = value;
            oserr = OSFutex(&parameters, __tls_current()->async_context);
            if (oserr != OS_EOK) {
                break;
            }

            value = atomic_load(event->SyncAddress);
        }

        if (atomic_compare_exchange_strong(event->SyncAddress, &value, value - 1)) {
            break;
        }
    }
    return oserr;
}

oserr_t
OSEventUnlock(
        _In_ OSHandle_t*  handle,
        _In_ unsigned int count)
{
    OSFutexParameters_t parameters;
    struct Event*       event = handle->Payload;
    oserr_t             status = OS_EINCOMPLETE;
    int                 currentValue;
    int                 i;
    int                 result;

    parameters.Futex0    = event->SyncAddress;
    parameters.Expected0 = 0;
    parameters.Flags     = FUTEX_FLAG_WAKE;

    // assert not max
    currentValue = atomic_load(event->SyncAddress);
    if (currentValue < event->MaxValue) {
        for (i = 0; i < count; i++) {
            result = 0;
            while (!result && currentValue < event->MaxValue) {
                result = atomic_compare_exchange_weak(event->SyncAddress,
                                                      &currentValue, currentValue + 1);
                parameters.Expected0++;
            }
        }
    }

    if (parameters.Expected0) {
        OSFutex(&parameters, __tls_current()->async_context);
        status = OS_EOK;
    }

    return status;
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
    (void)Syscall_DestroyHandle(handle->ID);
    free(handle->Payload);
}
