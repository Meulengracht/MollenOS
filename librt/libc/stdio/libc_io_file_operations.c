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

#include <ddk/services/file.h> // for ipc
#include <os/services/file.h>
#include <os/mollenos.h>
#include <ddk/utils.h>

#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "libc_io.h"
#include "../threads/tls.h"

static inline int
perform_transfer(UUId_t file_handle, UUId_t buffer_handle, int direction, 
    size_t chunk_size, off_t offset, size_t length, size_t* total_bytes)
{
    size_t bytes_left = length;
    int    err_code;
    
    // Keep reading chunks untill we've read all requested
    while (bytes_left > 0) {
        FileSystemCode_t fs_code;
        size_t           bytes_to_transfer = MIN(chunk_size, bytes_left);
        size_t           bytes_transferred;

        fs_code = TransferFile(file_handle, buffer_handle, direction, offset, bytes_to_transfer, &bytes_transferred);
        err_code = _fval(fs_code);
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
        UUId_t buffer_handle;
        
        status = MemoryShare(buffer, length, &buffer_handle);
        if (status != OsSuccess) {
            return status;
        }
        
        err_code = perform_transfer(handle->InheritationHandle, buffer_handle, 0, length, 0, length, bytes_read);
        MemoryUnshare(buffer_handle);
        return err_code == EOK ? OsSuccess : OsError;
    }
    
    err_code = perform_transfer(handle->InheritationHandle, builtin_handle, 0, builtin_length, 0, length, bytes_read);
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
        UUId_t buffer_handle;
        
        status = MemoryShare(buffer, length, &buffer_handle);
        if (status != OsSuccess) {
            return status;
        }
        
        err_code = perform_transfer(handle->InheritationHandle, buffer_handle, 1, length, 0, length, bytes_written);
        MemoryUnshare(buffer_handle);
        return err_code == EOK ? OsSuccess : OsError;
    }
    
    memcpy(tls_current()->transfer_buffer.buffer, buffer, length);
    err_code = perform_transfer(handle->InheritationHandle, builtin_handle, 1, builtin_length, 0, length, bytes_written);
    return err_code == EOK ? OsSuccess : OsError;
}

OsStatus_t stdio_file_op_seek(stdio_handle_t* handle, int origin, off64_t offset, long long* position_out)
{
    FileSystemCode_t    FsStatus;
    LargeInteger_t      SeekFinal;
    OsStatus_t          Status;

    // If we search from SEEK_SET, just build offset directly
    if (origin != SEEK_SET) {
        LargeInteger_t FileInitial;

        // Adjust for seek origin
        if (origin == SEEK_CUR) {
            Status = GetFilePosition(handle->InheritationHandle, &FileInitial.u.LowPart, &FileInitial.u.HighPart);
            if (Status != OsSuccess) {
                ERROR("failed to get file position");
                return OsError;
            }

            // Sanitize for overflow
            if ((size_t)FileInitial.QuadPart != FileInitial.QuadPart) {
                ERROR("file-offset-overflow");
                _set_errno(EOVERFLOW);
                return OsError;
            }
        }
        else {
            Status = GetFileSize(handle->InheritationHandle, &FileInitial.u.LowPart, &FileInitial.u.HighPart);
            if (Status != OsSuccess) {
                ERROR("failed to get file size");
                return OsError;
            }
        }
        SeekFinal.QuadPart = FileInitial.QuadPart + offset;
    }
    else {
        SeekFinal.QuadPart = offset;
    }

    // Now perform the seek
    FsStatus = SeekFile(handle->InheritationHandle, SeekFinal.u.LowPart, SeekFinal.u.HighPart);
    if (!_fval((int)FsStatus)) {
        *position_out = SeekFinal.QuadPart;
        return OsSuccess;
    }
    TRACE("stdio::fseek::fail %u", FsStatus);
    return OsError;
}

OsStatus_t stdio_file_op_resize(stdio_handle_t* handle, long long resize_by)
{
    return OsNotSupported;
}

OsStatus_t stdio_file_op_close(stdio_handle_t* handle, int options)
{
	int        result    = (int)CloseFile(handle->InheritationHandle);
	OsStatus_t converted = OsSuccess;
    if (_fval(result)) {
        result    = -1;
        converted = OsError;
    }
    return converted;
}

void stdio_get_file_operations(stdio_ops_t* ops)
{
    ops->read   = stdio_file_op_read;
    ops->write  = stdio_file_op_write;
    ops->seek   = stdio_file_op_seek;
    ops->resize = stdio_file_op_resize;
    ops->close  = stdio_file_op_close;
}
