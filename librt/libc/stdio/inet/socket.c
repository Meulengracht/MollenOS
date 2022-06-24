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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Standard C Support
 * - Standard Socket IO Implementation
 */
#define __TRACE

#include <ddk/utils.h>
#include <internal/_io.h>
#include <internal/_ipc.h>
#include <os/mollenos.h>
#include <stdlib.h>

int socket(int domain, int type, int protocol)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    OsStatus_t               os_status;
    UUId_t                   handle;
    UUId_t                   send_handle;
    UUId_t                   recv_handle;
    int                      fd;
    
    // We need to create the socket object at kernel level, as we need
    // kernel assisted functionality to support a centralized storage of
    // all system sockets. They are the foundation of the microkernel for
    // communication between processes and are needed long before anything else.
    TRACE("[socket] remote create");
    sys_socket_create(GetGrachtClient(), &msg.base, domain, type, protocol);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_socket_create_result(GetGrachtClient(), &msg.base, &os_status, &handle, &recv_handle, &send_handle);
    if (os_status != OsOK) {
        ERROR("[socket] CreateSocket failed with code %u", os_status);
        (void)OsStatusToErrno(os_status);
        return -1;
    }
    
    fd = socket_create(domain, type, protocol, handle, send_handle, recv_handle);
    if (fd == -1) {
        ERROR("[socket] socket_create failed");
        return -1;
    }
    return fd;
}
