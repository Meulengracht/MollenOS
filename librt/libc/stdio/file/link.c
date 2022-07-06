/**
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
 * Creates a new link between two files or two directories. The link can either
 * be symbolic or hard. It is up to the filesystem whether or not these types of
 * links are supported, and thus the error code should always be checked.
 * The function returns 0 if successful, or -1 on error.
 * 
 */

#include <errno.h>
#include <internal/_ipc.h>
#include <io.h>
#include <os/mollenos.h>

int link(
    _In_ const char* from, 
    _In_ const char* to, 
    _In_ int         symbolic)
{
    struct vali_link_message msg    = VALI_MSG_INIT_HANDLE(GetFileService());
    oscode_t               status;

    if (!from || !to) {
        return OsInvalidParameters;
    }
    
    sys_file_link(GetGrachtClient(), &msg.base, *__crt_processid_ptr(), from, to, symbolic);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_file_link_result(GetGrachtClient(), &msg.base, &status);
    return OsCodeToErrNo(status);
}
