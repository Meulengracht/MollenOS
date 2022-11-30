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

#define __TRACE

#include <ddk/utils.h>
#include <ds/mstring.h>
#include <ds/guid.h>
#include <os/dmabuf.h>
#include <os/types/file.h>
#include <vfs/filesystem.h>
#include <vfs/interface.h>
#include <vfs/scope.h>
#include <vfs/storage.h>
#include <vfs/vfs.h>
#include <stdlib.h>

#include <sys_mount_service_server.h>

static uint32_t __AccessFlagsFromMountFlags(enum sys_mount_flags mountFlags)
{
    uint32_t access = 0;
    if (mountFlags & SYS_MOUNT_FLAGS_READ) {
        access |= FILE_PERMISSION_READ;
    }
    if (mountFlags & SYS_MOUNT_FLAGS_WRITE) {
        access |= FILE_PERMISSION_WRITE;
    }
    return access;
}

static oserr_t __DetectFileSystem(
        _In_  struct VFSStorage*  storage,
        _In_  UInteger64_t*       sector,
        _Out_ const char**        fsHintOut)
{

    DMABuffer_t     dmaInfo;
    DMAAttachment_t dmaAttachment;
    oserr_t         oserr;
    TRACE("__DetectFileSystem()");

    // Allocate a generic transfer buffer for disk operations
    // on the given disk, we need it to parse the disk
    dmaInfo.name     = "disk_temp_buffer";
    dmaInfo.capacity = storage->Stats.SectorSize;
    dmaInfo.length   = storage->Stats.SectorSize;
    dmaInfo.flags    = 0;
    dmaInfo.type     = DMA_TYPE_DRIVER_32LOW;

    oserr = DmaCreate(&dmaInfo, &dmaAttachment);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = VFSStorageDeriveFileSystemType(
            storage,
            dmaAttachment.handle,
            dmaAttachment.buffer,
            sector,
            fsHintOut
    );

    DmaAttachmentUnmap(&dmaAttachment);
    DmaDetach(&dmaAttachment);
    return oserr;
}

static oserr_t __SetupFileBackedStorage(
        _In_  struct VFSStorage*  storage,
        _In_  size_t              offset,
        _In_  const char*         fsType,
        _In_  uuid_t              interfaceDriverID,
        _Out_ struct FileSystem** fileSystemOut)
{
    struct VFSInterface* interface = NULL;
    struct FileSystem*   fileSystem;
    oserr_t              oserr;
    UInteger64_t         sector;
    guid_t               guid;
    TRACE("__SetupFileBackedStorage()");

    // Offset should be given in bytes, and should always be 512-bytes
    // aligned.
    guid_new(&guid);
    sector.QuadPart = offset / 512;
    oserr = VFSStorageRegisterPartition(
            storage,
            0,
            &sector,
            &guid,
            &fileSystem
    );
    if (oserr != OS_EOK) {
        return oserr;
    }
    *fileSystemOut = fileSystem;

    // Try to find a module for the filesystem type. Since this is file mounted, and we
    // only support RO file mounts at the moment, we expect to be able to resolve the file
    // system driver immediately
    if (interfaceDriverID == UUID_INVALID && fsType == NULL) {
        // Use auto-detection like we do for MBR/GPT
        oserr = __DetectFileSystem(storage, &sector, &fsType);
        if (oserr != OS_EOK) {
            // If we fail to determine the file-system, then we should still mount the file
            // as a raw device that people can format. TODO This needs to be implemented.
            WARNING("__SetupFileBackedStorage failed to auto-detect filesystem");
            // __MountAsRawDeviceOnly
            return OS_ENOTSUPPORTED;
        }
    }

    // Next step is to load a module or driver for the fileystem.
    if (interfaceDriverID == UUID_INVALID) {
        oserr = VFSInterfaceLoadInternal(fsType, &interface);
        if (oserr != OS_EOK) {
            WARNING("__SetupFileBackedStorage no module for filesystem type %s", fsType);
        }
    } else {
        oserr = VFSInterfaceLoadDriver(interfaceDriverID, &interface);
        if (oserr != OS_EOK) {
            WARNING("__SetupFileBackedStorage failed to register driver for %s", fsType);
        }
    }

    // If the interface fails to connect, then the filesystem will go into
    // state NO_INTERFACE. We bail early then as there is no reason to mount the
    // filesystem
    return VFSFileSystemConnectInterface(fileSystem, interface);
}

