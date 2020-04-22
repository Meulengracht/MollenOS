/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS - C Standard Library
 * - Deletes the file specified by the path
 */

#include <ddk/protocols/svc_file_protocol_client.h>
#include <ddk/service.h>
#include <errno.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <io.h>
#include <os/mollenos.h>
#include <stdio.h>

int unlink(
	_In_ const char *path)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    int                      status;
    OsStatus_t               osStatus;
        
    if (path == NULL) {
    	_set_errno(EINVAL);
    	return EOF;
    }
    
    status = svc_file_delete_sync(GetGrachtClient(), &msg, *GetInternalProcessId(),
        path, 0, &osStatus);
    gracht_vali_message_finish(&msg);
    if (status || OsStatusToErrno(osStatus)) {
    	return -1;
    }
    return 0;
}

int remove(const char * filename) {
    return unlink(filename);
}
