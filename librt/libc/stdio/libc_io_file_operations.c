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
#include <ddk/utils.h>

#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "libc_io.h"
#include "../threads/tls.h"

OsStatus_t stdio_file_op_read(stdio_handle_t* handle, const void* buffer, size_t length, size_t* bytes_read)
{
    uint8_t *Pointer        = (uint8_t*)buffer;
    size_t BytesReadTotal   = 0, BytesLeft = length;
    size_t OriginalSize     = GetBufferSize(tls_current()->transfer_buffer);

    // There is a time when reading more than a couple of times is considerably slower
    // than just reading the entire thing at once. When? Who knows, but in our case anything
    // more than 5 transfers is useless
    if (length >= (OriginalSize * 5)) {
        DmaBuffer_t *TransferBuffer     = CreateBuffer(UUID_INVALID, length);
        size_t BytesReadFs              = 0, BytesIndex = 0;
        FileSystemCode_t FsCode;

        FsCode = ReadFile(handle->InheritationHandle, GetBufferHandle(TransferBuffer), length, &BytesIndex, &BytesReadFs);
        if (_fval(FsCode) || BytesReadFs == 0) {
            DestroyBuffer(TransferBuffer);
            if (BytesReadFs == 0) {
                *bytes_read = 0;
                return OsSuccess;
            }
            return OsError;
        }

        SeekBuffer(TransferBuffer, BytesIndex);
        ReadBuffer(TransferBuffer, buffer, BytesReadFs, NULL);
        DestroyBuffer(TransferBuffer);
        *bytes_read = BytesReadFs;
        return OsSuccess;
    }
    
    // Keep reading chunks untill we've read all requested
    while (BytesLeft > 0) {
        FileSystemCode_t FsCode = FsOk;
        size_t ChunkSize        = MIN(OriginalSize, BytesLeft);
        size_t BytesReadFs      = 0, BytesIndex = 0;

        // Perform the read
        FsCode = ReadFile(handle->InheritationHandle, GetBufferHandle(tls_current()->transfer_buffer), 
            ChunkSize, &BytesIndex, &BytesReadFs);
        if (_fval(FsCode) || BytesReadFs == 0) {
            break;
        }
        
        // Seek to the valid buffer index, then read the byte count
        SeekBuffer(tls_current()->transfer_buffer, BytesIndex);
        ReadBuffer(tls_current()->transfer_buffer, (const void*)Pointer, BytesReadFs, NULL);
        SeekBuffer(tls_current()->transfer_buffer, 0);

        BytesLeft       -= BytesReadFs;
        BytesReadTotal  += BytesReadFs;
        Pointer         += BytesReadFs;
    }

    // Restore transfer buffer
    *bytes_read = BytesReadTotal;
    return OsSuccess;
}

OsStatus_t stdio_file_op_write(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytes_written)
{
    size_t BytesWrittenTotal = 0, BytesLeft = length;
    size_t OriginalSize = GetBufferSize(tls_current()->transfer_buffer);
    uint8_t *Pointer = (uint8_t*)buffer;

    // Keep writing chunks untill we've read all requested
    while (BytesLeft > 0) {
        size_t ChunkSize = MIN(OriginalSize, BytesLeft);
        size_t BytesWrittenLocal = 0;
        
        SeekBuffer(tls_current()->transfer_buffer, 0); // Rewind buffer
        WriteBuffer(tls_current()->transfer_buffer, (const void *)Pointer, ChunkSize, &BytesWrittenLocal);
        if (WriteFile(handle->InheritationHandle, GetBufferHandle(tls_current()->transfer_buffer), 
            ChunkSize, &BytesWrittenLocal) != FsOk) {
            break;
        }
        if (BytesWrittenLocal == 0) {
            break;
        }
        BytesWrittenTotal += BytesWrittenLocal;
        BytesLeft         -= BytesWrittenLocal;
        Pointer           += BytesWrittenLocal;
    }
    *bytes_written = BytesWrittenTotal;
    return OsSuccess;
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
