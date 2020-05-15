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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * C Standard Library
 * - Standard IO file operation implementations.
 */
//#define __TRACE

#include <assert.h>
#include <ddk/utils.h>
#include <errno.h>
#include <internal/_io.h>
#include <internal/_ipc.h>
#include <io.h>
#include <os/mollenos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../threads/tls.h"

static inline OsStatus_t
perform_transfer(UUId_t file_handle, UUId_t buffer_handle, int direction, 
    size_t chunkSize, off_t offset, size_t length, size_t* bytesTransferreOut)
{
    struct vali_link_message msg       = VALI_MSG_INIT_HANDLE(GetFileService());
    size_t                   bytesLeft = length;
    OsStatus_t               status;
    TRACE("[libc] [file-io] [perform_transfer] length %" PRIuIN, length);
    
    // Keep reading chunks untill we've read all requested
    while (bytesLeft > 0) {
        size_t bytesToTransfer = MIN(chunkSize, bytesLeft);
        size_t bytesTransferred;

        TRACE("[libc] [file-io] [perform_transfer] chunk size %" PRIuIN ", offset %" PRIuIN,
            bytesToTransfer, offset);
        svc_file_transfer(GetGrachtClient(), &msg, *GetInternalProcessId(),
            file_handle, direction, buffer_handle, offset, bytesToTransfer,
            &status, &bytesTransferred);
        gracht_vali_message_finish(&msg);
        
        TRACE("[libc] [file-io] [perform_transfer] bytes read %" PRIuIN ", status %u",
            bytesTransferred, status);
        if (status != OsSuccess || bytesTransferred == 0) {
            break;
        }

        bytesLeft -= bytesTransferred;
        offset    += bytesTransferred;
    }
    
    *bytesTransferreOut = length - bytesLeft;
    return status;
}

OsStatus_t stdio_file_op_read(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytesReadOut)
{
    UUId_t     builtinHandle = tls_current()->transfer_buffer.handle;
    size_t     builtinLength = tls_current()->transfer_buffer.length;
    size_t     bytesRead;
    OsStatus_t status;
    
    // There is a time when reading more than a couple of times is considerably slower
    // than just reading the entire thing at once. 
    if (length >= builtinLength) {
        struct dma_buffer_info info;
        struct dma_attachment  attachment;
        
        // enforce dword alignment on the buffer
        assert(((uintptr_t)buffer % 0x4) == 0);
        
        info.name     = "stdio_transfer";
        info.length   = length;
        info.capacity = length;
        info.flags    = DMA_PERSISTANT;
        
        status = dma_export(buffer, &info, &attachment);
        if (status != OsSuccess) {
            return status;
        }
        
        // Pass the callers pointer directly here
        status = perform_transfer(handle->object.handle, attachment.handle,
            0, length, 0, length, bytesReadOut);
        dma_detach(&attachment);
        return status;
    }
    
    status = perform_transfer(handle->object.handle, builtinHandle, 0,
        builtinLength, 0, length, &bytesRead);
    if (status == OsSuccess && bytesRead > 0) {
        memcpy(buffer, tls_current()->transfer_buffer.buffer, bytesRead);
    }
    
    *bytesReadOut = bytesRead;
    return status;
}

OsStatus_t stdio_file_op_write(stdio_handle_t* handle, const void* buffer,
    size_t length, size_t* bytesWrittenOut)
{
    UUId_t     builtinHandle = tls_current()->transfer_buffer.handle;
    size_t     builtinLength = tls_current()->transfer_buffer.length;
    OsStatus_t status;
    
    // There is a time when reading more than a couple of times is considerably slower
    // than just reading the entire thing at once. 
    if (length >= builtinLength) {
        struct dma_buffer_info info;
        struct dma_attachment  attachment;
        
        // enforce dword alignment on the buffer
        assert(((uintptr_t)buffer % 0x4) == 0);
        
        info.length   = length;
        info.capacity = length;
        info.flags    = DMA_PERSISTANT;
        
        status = dma_export((void*)buffer, &info, &attachment);
        if (status != OsSuccess) {
            return status;
        }
        
        status = perform_transfer(handle->object.handle, attachment.handle,
            1, length, 0, length, bytesWrittenOut);
        dma_detach(&attachment);
        return status;
    }
    
    memcpy(tls_current()->transfer_buffer.buffer, buffer, length);
    status = perform_transfer(handle->object.handle, builtinHandle, 1,
        builtinLength, 0, length, bytesWrittenOut);
    return status;
}

OsStatus_t stdio_file_op_seek(stdio_handle_t* handle, int origin, off64_t offset, long long* position_out)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    OsStatus_t               status;
    LargeInteger_t           SeekFinal;

    // If we search from SEEK_SET, just build offset directly
    if (origin != SEEK_SET) {
        LargeInteger_t FileInitial;

        // Adjust for seek origin
        if (origin == SEEK_CUR) {
            svc_file_get_position(GetGrachtClient(), &msg, *GetInternalProcessId(),
                handle->object.handle, &status, &FileInitial.u.LowPart, &FileInitial.u.HighPart);
            gracht_vali_message_finish(&msg);
            if (status != OsSuccess) {
                ERROR("failed to get file position");
                return status;
            }

            // Sanitize for overflow
            if ((size_t)FileInitial.QuadPart != FileInitial.QuadPart) {
                ERROR("file-offset-overflow");
                _set_errno(EOVERFLOW);
                return OsError;
            }
        }
        else {
            svc_file_get_size(GetGrachtClient(), &msg, *GetInternalProcessId(),
                handle->object.handle, &status, &FileInitial.u.LowPart, &FileInitial.u.HighPart);
            gracht_vali_message_finish(&msg);
            if (status != OsSuccess) {
                ERROR("failed to get file size");
                return status;
            }
        }
        SeekFinal.QuadPart = FileInitial.QuadPart + offset;
    }
    else {
        SeekFinal.QuadPart = offset;
    }

    // Now perform the seek
    svc_file_seek(GetGrachtClient(), &msg, *GetInternalProcessId(),
        handle->object.handle, SeekFinal.u.LowPart, SeekFinal.u.HighPart, &status);
    gracht_vali_message_finish(&msg);
    if (status == OsSuccess) {
        *position_out = SeekFinal.QuadPart;
        return OsSuccess;
    }
    TRACE("stdio::fseek::fail %u", status);
    return status;
}

OsStatus_t stdio_file_op_resize(stdio_handle_t* handle, long long resize_by)
{
    return OsNotSupported;
}

OsStatus_t stdio_file_op_close(stdio_handle_t* handle, int options)
{
    struct vali_link_message msg    = VALI_MSG_INIT_HANDLE(GetFileService());
	OsStatus_t               status = OsSuccess;
	
	if (options & STDIO_CLOSE_FULL) {
        svc_file_close(GetGrachtClient(), &msg, *GetInternalProcessId(),
            handle->object.handle, &status);
        gracht_vali_message_finish(&msg);
	}
    return status;
}

OsStatus_t stdio_file_op_inherit(stdio_handle_t* handle)
{
    return OsSuccess;
}

void stdio_get_file_operations(stdio_ops_t* ops)
{
    ops->inherit = stdio_file_op_inherit;
    ops->read    = stdio_file_op_read;
    ops->write   = stdio_file_op_write;
    ops->seek    = stdio_file_op_seek;
    ops->resize  = stdio_file_op_resize;
    ops->close   = stdio_file_op_close;
}
