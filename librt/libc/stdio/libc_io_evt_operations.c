/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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

#define __need_minmax
#include <ddk/handle.h>
#include <event.h>
#include <internal/_io.h>
#include <os/futex.h>
#include <ioctl.h>

#define EVT_OPTION_NON_BLOCKING 0x1

static oserr_t evt_lock(atomic_int* sync_address, unsigned int options)
{
    OSFutexParameters_t parameters;
    oserr_t             oserr = OS_EOK;
    int               value;

    parameters._futex0  = sync_address;
    parameters._flags   = FUTEX_FLAG_WAIT;
    parameters._timeout = 0;

    while (1) {
        value = atomic_load(sync_address);
        while (value < 1) {
            if (options & EVT_OPTION_NON_BLOCKING) {
                return OS_EBUSY;
            }

            parameters._val0 = value;
            oserr = Futex(&parameters, NULL);
            if (oserr != OS_EOK) {
                break;
            }

            value = atomic_load(sync_address);
        }

        if (atomic_compare_exchange_strong(sync_address, &value, value - 1)) {
            break;
        }
    }
    return oserr;
}

static oserr_t evt_unlock(atomic_int* sync_address, unsigned int maxValue, unsigned int value)
{
    OSFutexParameters_t parameters;
    oserr_t             status = OS_EINCOMPLETE;
    int               currentValue;
    int               i;
    int               result;

    parameters._futex0 = sync_address;
    parameters._val0   = 0;
    parameters._flags  = FUTEX_FLAG_WAKE;

    // assert not max
    currentValue = atomic_load(sync_address);
    if (currentValue < maxValue) {
        for (i = 0; i < value; i++) {
            result = 0;
            while (!result && currentValue < maxValue) {
                result = atomic_compare_exchange_weak(sync_address,
                        &currentValue, currentValue + 1);
                parameters._val0++;
            }
        }
    }

    if (parameters._val0) {
        Futex(&parameters, NULL);
        status = OS_EOK;
    }

    return status;
}

oserr_t stdio_evt_op_read(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytes_read)
{
    oserr_t result;

    // Sanitize buffer and length for RESET and SEM events
    if (EVT_TYPE(handle->object.data.evt.flags) != EVT_TIMEOUT_EVENT) {
        if (!buffer || length < sizeof(unsigned int)) {
            return OS_EPERMISSIONS;
        }
    }

    result = evt_lock(handle->object.data.evt.sync_address, handle->object.data.evt.options);
    if (result != OS_EOK) {
        return result;
    }

    // Read the reset event value if we are a reset event, otherwise for SEM read value is 1
    if (EVT_TYPE(handle->object.data.evt.flags) == EVT_RESET_EVENT) {
        *(unsigned int*)buffer = handle->object.data.evt.initialValue;
        *bytes_read            = sizeof(unsigned int);
    }
    else if (EVT_TYPE(handle->object.data.evt.flags) == EVT_SEM_EVENT) {
        *(unsigned int*)buffer = 1;
        *bytes_read            = sizeof(unsigned int);
    }
    return OS_EOK;
}

oserr_t stdio_evt_op_write(stdio_handle_t* handle, const void* buffer, size_t length, size_t* bytes_written)
{
    oserr_t result = OS_ENOTSUPPORTED;
    if (!buffer || length < sizeof(unsigned int)) {
        return OS_EPERMISSIONS;
    }

    if (EVT_TYPE(handle->object.data.evt.flags) == EVT_RESET_EVENT) {
        handle->object.data.evt.initialValue = *(unsigned int*)buffer;
        *bytes_written = sizeof(unsigned int);

        result = evt_unlock(handle->object.data.evt.sync_address, 1, 1);
        if (result == OS_EOK) {
            OSNotificationQueuePost(handle->object.handle, IOSETSYN);
        }
    }
    else if (EVT_TYPE(handle->object.data.evt.flags) == EVT_SEM_EVENT) {
        unsigned int value = *(unsigned int*)buffer;

        result = evt_unlock(handle->object.data.evt.sync_address,
                            handle->object.data.evt.initialValue,
                            value);
        if (result == OS_EOK) {
            OSNotificationQueuePost(handle->object.handle, IOSETSYN);
        }
        *bytes_written = sizeof(size_t);
    }

    return result;
}

oserr_t stdio_evt_op_seek(stdio_handle_t* handle, int origin, off64_t offset, long long* position_out)
{
    return OS_ENOTSUPPORTED;
}

oserr_t stdio_evt_op_resize(stdio_handle_t* handle, long long resize_by)
{
    return OS_ENOTSUPPORTED;
}

oserr_t stdio_evt_op_close(stdio_handle_t* handle, int options)
{
    if (options & STDIO_CLOSE_FULL) {
        if (handle->object.handle != UUID_INVALID) {
            return OSHandleDestroy(handle->object.handle);
        }
    }
    return OS_EOK;
}

oserr_t stdio_evt_op_inherit(stdio_handle_t* handle)
{
    // we can't inherit them atm, we need the userspace mapping mapped into this process as well
    return OS_ENOTSUPPORTED;
}

oserr_t stdio_evt_op_ioctl(stdio_handle_t* handle, int request, va_list args)
{
    if ((unsigned int)request == FIONBIO) {
        int* nonBlocking = va_arg(args, int*);
        if (nonBlocking) {
            if (*nonBlocking) {
                handle->object.data.evt.options |= EVT_OPTION_NON_BLOCKING;
            }
            else {
                handle->object.data.evt.options &= ~(EVT_OPTION_NON_BLOCKING);
            }
        }
        return OS_EOK;
    }
    else if ((unsigned int)request == FIONREAD) {
        int* bytesAvailableOut = va_arg(args, int*);
        if (bytesAvailableOut) {
            *bytesAvailableOut = atomic_load(handle->object.data.evt.sync_address);
        }
        return OS_EOK;
    }
    return OS_ENOTSUPPORTED;
}

void stdio_get_evt_operations(stdio_ops_t* ops)
{
    ops->inherit = stdio_evt_op_inherit;
    ops->read    = stdio_evt_op_read;
    ops->write   = stdio_evt_op_write;
    ops->seek    = stdio_evt_op_seek;
    ops->resize  = stdio_evt_op_resize;
    ops->ioctl   = stdio_evt_op_ioctl;
    ops->close   = stdio_evt_op_close;
}