static oserr_t __MountFile(
        _In_ struct VFS*          vfs,
        _In_ const char*          cpath,
        _In_ size_t               offset,
        _In_ uuid_t               interfaceDriverID,
        _In_ uuid_t               atHandle,
        _In_ const char*          fsType,
        _In_ enum sys_mount_flags flags)
{
    struct VFSStorage* storage;
    struct FileSystem* fileSystem;
    uuid_t             handle;
    oserr_t            oserr;
    TRACE("__MountFile()");

    oserr = VFSNodeOpen(
            vfs, cpath,
            __AccessFlagsFromMountFlags(flags),
            FILE_PERMISSION_READ | FILE_PERMISSION_OWNER_WRITE | FILE_PERMISSION_OWNER_EXECUTE,
            &handle
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    // The first thing we need to do is to create as file-backed storage medium.
    // Then we need to somehow discover the correct filesystem, either by using the
    // hint provided, or allowing the user to specify a filesystem interface implementation.
    storage = VFSStorageCreateFileBacked(handle);
    if (storage == NULL) {
        (void)VFSNodeClose(vfs, handle);
        return OS_EOOM;
    }

    // Then we can safely create a new VFS from this.
    oserr = __SetupFileBackedStorage(storage, offset, fsType, interfaceDriverID, &fileSystem);
    if (oserr != OS_EOK) {
        VFSStorageDelete(storage);
        (void)VFSNodeClose(vfs, handle);
        return oserr;
    }

    oserr = VFSNodeMount(vfs, atHandle, fileSystem->VFS);
    if (oserr != OS_EOK) {
        VFSStorageDelete(storage);
        (void)VFSNodeClose(vfs, handle);
    }
    return oserr;
}

static oserr_t __MountPath(
        _In_ struct VFS*          vfs,
        _In_ const char*          cpath,
        _In_ uuid_t               atHandle,
        _In_ const char*          fsType,
        _In_ enum sys_mount_flags flags)
{
    uuid_t  handle;
    oserr_t oserr;
    TRACE("__MountPath(what=%s)", cpath);

    oserr = VFSNodeOpen(
            vfs, cpath,
            __FILE_DIRECTORY,  // TODO what kind of access should we have here
            FILE_PERMISSION_READ | FILE_PERMISSION_OWNER_WRITE | FILE_PERMISSION_OWNER_EXECUTE,
            &handle
    );
    if (oserr != OS_EOK) {
        if (oserr == OS_ENOTDIR) {
            return __MountFile(vfs, cpath, 0, UUID_INVALID, atHandle, fsType, flags);
        }
        return oserr;
    }

    oserr = VFSNodeBind(vfs, handle, atHandle);
    if (oserr != OS_EOK) {
        // Ok something went wrong, maybe it's already bind mounted.
        (void)VFSNodeClose(vfs, handle);
        return oserr;
    }
    return oserr;
}

static oserr_t __MountSpecial(
        _In_ struct VFS*     vfs,
        _In_ uuid_t          atHandle,
        _In_ const char*     fsType)
{
    TRACE("__MountSpecial()");

    if (fsType == NULL) {
        return OS_EINVALPARAMS;
    }

    // Verify against supported special fs's
    if (!strcmp(fsType, "memfs")) {
        // TODO support mounting temporary fs's, this should be pretty easy
        return OS_ENOTSUPPORTED;
    }
    return OS_EINVALPARAMS;
}

oserr_t
Mount(
        _In_ struct VFS*  fsScope,
        _In_ const char*  path,
        _In_ const char*  at,
        _In_ const char*  fsType,
        _In_ unsigned int flags)
{
    uuid_t  atHandle = UUID_INVALID;
    oserr_t oserr;

    // Get a handle on the directory we are mounting on top of. This directory
    // must exist for the length of the mount.
    oserr = VFSNodeOpen(
            fsScope,
            at,
            __FILE_DIRECTORY, // TODO what kind of access should we have here
            FILE_PERMISSION_READ | FILE_PERMISSION_OWNER_WRITE | FILE_PERMISSION_OWNER_EXECUTE,
            &atHandle
    );
    if (oserr != OS_EOK) {
        goto cleanup;
    }

    // If a path is provided, then we are either mounting a directory or
    // an image file. If no path is provided, then we are mounting a special
    // filesystem on top of the path given in <at>
    if (path) {
        oserr = __MountPath(
                fsScope, path, atHandle, fsType,
                (enum sys_mount_flags)flags
        );
    } else {
        oserr = __MountSpecial(fsScope, atHandle, fsType);
    }

cleanup:
    // Cleanup resources allocated by us and the request
    if (oserr != OS_EOK && atHandle != UUID_INVALID) {
        (void)VFSNodeClose(fsScope, atHandle);
    }
    return oserr;
}
