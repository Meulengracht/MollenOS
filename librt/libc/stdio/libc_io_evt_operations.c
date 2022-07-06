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
 *
 *
 * C Standard Library
 * - Standard IO null operation implementations.
 */

#include <ddk/handle.h>
#include <ddk/utils.h>
#include <event.h>
#include <internal/_io.h>
#include <internal/_syscalls.h>
#include <ioctl.h>

#define EVT_OPTION_NON_BLOCKING 0x1

static oscode_t evt_lock(atomic_int* sync_address, unsigned int options)
{
    FutexParameters_t parameters;
    oscode_t        status = OsOK;
    int               value;

    parameters._futex0  = sync_address;
    parameters._flags   = 0;
    parameters._timeout = 0;

    while (1) {
        value = atomic_load(sync_address);
        while (value < 1) {
            if (options & EVT_OPTION_NON_BLOCKING) {
                return OsBusy;
            }

            parameters._val0 = value;
            status = Syscall_FutexWait(&parameters);
            if (status != OsOK) {
                break;
            }

            value = atomic_load(sync_address);
        }

        if (atomic_compare_exchange_strong(sync_address, &value, value - 1)) {
            break;
        }
    }
    return status;
}

static oscode_t evt_unlock(atomic_int* sync_address, unsigned int maxValue, unsigned int value)
{
    FutexParameters_t parameters;
    oscode_t        status = OsIncomplete;
    int               currentValue;
    int               i;
    int               result;

    parameters._futex0 = sync_address;
    parameters._val0   = 0;
    parameters._flags  = 0;

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
        Syscall_FutexWake(&parameters);
        status = OsOK;
    }

    return status;
}

oscode_t stdio_evt_op_read(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytes_read)
{
    oscode_t result;

    // Sanitize buffer and length for RESET and SEM events
    if (EVT_TYPE(handle->object.data.evt.flags) != EVT_TIMEOUT_EVENT) {
        if (!buffer || length < sizeof(unsigned int)) {
            return OsInvalidPermissions;
        }
    }

    result = evt_lock(handle->object.data.evt.sync_address, handle->object.data.evt.options);
    if (result != OsOK) {
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
    return OsOK;
}

oscode_t stdio_evt_op_write(stdio_handle_t* handle, const void* buffer, size_t length, size_t* bytes_written)
{
    oscode_t result = OsNotSupported;
    if (!buffer || length < sizeof(unsigned int)) {
        return OsInvalidPermissions;
    }

    if (EVT_TYPE(handle->object.data.evt.flags) == EVT_RESET_EVENT) {
        handle->object.data.evt.initialValue = *(unsigned int*)buffer;
        *bytes_written = sizeof(unsigned int);

        result = evt_unlock(handle->object.data.evt.sync_address, 1, 1);
        if (result == OsOK) {
            handle_post_notification(handle->object.handle, IOSETSYN);
        }
    }
    else if (EVT_TYPE(handle->object.data.evt.flags) == EVT_SEM_EVENT) {
        unsigned int value = *(unsigned int*)buffer;

        result = evt_unlock(handle->object.data.evt.sync_address,
                            handle->object.data.evt.initialValue,
                            value);
        if (result == OsOK) {
            handle_post_notification(handle->object.handle, IOSETSYN);
        }
        *bytes_written = sizeof(size_t);
    }

    return result;
}

oscode_t stdio_evt_op_seek(stdio_handle_t* handle, int origin, off64_t offset, long long* position_out)
{
    return OsNotSupported;
}

oscode_t stdio_evt_op_resize(stdio_handle_t* handle, long long resize_by)
{
    return OsNotSupported;
}

oscode_t stdio_evt_op_close(stdio_handle_t* handle, int options)
{
    if (options & STDIO_CLOSE_FULL) {
        if (handle->object.handle != UUID_INVALID) {
            return handle_destroy(handle->object.handle);
        }
    }
    return OsOK;
}

oscode_t stdio_evt_op_inherit(stdio_handle_t* handle)
{
    // we can't inherit them atm, we need the userspace mapping mapped into this process as well
    return OsNotSupported;
}

oscode_t stdio_evt_op_ioctl(stdio_handle_t* handle, int request, va_list args)
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
        return OsOK;
    }
    else if ((unsigned int)request == FIONREAD) {
        int* bytesAvailableOut = va_arg(args, int*);
        if (bytesAvailableOut) {
            *bytesAvailableOut = atomic_load(handle->object.data.evt.sync_address);
        }
        return OsOK;
    }
    return OsNotSupported;
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
