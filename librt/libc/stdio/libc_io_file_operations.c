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

static inline int
perform_transfer(UUId_t file_handle, UUId_t buffer_handle, int direction, 
    size_t chunk_size, off_t offset, size_t length, size_t* total_bytes)
{
    struct vali_link_message msg        = VALI_MSG_INIT_HANDLE(GetFileService());
    size_t                   bytes_left = length;
    int                      err_code;
    OsStatus_t               status;
    
    // Keep reading chunks untill we've read all requested
    while (bytes_left > 0) {
        size_t bytes_to_transfer = MIN(chunk_size, bytes_left);
        size_t bytes_transferred;

        svc_file_transfer_sync(GetGrachtClient(), &msg, file_handle, direction, buffer_handle,
            offset, bytes_to_transfer, &status, &bytes_transferred);
        gracht_vali_message_finish(&msg);
        err_code = OsStatusToErrno(status);
        if (err_code || bytes_transferred == 0) {
            break;
        }

        bytes_left   -= bytes_transferred;
        *total_bytes += bytes_transferred;
        offset       += bytes_transferred;
    }
    return err_code;
}

OsStatus_t stdio_file_op_read(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytes_read)
{
    UUId_t     builtin_handle = tls_current()->transfer_buffer.handle;
    size_t     builtin_length = tls_current()->transfer_buffer.length;
    OsStatus_t status;
    int        err_code;
    
    // There is a time when reading more than a couple of times is considerably slower
    // than just reading the entire thing at once. 
    if (length >= builtin_length) {
        struct dma_buffer_info info;
        struct dma_attachment  attachment;
        
        // enforce dword alignment on the buffer
        assert(((uintptr_t)buffer % 0x4) == 0);
        
        info.length   = length;
        info.capacity = length;
        info.flags    = DMA_PERSISTANT;
        
        status = dma_export(buffer, &info, &attachment);
        if (status != OsSuccess) {
            return status;
        }
        
        err_code = perform_transfer(handle->object.handle, attachment.handle,
            0, length, 0, length, bytes_read);
        dma_detach(&attachment);
        return err_code == EOK ? OsSuccess : OsError;
    }
    
    err_code = perform_transfer(handle->object.handle, builtin_handle, 0, builtin_length, 0, length, bytes_read);
    memcpy(buffer, tls_current()->transfer_buffer.buffer, *bytes_read);
    return err_code == EOK ? OsSuccess : OsError;
}

OsStatus_t stdio_file_op_write(stdio_handle_t* handle, const void* buffer, size_t length, size_t* bytes_written)
{
    UUId_t     builtin_handle = tls_current()->transfer_buffer.handle;
    size_t     builtin_length = tls_current()->transfer_buffer.length;
    OsStatus_t status;
    int        err_code;
    
    // There is a time when reading more than a couple of times is considerably slower
    // than just reading the entire thing at once. 
    if (length >= builtin_length) {
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
        
        err_code = perform_transfer(handle->object.handle, attachment.handle,
            1, length, 0, length, bytes_written);
        dma_detach(&attachment);
        return err_code == EOK ? OsSuccess : OsError;
    }
    
    memcpy(tls_current()->transfer_buffer.buffer, buffer, length);
    err_code = perform_transfer(handle->object.handle, builtin_handle, 1, builtin_length, 0, length, bytes_written);
    return err_code == EOK ? OsSuccess : OsError;
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
            svc_file_get_position_sync(GetGrachtClient(), &msg, handle->object.handle,
                &status, &FileInitial.u.LowPart, &FileInitial.u.HighPart);
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
            svc_file_get_size_sync(GetGrachtClient(), &msg, handle->object.handle,
                &status, &FileInitial.u.LowPart, &FileInitial.u.HighPart);
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
    svc_file_seek_sync(GetGrachtClient(), &msg, handle->object.handle,
        SeekFinal.u.LowPart, SeekFinal.u.HighPart, &status);
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
        svc_file_close_sync(GetGrachtClient(), &msg, handle->object.handle, &status);
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
