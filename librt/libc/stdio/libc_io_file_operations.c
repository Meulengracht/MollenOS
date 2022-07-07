/* MollenOS
 *
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
 *
 * C Standard Library
 * - Standard IO file operation implementations.
 */

//#define __TRACE

#include <ddk/utils.h>
#include <errno.h>
#include <internal/_io.h>
#include <internal/_ipc.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../threads/tls.h"

static inline oscode_t
perform_transfer(uuid_t file_handle, uuid_t buffer_handle, int direction,
                 size_t chunkSize, off_t offset, size_t length, size_t* bytesTransferreOut)
{
    struct vali_link_message msg       = VALI_MSG_INIT_HANDLE(GetFileService());
    size_t                   bytesLeft = length;
    oscode_t               status;
    TRACE("[libc] [file-io] [perform_transfer] length %" PRIuIN, length);
    
    // Keep reading chunks untill we've read all requested
    while (bytesLeft > 0) {
        size_t bytesToTransfer = MIN(chunkSize, bytesLeft);
        size_t bytesTransferred;

        TRACE("[libc] [file-io] [perform_transfer] chunk size %" PRIuIN ", offset %" PRIuIN,
            bytesToTransfer, offset);
        sys_file_transfer(GetGrachtClient(), &msg.base, *__crt_processid_ptr(),
            file_handle, direction, buffer_handle, offset, bytesToTransfer);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
        sys_file_transfer_result(GetGrachtClient(), &msg.base, &status, &bytesTransferred);
        
        TRACE("[libc] [file-io] [perform_transfer] bytes read %" PRIuIN ", status %u",
            bytesTransferred, status);
        if (status != OsOK || bytesTransferred == 0) {
            break;
        }

        bytesLeft -= bytesTransferred;
        offset    += bytesTransferred;
    }
    
    *bytesTransferreOut = length - bytesLeft;
    return status;
}

oscode_t stdio_file_op_read(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytesReadOut)
{
    uuid_t     builtinHandle = tls_current()->transfer_buffer.handle;
    size_t     builtinLength = tls_current()->transfer_buffer.length;
    size_t     bytesRead;
    oscode_t status;
    TRACE("stdio_file_op_read(buffer=0x%" PRIxIN ", length=%" PRIuIN ")", buffer, length);
    
    // There is a time when reading more than a couple of times is considerably slower
    // than just reading the entire thing at once. 
    if (length > builtinLength) {
        struct dma_buffer_info info;
        struct dma_attachment  attachment;
        void*                  adjustedPointer = (void*)buffer;
        size_t                 adjustedLength  = length;

        // enforce dword alignment on the buffer
        // which means if someone passes us a byte or word aligned
        // buffer we must account for that
        if ((uintptr_t)buffer & 0x3) {
            size_t bytesToAlign = 4 - ((uintptr_t)buffer & 0x3);
            status = stdio_file_op_read(handle, buffer, bytesToAlign, bytesReadOut);
            if (status != OsOK) {
                return status;
            }
            adjustedPointer = (void*)((uintptr_t)buffer + bytesToAlign);
            adjustedLength -= bytesToAlign;
        }
        
        info.name     = "stdio_transfer";
        info.length   = adjustedLength;
        info.capacity = adjustedLength;
        info.flags    = DMA_PERSISTANT;
        info.type     = DMA_TYPE_DRIVER_32;
        
        status = dma_export(adjustedPointer, &info, &attachment);
        if (status != OsOK) {
            return status;
        }
        
        // Pass the callers pointer directly here
        status = perform_transfer(handle->object.handle, attachment.handle,
            0, adjustedLength, 0, adjustedLength, bytesReadOut);
        if (*bytesReadOut == adjustedLength) {
            *bytesReadOut = length;
        }

        dma_detach(&attachment);
        return status;
    }
    
    status = perform_transfer(handle->object.handle, builtinHandle, 0,
        builtinLength, 0, length, &bytesRead);
    if (status == OsOK && bytesRead > 0) {
        memcpy(buffer, tls_current()->transfer_buffer.buffer, bytesRead);
    }
    
    *bytesReadOut = bytesRead;
    return status;
}

