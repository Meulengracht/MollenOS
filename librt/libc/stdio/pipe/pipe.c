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

#include <ddk/utils.h>
#include <internal/_io.h>
#include <io.h>
#include <os/dmabuf.h>
#include <os/mollenos.h>
#include <string.h>

// Convert O_* flags to WX_* flags
static unsigned int __convert_o_to_wx_flags(unsigned int oflags)
{
    unsigned int wxflags = WX_PIPE | WX_APPEND;
    unsigned int unsupp;

    // detect options
    if (oflags & O_NOINHERIT) wxflags |= WX_DONTINHERIT;

    // detect mode, default to binary
    if (oflags & O_BINARY)       {/* Nothing to do */}
    else if (oflags & O_TEXT)    wxflags |= WX_TEXT;
    else if (oflags & O_WTEXT)   wxflags |= WX_WIDE;
    else if (oflags & O_U16TEXT) wxflags |= WX_UTF16;
    else if (oflags & O_U8TEXT)  wxflags |= WX_UTF;

    if ((unsupp = oflags & ~(
            O_BINARY|O_TEXT|O_APPEND|
            O_TRUNC|O_EXCL|O_CREAT|
            O_RDWR|O_WRONLY|O_TEMPORARY|
            O_NOINHERIT|
            O_SEQUENTIAL|O_RANDOM|O_SHORT_LIVED|
            O_WTEXT|O_U16TEXT|O_U8TEXT
    ))) {
        TRACE(STR("[pipe] [__convert_o_to_wx_flags] unsupported oflags 0x%x"), unsupp);
    }
    return wxflags;
}

int pipe(long size, int flags)
{
    stdio_handle_t*        ioObject;
    OsStatus_t             osStatus;
    int                    status;
    struct dma_buffer_info bufferInfo;
    struct dma_attachment  attachment;
    unsigned int           wxflags = __convert_o_to_wx_flags(flags);

    // create the dma attachment
    bufferInfo.name     = "libc_pipe";
    bufferInfo.length   = size;
    bufferInfo.capacity = size;
    bufferInfo.flags    = 0;

    osStatus = dma_create(&bufferInfo, &attachment);
    if (osStatus != OsSuccess) {
        return OsStatusToErrno(osStatus);
    }

    streambuffer_construct(
        attachment.buffer,
        size - sizeof(struct streambuffer),
        STREAMBUFFER_MULTIPLE_WRITERS | STREAMBUFFER_GLOBAL);

    status = stdio_handle_create(-1, WX_OPEN | wxflags, &ioObject);
    if (status) {
        dma_detach(&attachment);
        return status;
    }

    stdio_handle_set_handle(ioObject, attachment.handle);
    stdio_handle_set_ops_type(ioObject, STDIO_HANDLE_PIPE);
    
    memcpy(&ioObject->object.data.pipe.attachment, &attachment,
        sizeof(struct dma_attachment));
    ioObject->object.data.pipe.options = STREAMBUFFER_ALLOW_PARTIAL;
    return ioObject->fd;
}
