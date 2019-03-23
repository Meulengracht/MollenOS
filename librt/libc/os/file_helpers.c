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
 * File Definitions & Structures
 * - This header describes the base file-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>

#include <os/services/file.h>
#include <os/services/storage.h>
#include <os/mollenos.h>
#include <stdio.h>
#include "../stdio/local.h"

OsStatus_t
GetFilePathFromFd(
    _In_ int    FileDescriptor,
    _In_ char*  PathBuffer,
    _In_ size_t MaxLength)
{
    StdioHandle_t *FileHandle = StdioFdToHandle(FileDescriptor);

    if (FileHandle == NULL || PathBuffer == NULL || 
        FileHandle->InheritationType != STDIO_HANDLE_FILE) {
        return OsError;
    }
    return GetFilePath(FileHandle->InheritationHandle, PathBuffer, MaxLength);
}

OsStatus_t
GetStorageInformationFromPath(
    _In_ const char*            Path,
    _In_ OsStorageDescriptor_t* Information)
{
    if (Information == NULL || Path == NULL) {
        return OsError;
    }
    return QueryStorageByPath(Path, Information);
}

OsStatus_t
GetStorageInformationFromFd(
    _In_ int                    FileDescriptor,
    _In_ OsStorageDescriptor_t* Information)
{
    StdioHandle_t *FileHandle = StdioFdToHandle(FileDescriptor);

    if (FileHandle == NULL || Information == NULL ||
        FileHandle->InheritationType != STDIO_HANDLE_FILE) {
        return OsError;
    }
    return QueryStorageByHandle(FileHandle->InheritationHandle, Information);
}

FileSystemCode_t
GetFileSystemInformationFromPath(
    _In_ const char *Path,
    _In_ OsFileSystemDescriptor_t *Information)
{
    if (Information == NULL || Path == NULL) {
        return FsInvalidParameters;
    }
    return GetFileSystemStatsByPath(Path, Information);
}

FileSystemCode_t
GetFileSystemInformationFromFd(
    _In_ int FileDescriptor,
    _In_ OsFileSystemDescriptor_t *Information)
{
    StdioHandle_t *FileHandle = StdioFdToHandle(FileDescriptor);

    if (FileHandle == NULL || Information == NULL ||
        FileHandle->InheritationType != STDIO_HANDLE_FILE) {
        return FsInvalidParameters;
    }
    return GetFileSystemStatsByHandle(FileHandle->InheritationHandle, Information);
}

FileSystemCode_t
GetFileInformationFromPath(
    _In_ const char*            Path,
    _In_ OsFileDescriptor_t*    Information)
{
    if (Information == NULL || Path == NULL) {
        return FsInvalidParameters;
    }
    return GetFileStatsByPath(Path, Information);
}

FileSystemCode_t
GetFileInformationFromFd(
    _In_ int                    FileDescriptor,
    _In_ OsFileDescriptor_t*    Information)
{
    StdioHandle_t *FileHandle = StdioFdToHandle(FileDescriptor);

    if (FileHandle == NULL || Information == NULL ||
        FileHandle->InheritationType != STDIO_HANDLE_FILE) {
        return FsInvalidParameters;
    }
    return GetFileStatsByHandle(FileHandle->InheritationHandle, Information);
}

OsStatus_t
CreateFileMapping(
    _In_  int      FileDescriptor,
    _In_  int      Flags,
    _In_  uint64_t Offset,
    _In_  size_t   Length,
    _Out_ void**   MemoryPointer,
    _Out_ UUId_t*  Handle)
{
    FileMappingParameters_t Parameters;
    StdioHandle_t*          FileHandle = StdioFdToHandle(FileDescriptor);
    OsStatus_t              Status;

    // Sanitize that the descritor is valid
    if (FileHandle == NULL || FileHandle->InheritationType != STDIO_HANDLE_FILE) {
        return OsError;
    }

    // Start out by allocating a memory handler handle
    Status = Syscall_CreateMemoryHandler(Flags, Length, Handle, MemoryPointer);
    if (Status == OsSuccess) {
        // Tell the file manager that it now has to handle this as-well
        Parameters.MemoryHandle   = *Handle;
        Parameters.Flags          = Flags;
        Parameters.FileOffset     = Offset;
        Parameters.VirtualAddress = (uintptr_t)*MemoryPointer;
        Parameters.Length         = Length;
        // Status = RegisterFileMapping(FileHandle->InheritationHandle, &FileMappingParameters);
    }
    return Status;
}

OsStatus_t
DestroyFileMapping(
    _In_ UUId_t Handle)
{
    OsStatus_t Status = Syscall_DestroyMemoryHandler(Handle);
    if (Status == OsSuccess) {
        // Status = UnregisterFileMapping(Handle);
    }
    return Status;
}
