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
#include <io.h>
#include <os/mollenos.h>

int socketpair(int domain, int type, int protocol, int* iods)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    stdio_handle_t*          io_object1;
    stdio_handle_t*          io_object2;
    OsStatus_t               status;
    
    if (!iods) {
        _set_errno(EINVAL);
        return -1;
    }
    
    iods[0] = socket(domain, type, protocol);
    if (iods[0] == -1) {
        return -1;
    }
    
    iods[1] = socket(domain, type, protocol);
    if (iods[1] == -1) {
        close(iods[1]);
        return -1;
    }
    
    io_object1 = stdio_handle_get(iods[0]);
    io_object2 = stdio_handle_get(iods[1]);
    
    sys_socket_pair(GetGrachtClient(), &msg.base, io_object1->object.handle,
        io_object2->object.handle);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_socket_pair_result(GetGrachtClient(), &msg.base, &status);
    if (status != OsSuccess) {
        (void)OsStatusToErrno(status);
        return -1;
    }
    return 0;
}
