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

#include <errno.h>
#include <internal/_io.h>
#include <internal/_ipc.h>
#include <inet/local.h>
#include <os/mollenos.h>

int getpeername(int iod, struct sockaddr* address_out, socklen_t* address_length_out)
{
    struct vali_link_message msg    = VALI_MSG_INIT_HANDLE(GetNetService());
    stdio_handle_t*          handle = stdio_handle_get(iod);
    oserr_t               status;
    
    if (!handle) {
        _set_errno(EBADF);
        return -1;
    }
    
    if (!address_out || !address_length_out) {
        _set_errno(EINVAL);
        return -1;
    }
    
    if (handle->object.type != STDIO_HANDLE_SOCKET) {
        _set_errno(ENOTSOCK);
        return -1;
    }
    
    sys_socket_get_address(GetGrachtClient(), &msg.base, handle->object.handle, SYS_ADDRESS_TYPE_PEER);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_socket_get_address_result(GetGrachtClient(), &msg.base, &status, (uint8_t*)address_out, *address_length_out);
    if (status != OS_EOK) {
        OsErrToErrNo(status);
        return -1;
    }
    
    *address_length_out = address_out->sa_len;
    return 0;
}
