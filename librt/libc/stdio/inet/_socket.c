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
#define __TRACE

#include <svc_socket_protocol_client.h>
#include <ddk/service.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <internal/_io.h>
#include <internal/_utils.h>
#include <os/mollenos.h>
#include <stdlib.h>

int socket_create(int domain, int type, int protocol, UUId_t handle, 
    UUId_t send_handle, UUId_t recv_handle)
{
    stdio_handle_t* ioObject;
    int             status;
    OsStatus_t      osStatus;
    TRACE("[socket] creating from handle %u", LODWORD(handle));
    
    status = stdio_handle_create(-1, WX_OPEN | WX_PIPE, &ioObject);
    if (status) {
        struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
        
        ERROR("[socket] stdio_handle_create failed with code %u", status);
        svc_socket_close(GetGrachtClient(), &msg.base, handle, SVC_SOCKET_CLOSE_OPTIONS_DESTROY);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GetGrachtBuffer());
        svc_socket_close_result(GetGrachtClient(), &msg.base, &osStatus);
        return -1;
    }
    
    TRACE("[socket] initializing libc structures");
    stdio_handle_set_handle(ioObject, handle);
    stdio_handle_set_ops_type(ioObject, STDIO_HANDLE_SOCKET);
    
    ioObject->object.data.socket.domain   = domain;
    ioObject->object.data.socket.type     = type;
    ioObject->object.data.socket.protocol = protocol;
    
    ioObject->object.data.socket.send_buffer.handle = send_handle;
    ioObject->object.data.socket.recv_buffer.handle = recv_handle;
    
    TRACE("[socket] mapping pipes");
    osStatus = ioObject->ops.inherit(ioObject);
    if (osStatus != OsSuccess) {
        (void)OsStatusToErrno(osStatus);
        ioObject->ops.close(ioObject, 0);
        stdio_handle_destroy(ioObject, 0);
        return -1;
    }
    TRACE("[socket] done %i", ioObject->fd);
    return ioObject->fd;
}
