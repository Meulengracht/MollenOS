/**
 * Copyright 2020, Philip Meulengracht
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

#include <errno.h>
#include <event.h>
#include <internal/_io.h>
#include <os/handle.h>
#include <os/notification_queue.h>
#include <os/event.h>
#include <io.h>
#include <ioctl.h>
#include <stdlib.h>

struct Event {
    unsigned int InitialValue;
    unsigned int Flags;
    unsigned int Options;
};

static oserr_t __evt_read(stdio_handle_t*, void*, size_t, size_t*);
static oserr_t __evt_write(stdio_handle_t*, const void*, size_t, size_t*);
static oserr_t __evt_ioctl(stdio_handle_t*, int, va_list);
static void    __evt_close(stdio_handle_t*, int);

stdio_ops_t g_evtOps = {
        .read = __evt_read,
        .write = __evt_write,
        .ioctl = __evt_ioctl,
        .close = __evt_close
};

static struct Event*
__event_new(
        _In_ unsigned int initialValue,
        _In_ unsigned int flags)
{
    struct Event* event;

    event = malloc(sizeof(struct Event));
    if (event == NULL) {
        return NULL;
    }
    event->InitialValue = initialValue;
    event->Flags = flags;
    event->Options = 0;
    return event;
}

int eventd(unsigned int initialValue, unsigned int flags)
{
    stdio_handle_t* object;
    struct Event*   event;
    int             status;
    OSHandle_t      handle;
    oserr_t         oserr;

    if (EVT_TYPE(flags) == EVT_RESET_EVENT) {
        initialValue = 1;
    }

    event = __event_new(initialValue, flags);
    if (event == NULL) {
        return -1;
    }

    if (EVT_TYPE(flags) != EVT_TIMEOUT_EVENT) {
        oserr = OSEvent(
                initialValue,
                initialValue,
                &handle
        );
    } else {
        oserr = OSTimeoutEvent(
                initialValue,
                &handle
        );
    }
    if (oserr != OS_EOK) {
        free(event);
        return OsErrToErrNo(oserr);
    }

    status = stdio_handle_create2(
            -1,
            O_RDWR | O_NOINHERIT,
            0,
            EVENT_SIGNATURE,
            event,
            &object
    );
    if (status) {
        OSHandleDestroy(handle.ID);
        return -1;
    }

    stdio_handle_set_handle(object, &handle);
    return stdio_handle_iod(object);
}

static oserr_t
__evt_read(
        _In_  stdio_handle_t* handle,
        _In_  void*           buffer,
        _In_  size_t          length,
        _Out_ size_t*         bytes_read)
{
    struct Event* event = handle->OpsContext;
    oserr_t       oserr;

    // Sanitize buffer and length for RESET and SEM events
    if (EVT_TYPE(event->Flags) != EVT_TIMEOUT_EVENT) {
        if (!buffer || length < sizeof(unsigned int)) {
            return OS_EPERMISSIONS;
        }
    }

    oserr = OSEventLock(&handle->OSHandle, event->Options);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Read the reset event value if we are a reset event, otherwise for SEM read value is 1
    if (EVT_TYPE(event->Flags) == EVT_RESET_EVENT) {
        *(unsigned int*)buffer = event->InitialValue;
        *bytes_read            = sizeof(unsigned int);
    } else if (EVT_TYPE(event->Flags) == EVT_SEM_EVENT) {
        *(unsigned int*)buffer = 1;
        *bytes_read            = sizeof(unsigned int);
    }
    return OS_EOK;
}

static oserr_t
__evt_write(
        _In_  stdio_handle_t* handle,
        _In_  const void*     buffer,
        _In_  size_t          length,
        _Out_ size_t*         bytes_written)
{
    struct Event* event = handle->OpsContext;
    oserr_t       result = OS_ENOTSUPPORTED;
    if (!buffer || length < sizeof(unsigned int)) {
        return OS_EPERMISSIONS;
    }

    if (EVT_TYPE(event->Flags) == EVT_RESET_EVENT) {
        event->InitialValue = *(unsigned int*)buffer;
        *bytes_written = sizeof(unsigned int);

        result = OSEventUnlock(&handle->OSHandle, 1);
        if (result == OS_EOK) {
            OSNotificationQueuePost(&handle->OSHandle, IOSETSYN);
        }
    } else if (EVT_TYPE(event->Flags) == EVT_SEM_EVENT) {
        unsigned int value = *(unsigned int*)buffer;

        result = OSEventUnlock(&handle->OSHandle, value);
        if (result == OS_EOK) {
            OSNotificationQueuePost(&handle->OSHandle, IOSETSYN);
        }
        *bytes_written = sizeof(size_t);
    }
    return result;
}

static void
__evt_close(
        _In_ stdio_handle_t* handle,
        _In_ int             options)
{
    if (options & STDIO_CLOSE_FULL) {
        (void)OSHandleDestroy(handle->OSHandle.ID);
        free(handle->OpsContext);
    }
}

static oserr_t
__evt_ioctl(
        _In_ stdio_handle_t* handle,
        _In_ int             request,
        _In_ va_list         args)
{
    struct Event* event = handle->OpsContext;
    if ((unsigned int)request == FIONBIO) {
        int* nonBlocking = va_arg(args, int*);
        if (nonBlocking) {
            if (*nonBlocking) {
                event->Options |= OSEVENT_LOCK_NONBLOCKING;
            } else {
                event->Options &= ~(OSEVENT_LOCK_NONBLOCKING);
            }
        }
        return OS_EOK;
    } else if ((unsigned int)request == FIONREAD) {
        int* bytesAvailableOut = va_arg(args, int*);
        if (bytesAvailableOut) {
            *bytesAvailableOut = OSEventValue(&handle->OSHandle);
        }
        return OS_EOK;
    }
    return OS_ENOTSUPPORTED;
}
