/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#define __TRACE

#include <ddk/utils.h>
#include <internal/_io.h>
#include <os/mollenos.h>

int socket_create(int domain, int type, int protocol, uuid_t handle,
                  uuid_t send_handle, uuid_t recv_handle)
{
    stdio_handle_t* ioObject;
    int             status;
    oserr_t      osStatus;
    TRACE("[socket] creating from handle %u", LODWORD(handle));
    
    status = stdio_handle_create(-1, WX_PIPE, &ioObject);
    if (status) {
        return -1;
    }
    
    TRACE("[socket] initializing libc structures");
    stdio_handle_set_handle(ioObject, handle);
    stdio_handle_set_ops_type(ioObject, STDIO_HANDLE_SOCKET);
    
    ioObject->object.data.socket.domain   = domain;
    ioObject->object.data.socket.type     = type;
    ioObject->object.data.socket.protocol = protocol;
    
    ioObject->object.data.socket.send_buffer.ID = send_handle;
    ioObject->object.data.socket.recv_buffer.ID = recv_handle;
    
    TRACE("[socket] mapping pipes");
    osStatus = ioObject->ops.inherit(ioObject);
    if (osStatus != OS_EOK) {
        (void)OsErrToErrNo(osStatus);
        ioObject->ops.close(ioObject, 0);
        stdio_handle_delete(ioObject);
        return -1;
    }
    TRACE("[socket] done %i", ioObject->fd);
    return ioObject->fd;
}
