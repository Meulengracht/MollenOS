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
 * MollenOS MCore - File Definitions & Structures
 * - This header describes the base file-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <os/file.h>
#include <os/mollenos.h>
#include <os/syscall.h>
#include <stdio.h>
#include "../stdio/local.h"

/* GetFilePathFromFd 
 * Queries the system for the absolute file-path of the given file-descriptor. */
OsStatus_t
GetFilePathFromFd(
    _In_ int    FileDescriptor,
    _In_ char*  PathBuffer,
    _In_ size_t MaxLength)
{
    // Variables
    StdioHandle_t *FileHandle = StdioFdToHandle(FileDescriptor);

    if (FileHandle == NULL || PathBuffer == NULL || 
        FileHandle->InheritationType != STDIO_HANDLE_FILE) {
        return OsError;
    }
    return GetFilePath(FileHandle->InheritationHandle, PathBuffer, MaxLength);
}

/* GetStorageInformationFromPath 
 * Retrives information about the storage medium that belongs to 
 * the given path. */
OsStatus_t
GetStorageInformationFromPath(
    _In_ const char*            Path,
    _In_ vStorageDescriptor_t*  Information)
{
    if (Information == NULL || Path == NULL) {
        return OsError;
    }
    return QueryDiskByPath(Path, Information);
}

/* GetStorageInformationFromFd 
 * Retrives information about the storage medium that belongs to 
 * the given file descriptor. */
OsStatus_t
GetStorageInformationFromFd(
    _In_ int                    FileDescriptor,
    _In_ vStorageDescriptor_t*  Information)
{
    // Variables
    StdioHandle_t *FileHandle = StdioFdToHandle(FileDescriptor);

    if (FileHandle == NULL || Information == NULL ||
        FileHandle->InheritationType != STDIO_HANDLE_FILE) {
        return OsError;
    }
    return QueryDiskByHandle(FileHandle->InheritationHandle, Information);
}

/* GetFileInformationFromPath 
 * Queries information about the file from the given path. If the path
 * does not exist or is invalid the descriptor is zeroed out. */
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

/* GetFileInformationFromFd 
 * Queries information about the file from the given file handle. If the path
 * does not exist or is invalid the descriptor is zeroed out. */
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

/* Parameter structure for creating file-mappings. 
 * Private structure, only used for parameter passing. */
struct FileMappingParameters {
    UUId_t    FileHandle;
    int       Flags;
    uint64_t  Offset;
    size_t    Size;
};

/* CreateFileMapping 
 * Creates a new memory mapping for the given file descriptor with the given
 * offset and size. */
OsStatus_t
CreateFileMapping(
    _In_  int       FileDescriptor,
    _In_  int       Flags,
    _In_  uint64_t  Offset,
    _In_  size_t    Size,
    _Out_ void**    MemoryPointer)
{
    // Variables
    struct FileMappingParameters Parameters;
    StdioHandle_t *FileHandle = StdioFdToHandle(FileDescriptor);

    // Sanitize that the descritor is valid
    if (FileHandle == NULL || FileHandle->InheritationType != STDIO_HANDLE_FILE) {
        return OsError;
    }

    Parameters.FileHandle = FileHandle->InheritationHandle;
    Parameters.Flags = Flags;
    Parameters.Offset = Offset;
    Parameters.Size = Size;
    return Syscall_CreateFileMapping(&Parameters, MemoryPointer);
}

/* DestroyFileMapping 
 * Destroys a previously created memory mapping. */
OsStatus_t
DestroyFileMapping(
    _In_ void *MemoryPointer)
{
    if (MemoryPointer == NULL) {
        return OsError;
    }
    return Syscall_DestroyFileMapping(MemoryPointer);
}
