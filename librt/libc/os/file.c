/**
 * MollenOS
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

#include <internal/_io.h>
#include <internal/_ipc.h>
#include <internal/_syscalls.h>
#include <os/mollenos.h>
#include <stdio.h>

OsStatus_t
GetFilePathFromFd(
    _In_ int    FileDescriptor,
    _In_ char*  PathBuffer,
    _In_ size_t MaxLength)
{
    struct vali_link_message msg    = VALI_MSG_INIT_HANDLE(GetFileService());
    stdio_handle_t*          handle = stdio_handle_get(FileDescriptor);
    OsStatus_t               status;

    if (handle == NULL || PathBuffer == NULL || 
        handle->object.type != STDIO_HANDLE_FILE) {
        return OsError;
    }
    
    svc_file_get_path_sync(GetGrachtClient(), &msg, handle->object.handle,
        &status, PathBuffer, MaxLength);
    gracht_vali_message_finish(&msg);
    return status;
}

OsStatus_t
GetStorageInformationFromPath(
    _In_ const char*            Path,
    _In_ OsStorageDescriptor_t* Information)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    OsStatus_t               status;
    
    if (Information == NULL || Path == NULL) {
        return OsError;
    }
    
    svc_storage_get_descriptor_from_path_sync(GetGrachtClient(), &msg, Path,
        &status, Information);
    gracht_vali_message_finish(&msg);
    return status;
}

OsStatus_t
GetStorageInformationFromFd(
    _In_ int                    FileDescriptor,
    _In_ OsStorageDescriptor_t* Information)
{
    struct vali_link_message msg    = VALI_MSG_INIT_HANDLE(GetFileService());
    stdio_handle_t*          handle = stdio_handle_get(FileDescriptor);
    OsStatus_t               status;

    if (handle == NULL || Information == NULL ||
        handle->object.type != STDIO_HANDLE_FILE) {
        return OsError;
    }
    
    svc_storage_get_descriptor_sync(GetGrachtClient(), &msg, handle->object.handle,
        &status, Information);
    gracht_vali_message_finish(&msg);
    return status;
}

OsStatus_t
GetFileSystemInformationFromPath(
    _In_ const char *Path,
    _In_ OsFileSystemDescriptor_t *Information)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    OsStatus_t               status;
    
    if (Information == NULL || Path == NULL) {
        return OsInvalidParameters;
    }
    
    svc_file_fsstat_from_path_sync(GetGrachtClient(), &msg, Path, &status, Information);
    gracht_vali_message_finish(&msg);
    return status;
}

OsStatus_t
GetFileSystemInformationFromFd(
    _In_ int FileDescriptor,
    _In_ OsFileSystemDescriptor_t *Information)
{
    struct vali_link_message msg    = VALI_MSG_INIT_HANDLE(GetFileService());
    stdio_handle_t*          handle = stdio_handle_get(FileDescriptor);
    OsStatus_t               status;

    if (handle == NULL || Information == NULL ||
        handle->object.type != STDIO_HANDLE_FILE) {
        return OsInvalidParameters;
    }
    
    svc_file_fsstat_sync(GetGrachtClient(), &msg, handle->object.handle, &status, Information);
    gracht_vali_message_finish(&msg);
    return status;
}

OsStatus_t
GetFileInformationFromPath(
    _In_ const char*            Path,
    _In_ OsFileDescriptor_t*    Information)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    OsStatus_t               status;
    
    if (Information == NULL || Path == NULL) {
        return OsInvalidParameters;
    }
    
    svc_file_fstat_from_path_sync(GetGrachtClient(), &msg, Path, &status, Information);
    gracht_vali_message_finish(&msg);
    return status;
}

OsStatus_t
GetFileInformationFromFd(
    _In_ int                    FileDescriptor,
    _In_ OsFileDescriptor_t*    Information)
{
    struct vali_link_message msg    = VALI_MSG_INIT_HANDLE(GetFileService());
    stdio_handle_t*          handle = stdio_handle_get(FileDescriptor);
    OsStatus_t               status;

    if (handle == NULL || Information == NULL ||
        handle->object.type != STDIO_HANDLE_FILE) {
        return OsInvalidParameters;
    }
    
    svc_file_fstat_sync(GetGrachtClient(), &msg, handle->object.handle, &status, Information);
    gracht_vali_message_finish(&msg);
    return status;
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
    stdio_handle_t*         handle = stdio_handle_get(FileDescriptor);
    OsStatus_t              Status;

    // Sanitize that the descritor is valid
    if (handle == NULL || handle->object.type != STDIO_HANDLE_FILE) {
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
        // Status = RegisterFileMapping(object->handle.InheritationHandle, &FileMappingParameters);
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
