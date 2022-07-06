/**
 * MollenOS
 *
 * Copyright 2021, Philip Meulengracht
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
 * C Standard Library
 * - File link implementation
 */

#include <ddk/service.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <internal/_io.h>
#include <internal/_utils.h>
#include <os/mollenos.h>
#include <stdio.h>

#include <sys_file_service_client.h>

FILE* tmpfile(void)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    int                      status;
    oscode_t               osStatus;
    stdio_handle_t*          object;
    UUId_t                   handle;
    char                     path[64];

    // generate a new path we can use as a temporary file
    sprintf(&path[0], "$share/%s",  tmpnam(NULL));
    status = sys_file_open(
        GetGrachtClient(), 
        &msg.base, 
        *__crt_processid_ptr(),
        &path[0], 
        __FILE_CREATE | __FILE_TEMPORARY | __FILE_FAILONEXIST | __FILE_BINARY, 
        __FILE_READ_ACCESS | __FILE_WRITE_ACCESS
    );

    if (status) {
        ERROR("open no communcation channel open");
        _set_errno(EPIPE);
        return NULL;
    }

    status = gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    if (status) {
        ERROR("open failed to wait for answer: %i", status);
        _set_errno(EPIPE);
        return NULL;
    }

    sys_file_open_result(GetGrachtClient(), &msg.base, &osStatus, &handle);
    if (OsCodeToErrNo(osStatus)) {
        ERROR("open(path=%s) failed with code: %u", &path[0], osStatus);
        return NULL;
    }

    TRACE("open retrieved handle %u", handle);
    if (stdio_handle_create(-1, WX_DONTINHERIT | WX_TEMP, &object)) {
        sys_file_close(GetGrachtClient(), &msg.base, *__crt_processid_ptr(), handle);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
        sys_file_close_result(GetGrachtClient(), &msg.base, &osStatus);
        return NULL;
    }
    
    stdio_handle_set_handle(object, handle);
    stdio_handle_set_ops_type(object, STDIO_HANDLE_FILE);

    return fdopen(object->fd, "wb+");
}
