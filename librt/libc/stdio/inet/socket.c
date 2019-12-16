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
 * Standard C Support
 * - Standard Socket IO Implementation
 */
//#define __TRACE

#include <ddk/services/net.h>
#include <ddk/utils.h>
#include <internal/_io.h>
#include <os/mollenos.h>
#include <stdlib.h>

int socket(int domain, int type, int protocol)
{
    stdio_handle_t* io_object;
    OsStatus_t      os_status;
    UUId_t          handle;
    int             status;
    TRACE("[socket] creating");
    
    status = stdio_handle_create(-1, WX_OPEN | WX_PIPE, &io_object);
    if (status) {
        ERROR("[socket] stdio_handle_create failed with code %u", status);
        return -1;
    }
    
    // We need to create the socket object at kernel level, as we need
    // kernel assisted functionality to support a centralized storage of
    // all system sockets. They are the foundation of the microkernel for
    // communication between processes and are needed long before anything else.
    TRACE("[socket] remote create");
    os_status = CreateSocket(domain, type, protocol, &handle,
        &io_object->object.data.socket.send_buffer.handle, 
        &io_object->object.data.socket.recv_buffer.handle);
    if (os_status != OsSuccess) {
        ERROR("[socket] CreateSocket failed with code %u", os_status);
        (void)OsStatusToErrno(os_status);
        stdio_handle_destroy(io_object, 0);
        return -1;
    }
    
    // We have been given a recieve queue that has been mapped into our
    // own address space for quick reading when bytes available.
    TRACE("[socket] initializing libc structures");
    stdio_handle_set_handle(io_object, handle);
    stdio_handle_set_ops_type(io_object, STDIO_HANDLE_SOCKET);
    
    io_object->object.data.socket.domain   = domain;
    io_object->object.data.socket.type     = type;
    io_object->object.data.socket.protocol = protocol;
    
    // Setup the dma buffers that we have to inherit, just reuse the inherit
    // stdio method, since the actions are identical.
    TRACE("[socket] mapping pipes");
    os_status = io_object->ops.inherit(io_object);
    if (os_status != OsSuccess) {
        ERROR("[socket] inherit failed with code %u", os_status);
        (void)OsStatusToErrno(os_status);
        io_object->ops.close(io_object, 0);
        stdio_handle_destroy(io_object, 0);
        return -1;
    }
    TRACE("[socket] done");
    return io_object->fd;
}