oscode_t stdio_file_op_write(stdio_handle_t* handle, const void* buffer,
                             size_t length, size_t* bytesWrittenOut)
{
    uuid_t     builtinHandle = tls_current()->transfer_buffer.handle;
    size_t     builtinLength = tls_current()->transfer_buffer.length;
    oscode_t status;
    TRACE("stdio_file_op_write(buffer=0x%" PRIxIN ", length=%" PRIuIN ")", buffer, length);
    
    // There is a time when reading more than a couple of times is considerably slower
    // than just reading the entire thing at once. 
    if (length > builtinLength) {
        struct dma_buffer_info info;
        struct dma_attachment  attachment;
        void*                  adjustedPointer = (void*)buffer;
        size_t                 adjustedLength  = length;
        
        // enforce dword alignment on the buffer
        // which means if someone passes us a byte or word aligned
        // buffer we must account for that
        if ((uintptr_t)buffer & 0x3) {
            size_t bytesToAlign = 4 - ((uintptr_t)buffer & 0x3);
            status = stdio_file_op_write(handle, buffer, bytesToAlign, bytesWrittenOut);
            if (status != OsOK) {
                return status;
            }
            adjustedPointer = (void*)((uintptr_t)buffer + bytesToAlign);
            adjustedLength -= bytesToAlign;
        }

        
        info.length   = adjustedLength;
        info.capacity = adjustedLength;
        info.flags    = DMA_PERSISTANT;
        info.type     = DMA_TYPE_DRIVER_32;
        
        status = dma_export(adjustedPointer, &info, &attachment);
        if (status != OsOK) {
            return status;
        }
        
        status = perform_transfer(handle->object.handle, attachment.handle,
            1, adjustedLength, 0, adjustedLength, bytesWrittenOut);
        dma_detach(&attachment);
        if (*bytesWrittenOut == adjustedLength) {
            *bytesWrittenOut = length;
        }
        return status;
    }
    
    memcpy(tls_current()->transfer_buffer.buffer, buffer, length);
    status = perform_transfer(handle->object.handle, builtinHandle, 1,
        builtinLength, 0, length, bytesWrittenOut);
    return status;
}

oscode_t stdio_file_op_seek(stdio_handle_t* handle, int origin, off64_t offset, long long* position_out)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oscode_t               status;
    Integer64_t           seekFinal;
    TRACE("stdio_file_op_seek(origin=%i, offset=%" PRIiIN ")", origin, offset);

    // If we search from SEEK_SET, just build offset directly
    if (origin != SEEK_SET) {
        Integer64_t currentOffset;

        // Adjust for seek origin
        if (origin == SEEK_CUR) {
            sys_file_get_position(GetGrachtClient(), &msg.base, *__crt_processid_ptr(), handle->object.handle);
            gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
            sys_file_get_position_result(GetGrachtClient(), &msg.base, &status, &currentOffset.u.LowPart, &currentOffset.u.HighPart);
            if (status != OsOK) {
                ERROR("failed to get file position");
                return status;
            }

            // Sanitize for overflow
            if ((size_t)currentOffset.QuadPart != currentOffset.QuadPart) {
                ERROR("file-offset-overflow");
                _set_errno(EOVERFLOW);
                return OsError;
            }
        }
        else {
            sys_file_get_size(GetGrachtClient(), &msg.base, *__crt_processid_ptr(), handle->object.handle);
            gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
            sys_file_get_size_result(GetGrachtClient(), &msg.base, &status, &currentOffset.u.LowPart, &currentOffset.u.HighPart);
            if (status != OsOK) {
                ERROR("failed to get file size");
                return status;
            }
        }
        seekFinal.QuadPart = currentOffset.QuadPart + offset;
    }
    else {
        seekFinal.QuadPart = offset;
    }

    // no reason to invoke the service
    if (origin == SEEK_CUR && offset == 0) {
        *position_out = seekFinal.QuadPart;
        return OsOK;
    }

    // Now perform the seek
    sys_file_seek(GetGrachtClient(), &msg.base, *__crt_processid_ptr(),
                  handle->object.handle, seekFinal.u.LowPart, seekFinal.u.HighPart);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_file_seek_result(GetGrachtClient(), &msg.base, &status);
    if (status == OsOK) {
        *position_out = seekFinal.QuadPart;
        return OsOK;
    }
    TRACE("stdio::fseek::fail %u", status);
    *position_out = (off_t)-1;
    return status;
}

oscode_t stdio_file_op_resize(stdio_handle_t* handle, long long resize_by)
{
    return OsNotSupported;
}

oscode_t stdio_file_op_close(stdio_handle_t* handle, int options)
{
    struct vali_link_message msg    = VALI_MSG_INIT_HANDLE(GetFileService());
	oscode_t               status = OsOK;
	
	if (options & STDIO_CLOSE_FULL) {
        sys_file_close(GetGrachtClient(), &msg.base, *__crt_processid_ptr(), handle->object.handle);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
        sys_file_close_result(GetGrachtClient(), &msg.base, &status);
	}
    return status;
}

oscode_t stdio_file_op_inherit(stdio_handle_t* handle)
{
    return OsOK;
}

oscode_t stdio_file_op_ioctl(stdio_handle_t* handle, int request, va_list vlist)
{
    return OsNotSupported;
}

void stdio_get_file_operations(stdio_ops_t* ops)
{
    ops->inherit = stdio_file_op_inherit;
    ops->read    = stdio_file_op_read;
    ops->write   = stdio_file_op_write;
    ops->seek    = stdio_file_op_seek;
    ops->resize  = stdio_file_op_resize;
    ops->ioctl   = stdio_file_op_ioctl;
    ops->close   = stdio_file_op_close;
}
