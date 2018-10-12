/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * MollenOS - File loader utility
 *   - Utility implementation for using the file service to load a file.
 */
#define __MODULE "LOAD"
//#define __TRACE

#include <memorybuffer.h>
#include <os/file.h>
#include <handle.h>
#include <debug.h>
#include <heap.h>

/* LoadFile
 * Helper for the kernel to interact with file services. Loads a file and returns
 * the size and a pre-allocated buffer. */
OsStatus_t
LoadFile(
    _In_  const char*   Path,
    _Out_ char**        FullPath,
    _Out_ void**        Data,
    _Out_ size_t*       Length)
{
    FileSystemCode_t    FsCode;
    UUId_t              fHandle;
    DmaBuffer_t         TransferBuffer  = { 0 };
    LargeInteger_t      QueriedSize     = { { 0 } };
    void*               fBuffer         = NULL;
    size_t fRead = 0, fIndex = 0;
    size_t fSize = 0;

    // Open the file as read-only
    FsCode = OpenFile(Path, 0, __FILE_READ_ACCESS, &fHandle);
    if (FsCode != FsOk) {
        ERROR("Invalid path given: %s", Path);
        return OsError;
    }

    if (GetFileSize(fHandle, &QueriedSize.u.LowPart, NULL) != OsSuccess) {
        ERROR("Failed to retrieve the file size");
        CloseFile(fHandle);
        return OsError;
    }

    if (FullPath != NULL) {
        *FullPath = (char*)kmalloc(_MAXPATH);
        memset((void*)*FullPath, 0, _MAXPATH);
        if (GetFilePath(fHandle, *FullPath, _MAXPATH) != OsSuccess) {
            ERROR("Failed to query file handle for full path");
            kfree((void*)*FullPath);
            CloseFile(fHandle);
            return OsError;
        }
    }

    fSize = (size_t)QueriedSize.QuadPart;
    if (fSize != 0) {
        if (CreateMemoryBuffer(MEMORY_BUFFER_KERNEL, fSize, &TransferBuffer) != OsSuccess) {
            ERROR("Failed to create a memory buffer");
            CloseFile(fHandle);
            return OsError;
        }
        
        fBuffer = kmalloc(fSize);
        if (fBuffer == NULL) {
            ERROR("Failed to allocate resources for file-loading");
            CloseFile(fHandle);
            return OsError;
        }

        FsCode = ReadFile(fHandle, TransferBuffer.Handle, fSize, &fIndex, &fRead);
        if (FsCode != FsOk) {
            ERROR("Failed to read file, code %i", FsCode);
            kfree(fBuffer);
            CloseFile(fHandle);
            return OsError;
        }
        memcpy(fBuffer, (const void*)TransferBuffer.Address, fRead);

        // Cleanup by removing the memory mappings and freeing the
        // physical space allocated.
        RemoveSystemMemoryMapping(GetCurrentSystemMemorySpace(), TransferBuffer.Address, TransferBuffer.Capacity);
        DestroyHandle(TransferBuffer.Handle);
    }
    CloseFile(fHandle);
    *Data   = fBuffer;
    *Length = fSize;
    return OsSuccess;
}
