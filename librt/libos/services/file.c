/**
 * Copyright 2022, Philip Meulengracht
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
 */

#include <ddk/service.h>
#include <ddk/convert.h>
#include <gracht/link/vali.h>
#include <internal/_io.h>
#include <internal/_utils.h>
#include <io.h>
#include <os/services/file.h>
#include <os/handle.h>
#include <sys_file_service_client.h>

static void __FileDestroy(struct OSHandle*);

const OSHandleOps_t g_osFileOps = {
        .Destroy = __FileDestroy
};

static oserr_t
__CloseHandle(
        _In_ uuid_t handleID)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                  oserr;
    int                      status;

    // Try to open the file by directly communicating with the file-service
    status = sys_file_close(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            handleID
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_close_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

oserr_t
OSOpenPath(
        _In_  const char*  path,
        _In_  unsigned int flags,
        _In_  unsigned int permissions,
        _Out_ OSHandle_t*  handleOut)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                  oserr;
    int                      status;
    uuid_t                   handleID;

    if (path == NULL || handleOut == NULL) {
        return OS_EINVALPARAMS;
    }

    // Try to open the file by directly communicating with the file-service
    status = sys_file_open(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            path,
            flags,
            permissions
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(
            GetGrachtClient(),
            &msg.base,
            GRACHT_AWAIT_ASYNC
    );
    sys_file_open_result(
            GetGrachtClient(),
            &msg.base,
            &oserr,
            &handleID
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = OSHandleWrap(
            handleID,
            OSHANDLE_FILE,
            NULL,
            false,
            handleOut
    );
    if (oserr != OS_EOK) {
        (void)__CloseHandle(handleID);
    }
    return oserr;
}

oserr_t
OSMakeDirectory(
        _In_ const char*  path,
        _In_ unsigned int permissions)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                  oserr;
    int                      status;

    if (path == NULL) {
        return OS_EINVALPARAMS;
    }

    status = sys_file_mkdir(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            path,
            permissions
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_mkdir_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

static void __ToOSDirectoryEntry(struct sys_directory_entry* in, OSDirectoryEntry_t* out)
{
    out->Name = in->name;
    out->ID = in->id;
    out->Flags = in->flags;
    out->Index = in->index;
}

oserr_t
OSReadDirectory(
        _In_ uuid_t              handle,
        _In_ OSDirectoryEntry_t* entry)
{
    struct vali_link_message   msg = VALI_MSG_INIT_HANDLE(GetFileService());
    struct sys_directory_entry sysEntry;
    oserr_t                    oserr;
    int                        status;

    if (entry == NULL) {
        return OS_EINVALPARAMS;
    }

    status = sys_file_readdir(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            handle
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_readdir_result(GetGrachtClient(), &msg.base, &oserr, &sysEntry);
    if (oserr == OS_EOK) {
        __ToOSDirectoryEntry(&sysEntry, entry);
    }
    return oserr;
}

oserr_t
OSSeekFile(
        _In_ uuid_t        handle,
        _In_ UInteger64_t* position)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                  oserr;
    int                      status;

    if (position == NULL) {
        return OS_EINVALPARAMS;
    }

    // Try to open the file by directly communicating with the file-service
    status = sys_file_seek(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            handle,
            position->u.LowPart,
            position->u.HighPart
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_seek_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

oserr_t
OSUnlinkPath(
        _In_ const char* path)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                  oserr;
    int                      status;

    if (path == NULL ) {
        return OS_EINVALPARAMS;
    }

    status = sys_file_delete(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            path,
            0
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_delete_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

oserr_t
OSMoveFile(
        _In_ const char* from,
        _In_ const char* to,
        _In_ bool        copy)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                  oserr;
    int                      status;

    if (from == NULL || to == NULL) {
        return OS_EINVALPARAMS;
    }

    status = sys_file_move(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            from,
            to,
            copy
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_move_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

oserr_t
OSLinkPath(
        _In_ const char* from,
        _In_ const char* to,
        _In_ bool        symbolic)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                  oserr;
    int                      status;

    if (from == NULL || to == NULL) {
        return OS_EINVALPARAMS;
    }

    status = sys_file_link(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            from,
            to,
            symbolic
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_link_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}


oserr_t
OSTransferFile(
        _In_  uuid_t handle,
        _In_  uuid_t bufferID,
        _In_  size_t bufferOffset,
        _In_  bool   write,
        _In_  size_t length,
        _Out_ size_t* bytesTransferred)
{
    struct vali_link_message msg       = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                  status;

    sys_file_transfer(GetGrachtClient(), &msg.base, __crt_process_id(),
                      handle,
                      (int)write,
                      bufferID,
                      bufferOffset,
                      length
    );
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_transfer_result(GetGrachtClient(), &msg.base, &status, bytesTransferred);
    return status;
}

oserr_t
OSGetFilePosition(
        _In_ uuid_t        handle,
        _In_ UInteger64_t* position)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                  oserr;
    int                      status;

    if (position == NULL) {
        return OS_EINVALPARAMS;
    }

    // Try to open the file by directly communicating with the file-service
    status = sys_file_get_position(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            handle
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_get_position_result(
            GetGrachtClient(), &msg.base, &oserr,
            &position->u.LowPart,
            &position->u.HighPart
    );
    return oserr;
}

oserr_t
OSGetFileSize(
        _In_ uuid_t        handle,
        _In_ UInteger64_t* size)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                  oserr;
    int                      status;

    if (size == NULL) {
        return OS_EINVALPARAMS;
    }

    // Try to open the file by directly communicating with the file-service
    status = sys_file_get_size(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            handle
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_get_size_result(
            GetGrachtClient(), &msg.base, &oserr,
            &size->u.LowPart,
            &size->u.HighPart
    );
    return oserr;
}

oserr_t
OSSetFileSize(
        _In_ uuid_t        handle,
        _In_ UInteger64_t* size)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                  oserr;
    int                      status;

    if (size == NULL) {
        return OS_EINVALPARAMS;
    }

    status = sys_file_set_size(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            handle,
            size->u.LowPart,
            size->u.HighPart
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_set_size_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

oserr_t
SetFileSizeFromPath(
        _In_ const char* path,
        _In_ size_t      size)
{
    oserr_t oserr;
    int     fd;
    
    if (!path) {
        return OS_EINVALPARAMS;
    }

    fd = open(path, O_RDWR);
    if (fd == -1) {
        return OsErrToErrNo(fd);
    }

    oserr = SetFileSizeFromFd(fd, size);

    close(fd);
    return oserr;
}

oserr_t
SetFileSizeFromFd(
        _In_ int    fileDescriptor,
        _In_ size_t size)
{
    stdio_handle_t* handle = stdio_handle_get(fileDescriptor);
    UInteger64_t    value;

    if (stdio_handle_signature(handle) != FILE_SIGNATURE) {
        return OS_EINVALPARAMS;
    }

    value.QuadPart = size;
    return OSSetFileSize(handle->OSHandle.ID, &value);
}

oserr_t
ChangeFilePermissionsFromPath(
        _In_ const char*  path,
        _In_ unsigned int permissions)
{
    oserr_t oserr;
    int     fd;
    
    if (!path) {
        return OS_EINVALPARAMS;
    }

    fd = open(path, O_RDWR);
    if (fd == -1) {
        return OsErrToErrNo(fd);
    }

    oserr = ChangeFilePermissionsFromFd(fd, permissions);
    
    close(fd);
    return oserr;
}

oserr_t
ChangeFilePermissionsFromFd(
        _In_ int          fileDescriptor,
        _In_ unsigned int permissions)
{
    struct vali_link_message msg    = VALI_MSG_INIT_HANDLE(GetFileService());
    stdio_handle_t*          handle = stdio_handle_get(fileDescriptor);
    oserr_t                  oserr;
    unsigned int             access;
    int                      status;

    if (stdio_handle_signature(handle) != FILE_SIGNATURE) {
        return OS_EINVALPARAMS;
    }

    status = sys_file_get_access(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            handle->OSHandle.ID
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_get_access_result(GetGrachtClient(), &msg.base, &oserr, &access);
    
    status = sys_file_set_access(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            handle->OSHandle.ID,
            access
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_set_access_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

oserr_t
ChangeFileHandleAccessFromFd(
        _In_ int          fileDescriptor,
        _In_ unsigned int access)
{
    struct vali_link_message msg    = VALI_MSG_INIT_HANDLE(GetFileService());
    stdio_handle_t*          handle = stdio_handle_get(fileDescriptor);
    oserr_t                  oserr;
    int                      status;

    if (stdio_handle_signature(handle) != FILE_SIGNATURE) {
        return OS_EINVALPARAMS;
    }

    status = sys_file_set_access(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            handle->OSHandle.ID,
            access
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_set_access_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

oserr_t
GetFileLink(
        _In_ const char* path,
        _In_ char*       linkPathBuffer,
        _In_ size_t      bufferLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                  oserr;
    int                      status;
    
    if (!path || !linkPathBuffer || bufferLength == 0) {
        return OS_EINVALPARAMS;
    }

    status = sys_file_fstat_link(GetGrachtClient(), &msg.base, __crt_process_id(), path);
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_fstat_link_result(GetGrachtClient(), &msg.base, &oserr, linkPathBuffer, bufferLength);
    return oserr;
}

oserr_t
GetFilePathFromFd(
    _In_ int    fileDescriptor,
    _In_ char*  buffer,
    _In_ size_t maxLength)
{
    struct vali_link_message msg    = VALI_MSG_INIT_HANDLE(GetFileService());
    stdio_handle_t*          handle = stdio_handle_get(fileDescriptor);
    oserr_t                  oserr;
    int                      status;

    if (stdio_handle_signature(handle) != FILE_SIGNATURE) {
        return OS_EINVALPARAMS;
    }
    
    status = sys_file_get_path(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            handle->OSHandle.ID
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_get_path_result(GetGrachtClient(), &msg.base, &oserr, buffer, maxLength);
    return oserr;
}

oserr_t
GetStorageInformationFromPath(
        _In_ const char*            path,
        _In_ int                    followLinks,
        _In_ OSStorageDescriptor_t* descriptor)
{
    struct vali_link_message   msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                    oserr;
    int                        status;
    struct sys_disk_descriptor gdescriptor;
    
    if (descriptor == NULL || path == NULL) {
        return OS_EINVALPARAMS;
    }

    status = sys_file_ststat_path(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            path,
            followLinks
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_ststat_path_result(GetGrachtClient(), &msg.base, &oserr, &gdescriptor);

    if (oserr == OS_EOK) {
        from_sys_disk_descriptor(&gdescriptor, descriptor);
    }
    return oserr;
}

oserr_t
GetStorageInformationFromFd(
        _In_ int                    fileDescriptor,
        _In_ OSStorageDescriptor_t* descriptor)
{
    struct vali_link_message   msg    = VALI_MSG_INIT_HANDLE(GetFileService());
    stdio_handle_t*            handle = stdio_handle_get(fileDescriptor);
    oserr_t                    oserr;
    int                        status;
    struct sys_disk_descriptor gdescriptor;

    if (descriptor == NULL || stdio_handle_signature(handle) != FILE_SIGNATURE) {
        return OS_EINVALPARAMS;
    }

    status = sys_file_ststat(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            handle->OSHandle.ID
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_ststat_result(GetGrachtClient(), &msg.base, &oserr, &gdescriptor);

    if (oserr == OS_EOK) {
        from_sys_disk_descriptor(&gdescriptor, descriptor);
    }
    return oserr;
}

oserr_t
GetFileSystemInformationFromPath(
        _In_ const char*               path,
        _In_ int                       followLinks,
        _In_ OSFileSystemDescriptor_t* descriptor)
{
    struct vali_link_message         msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                          oserr;
    int                              status;
    struct sys_filesystem_descriptor gdescriptor;
    
    if (descriptor == NULL || path == NULL) {
        return OS_EINVALPARAMS;
    }
    
    status = sys_file_fsstat_path(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            path,
            followLinks
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_fsstat_path_result(GetGrachtClient(), &msg.base, &oserr, &gdescriptor);

    if (oserr == OS_EOK) {
        from_sys_filesystem_descriptor(&gdescriptor, descriptor);
    }
    return oserr;
}

oserr_t
GetFileSystemInformationFromFd(
        _In_ int                       fileDescriptor,
        _In_ OSFileSystemDescriptor_t* descriptor)
{
    struct vali_link_message         msg    = VALI_MSG_INIT_HANDLE(GetFileService());
    stdio_handle_t*                  handle = stdio_handle_get(fileDescriptor);
    oserr_t                          oserr;
    int                              status;
    struct sys_filesystem_descriptor gdescriptor;

    if (descriptor == NULL || stdio_handle_signature(handle) != FILE_SIGNATURE) {
        return OS_EINVALPARAMS;
    }
    
    status = sys_file_fsstat(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            handle->OSHandle.ID
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_fsstat_result(GetGrachtClient(), &msg.base, &oserr, &gdescriptor);

    if (oserr == OS_EOK) {
        from_sys_filesystem_descriptor(&gdescriptor, descriptor);
    }
    return oserr;
}

oserr_t
GetFileInformationFromPath(
        _In_ const char*         path,
        _In_ int                 followLinks,
        _In_ OSFileDescriptor_t* descriptor)
{
    struct vali_link_message   msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                    oserr;
    int                        status;
    struct sys_file_descriptor gdescriptor;
    
    if (descriptor == NULL || path == NULL) {
        return OS_EINVALPARAMS;
    }
    
    status = sys_file_fstat_path(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            path,
            followLinks
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_fstat_path_result(GetGrachtClient(), &msg.base, &oserr, &gdescriptor);

    if (oserr == OS_EOK) {
        from_sys_file_descriptor(&gdescriptor, descriptor);
    }
    return oserr;
}

oserr_t
GetFileInformationFromFd(
        _In_ int                 fileDescriptor,
        _In_ OSFileDescriptor_t* descriptor)
{
    struct vali_link_message   msg    = VALI_MSG_INIT_HANDLE(GetFileService());
    stdio_handle_t*            handle = stdio_handle_get(fileDescriptor);
    oserr_t                    oserr;
    int                        status;
    struct sys_file_descriptor gdescriptor;

    if (descriptor == NULL || stdio_handle_signature(handle) != FILE_SIGNATURE) {
        return OS_EINVALPARAMS;
    }

    status = sys_file_fstat(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            handle->OSHandle.ID
    );
    if (status) {
        return OS_EPROTOCOL;
    }
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_fstat_result(GetGrachtClient(), &msg.base, &oserr, &gdescriptor);

    if (oserr == OS_EOK) {
        from_sys_file_descriptor(&gdescriptor, descriptor);
    }
    return oserr;
}

static void
__FileDestroy(struct OSHandle* handle)
{
    if (handle == NULL) {
        return;
    }
    (void)__CloseHandle(handle->ID);
}

// add default event handlers
void sys_file_event_storage_ready_invocation(gracht_client_t* client, const char* path) {
    _CRT_UNUSED(client);
    _CRT_UNUSED(path);
}
