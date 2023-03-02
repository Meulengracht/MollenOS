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

#include "os/notification_queue.h"
#include <errno.h>
#include <event.h>
#include <internal/_io.h>
#include <os/event.h>
#include <io.h>
#include <ioctl.h>

struct Event {
    unsigned int Flags;
    unsigned int Options;
};

static oserr_t __evt_inherit(stdio_handle_t*);
static oserr_t __evt_read(stdio_handle_t*, void*, size_t, size_t*);
static oserr_t __evt_write(stdio_handle_t*, const void*, size_t, size_t*);
static oserr_t __evt_resize(stdio_handle_t*, long long);
static oserr_t __evt_seek(stdio_handle_t*, int, off64_t, long long*);
static oserr_t __evt_ioctl(stdio_handle_t*, int, va_list);
static void    __evt_close(stdio_handle_t*, int);

static stdio_ops_t g_evtOps = {
        .inherit = __evt_inherit,
        .read = __evt_read,
        .write = __evt_write,
        .resize = __evt_resize,
        .seek = __evt_seek,
        .ioctl = __evt_ioctl,
        .close = __evt_close
};

int eventd(unsigned int initialValue, unsigned int flags)
{
    stdio_handle_t* object;
    int             status;
    OSHandle_t      handle;
    oserr_t         oserr;

    if (EVT_TYPE(flags) == EVT_RESET_EVENT) {
        initialValue = 1;
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
        return OsErrToErrNo(oserr);
    }

    status = stdio_handle_create2(
            -1,
            O_RDWR,
            0,
            EVENT_SIGNATURE,
            &g_evtOps,
            NULL,
            &object
    );
    if (status) {
        OSHandleDestroy(handle);
        return -1;
    }

    stdio_handle_set_handle(object, &handle);

    object->object.data.evt.flags   = flags;
    object->object.data.evt.options = 0;
    return object->fd;
}

static oserr_t
__evt_read(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytes_read)
{
    oserr_t result;

    // Sanitize buffer and length for RESET and SEM events
    if (EVT_TYPE(handle->object.data.evt.flags) != EVT_TIMEOUT_EVENT) {
        if (!buffer || length < sizeof(unsigned int)) {
            return OS_EPERMISSIONS;
        }
    }

    result = OSEventLock(&handle->handle, handle->object.data.evt.options);
    if (result != OS_EOK) {
        return result;
    }

    // Read the reset event value if we are a reset event, otherwise for SEM read value is 1
    if (EVT_TYPE(handle->object.data.evt.flags) == EVT_RESET_EVENT) {
        *(unsigned int*)buffer = handle->object.data.evt.initialValue;
        *bytes_read            = sizeof(unsigned int);
    } else if (EVT_TYPE(handle->object.data.evt.flags) == EVT_SEM_EVENT) {
        *(unsigned int*)buffer = 1;
        *bytes_read            = sizeof(unsigned int);
    }
    return OS_EOK;
}

static oserr_t
__evt_write(stdio_handle_t* handle, const void* buffer, size_t length, size_t* bytes_written)
{
    oserr_t result = OS_ENOTSUPPORTED;
    if (!buffer || length < sizeof(unsigned int)) {
        return OS_EPERMISSIONS;
    }

    if (EVT_TYPE(handle->object.data.evt.flags) == EVT_RESET_EVENT) {
        handle->object.data.evt.initialValue = *(unsigned int*)buffer;
        *bytes_written = sizeof(unsigned int);

        result = OSEventUnlock(handle->object.data.evt.sync_address, 1);
        if (result == OS_EOK) {
            OSNotificationQueuePost(handle->object.handle, IOSETSYN);
        }
    } else if (EVT_TYPE(handle->object.data.evt.flags) == EVT_SEM_EVENT) {
        unsigned int value = *(unsigned int*)buffer;

        result = OSEventUnlock(handle->object.data.evt.sync_address, value);
        if (result == OS_EOK) {
            OSNotificationQueuePost(handle->object.handle, IOSETSYN);
        }
        *bytes_written = sizeof(size_t);
    }
    return result;
}

static oserr_t
__evt_seek(stdio_handle_t* handle, int origin, off64_t offset, long long* position_out)
{
    return OS_ENOTSUPPORTED;
}

static oserr_t
__evt_resize(
        _In_ stdio_handle_t* handle,
        _In_ long long       resize_by)
{
    return OS_ENOTSUPPORTED;
}

static void
__evt_close(
        _In_ stdio_handle_t* handle,
        _In_ int             options)
{
    if (options & STDIO_CLOSE_FULL) {
        if (handle->object.handle != UUID_INVALID) {
            (void)OSHandleDestroy(handle->object.handle);
        }
    }
}

static oserr_t
__evt_inherit(
        _In_ stdio_handle_t* handle)
{
    // we can't inherit them atm, we need the userspace mapping mapped into this process as well
    return OS_ENOTSUPPORTED;
}

static oserr_t
__evt_ioctl(
        _In_ stdio_handle_t* handle,
        _In_ int             request,
        _In_ va_list         args)
{
    if ((unsigned int)request == FIONBIO) {
        int* nonBlocking = va_arg(args, int*);
        if (nonBlocking) {
            if (*nonBlocking) {
                handle->object.data.evt.options |= OSEVENT_LOCK_NONBLOCKING;
            } else {
                handle->object.data.evt.options &= ~(OSEVENT_LOCK_NONBLOCKING);
            }
        }
        return OS_EOK;
    } else if ((unsigned int)request == FIONREAD) {
        int* bytesAvailableOut = va_arg(args, int*);
        if (bytesAvailableOut) {
            *bytesAvailableOut = atomic_load(handle->object.data.evt.sync_address);
        }
        return OS_EOK;
    }
    return OS_ENOTSUPPORTED;
}
