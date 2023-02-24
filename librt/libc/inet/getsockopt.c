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

#include "internal/_io.h"
#include "internal/_ipc.h"
#include "inet/local.h"
#include "errno.h"
#include "os/mollenos.h"

int getsockopt(int iod, int protocol, int option, void* data, socklen_t* length_out)
{
    struct vali_link_message msg    = VALI_MSG_INIT_HANDLE(GetNetService());
    stdio_handle_t*          handle = stdio_handle_get(iod);
    oserr_t               status;
    
    if (!handle) {
        _set_errno(EBADF);
        return -1;
    }
    
    if (handle->object.type != STDIO_HANDLE_SOCKET) {
        _set_errno(ENOTSOCK);
        return -1;
    }
    
    sys_socket_get_option(GetGrachtClient(), &msg.base, handle->object.handle, protocol, option);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_socket_get_option_result(GetGrachtClient(), &msg.base, &status, data, (uint32_t)(*length_out), length_out);
    if (status != OS_EOK) {
        OsErrToErrNo(status);
        return -1;
    }
    return 0;
}
