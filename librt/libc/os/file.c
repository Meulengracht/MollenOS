/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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

/* Includes 
 * - System */
#include <os/driver/file.h>
#include <os/mollenos.h>
#include <os/syscall.h>

/* StdioFdToHandle
 * Retrieves the file descriptor os-handle from the given fd */
UUId_t
StdioFdToHandle(
    _In_ int fd);

/* GetFilePathFromFd 
 * Queries the system for the absolute file-path of the given file-descriptor. */
OsStatus_t
GetFilePathFromFd(
    _In_ int    FileDescriptor,
    _In_ char*  PathBuffer,
    _In_ size_t MaxLength)
{
    // Variables
    UUId_t FileHandle = StdioFdToHandle(FileDescriptor);

    if (FileHandle == UUID_INVALID || PathBuffer == NULL) {
        return OsError;
    }
    return GetFilePath(FileHandle, PathBuffer, MaxLength);
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
    UUId_t FileHandle = StdioFdToHandle(FileDescriptor);

    if (FileHandle == UUID_INVALID || Information == NULL) {
        return OsError;
    }
    return QueryDiskByHandle(FileHandle, Information);
}

/* GetFileInformationFromPath 
 * Queries information about the file from the given path. If the path
 * does not exist or is invalid the descriptor is zeroed out. */
OsStatus_t
GetFileInformationFromPath(
    _In_ const char*        Path,
    _In_ vFileDescriptor_t* Information)
{
    if (Information == NULL || Path == NULL) {
        return OsError;
    }
    return GetFileStatsByPath(Path, Information);
}

/* GetFileInformationFromFd 
 * Queries information about the file from the given file handle. If the path
 * does not exist or is invalid the descriptor is zeroed out. */
OsStatus_t
GetFileInformationFromFd(
    _In_ int                FileDescriptor,
    _In_ vFileDescriptor_t* Information)
{
    // Variables
    UUId_t FileHandle = StdioFdToHandle(FileDescriptor);

    if (FileHandle == UUID_INVALID || Information == NULL) {
        return OsError;
    }
    return GetFileStatsByHandle(FileHandle, Information);
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
    UUId_t FileHandle = StdioFdToHandle(FileDescriptor);

    // Sanitize that the descritor is valid
    if (FileHandle == UUID_INVALID) {
        return OsError;
    }

    Parameters.FileHandle = FileHandle;
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
