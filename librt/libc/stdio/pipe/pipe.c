/* MollenOS
 *
 * Copyright 2020, Philip Meulengracht
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
 * C Standard Library
 * - Pipe function, opens a new pipe
 */

//#define __TRACE

#include <internal/_io.h>
#include <io.h>
#include <os/dmabuf.h>
#include <os/mollenos.h>

int pipe(long size, int flags)
{
    stdio_handle_t*        ioObject;
    OsStatus_t             osStatus;
    int                    status;
    struct dma_buffer_info bufferInfo;
    struct dma_attachment  attachment;

    // create the dma attachment
    bufferInfo.name     = "libc_pipe";
    bufferInfo.length   = size;
    bufferInfo.capacity = size;
    bufferInfo.flags    = 0;

    osStatus = dma_create(&bufferInfo, &attachment);
    if (osStatus) {
        return OsStatusToErrno(osStatus);
    }

    streambuffer_construct(
        attachment.buffer,
        size - sizeof(struct streambuffer),
        STREAMBUFFER_MULTIPLE_WRITERS | STREAMBUFFER_GLOBAL);

    status = stdio_handle_create(-1, WX_OPEN | WX_PIPE, &ioObject);
    if (status) {
        dma_detach(&attachment);
        return status;
    }

    stdio_handle_set_handle(ioObject, handle);
    stdio_handle_set_ops_type(ioObject, STDIO_HANDLE_PIPE);
    
    memcpy(&ioObject->object.data.pipe.attachment, &attachment,
        sizeof(struct dma_attachment));
    return ioObject->fd;
}
