/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Generic IO Events. These are functions that may be supported by all
 * io descriptors. If they are not supported errno is set to ENOTSUPP
 */

#include <ddk/handle.h>
#include <errno.h>
#include <internal/_io.h>
#include <io_events.h>
#include <os/mollenos.h>

int io_set_create(int flags)
{
    stdio_handle_t* io_object;
    OsStatus_t      os_status;
    UUId_t          handle;
    int             status;
    
    status = stdio_handle_create(-1, WX_OPEN, &io_object);
    if (status) {
        return -1;
    }
    
    os_status = handle_set_create(0, &handle);
    if (os_status != OsSuccess) {
        (void)OsStatusToErrno(os_status);
        stdio_handle_destroy(io_object, 0);
        return -1;
    }
    
    stdio_handle_set_handle(io_object, handle);
    return io_object->fd;
}

int io_set_ctrl(int evt_iod, int op, int iod, int events)
{
    stdio_handle_t* io_set_object = stdio_handle_get(evt_iod);
    stdio_handle_t* io_object     = stdio_handle_get(iod);
    OsStatus_t      status;
    
    if (!io_set_object || !io_object) {
        _set_errno(ENOENT);
        return -1;
    }
    
    status = handle_set_ctrl(io_set_object->object.handle, op,
        io_object->object.handle, events, (void*)((size_t)iod));
    if (status != OsSuccess) {
        (void)OsStatusToErrno(status);
        return -1;
    }
    return 0;
}

int io_set_wait(int evt_iod, struct io_event* events, int max_events, int timeout)
{
    stdio_handle_t* io_set_object = stdio_handle_get(evt_iod);
    int             num_events;
    int             i;
    OsStatus_t      status;
    
    if (!io_set_object) {
        _set_errno(ENOENT);
        return -1;
    }
    
    handle_event_t h_events[max_events];
    status = handle_set_wait(io_set_object->object.handle, &h_events[0], 
        max_events, timeout, &num_events);
    if (status != OsSuccess) {
        (void)OsStatusToErrno(status);
        return -1;
    }
    
    for (i = 0; i < num_events; i++) {
        events[i].iod    = (int)((size_t)h_events[i].context);
        events[i].events = h_events[i].events;
    }
    return num_events;
}
