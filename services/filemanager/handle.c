/**
 * Copyright 2021, Philip Meulengracht
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
 *
 */

//#define __TRACE

#include <ds/hashtable.h>
#include <ddk/utils.h>
#include <vfs/filesystem.h>
#include <vfs/handle.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vfs/cache.h>

struct handle_wrapper {
    UUId_t              id;
    FileSystemHandle_t* handle;
};

static uint64_t vfs_handle_hash(const void* element);
static int      vfs_handle_cmp(const void* element1, const void* element2);

static hashtable_t       g_handles;
static struct usched_mtx g_handlesLock;
static _Atomic(UUId_t)   g_nextHandleId = ATOMIC_VAR_INIT(10000);

void VfsHandleInitialize(void)
{
    usched_mtx_init(&g_handlesLock);
    hashtable_construct(&g_handles, 0,
                        sizeof(struct handle_wrapper),
                        vfs_handle_hash,
                        vfs_handle_cmp);
}

static inline int __IsEntryFile(FileSystemEntryBase_t* entry)
{
    return (entry->Descriptor.Flags & FILE_FLAG_DIRECTORY) == 0 ? 1 : 0;
}

static inline int __IsAccessExclusive(unsigned int access)
{
    // Exclusive read access?
    if ((access & (__FILE_READ_ACCESS | __FILE_READ_SHARE)) == __FILE_READ_ACCESS) {
        return 1;
    }

    // Exclusive write access?
    if ((access & (__FILE_WRITE_ACCESS | __FILE_WRITE_SHARE)) == __FILE_WRITE_ACCESS) {
        return 1;
    }
    return 0;
}

static void
RegisterHandle(
        _In_ FileSystemCacheEntry_t* entry,
        _In_ FileSystemHandle_t*     handle)
{
    // register to entry
    usched_mtx_lock(&entry->lock);
    list_append(&entry->handles, &handle->header);
    usched_mtx_unlock(&entry->lock);

    // then add it to our registry of handles
    usched_mtx_lock(&g_handlesLock);
    hashtable_set(&g_handles, &(struct handle_wrapper) { .id = handle->id, .handle = handle });
    usched_mtx_unlock(&g_handlesLock);
}

static OsStatus_t
VerifyHandleAccess(
        _In_  FileSystemCacheEntry_t* entry,
        _In_  unsigned int            access)
{
    OsStatus_t osStatus = OsSuccess;

    // keep lock while inspecting element
    usched_mtx_lock(&entry->lock);
    foreach(element, &entry->handles) {
        FileSystemHandleBase_t* handle = element->value;

        // Are we trying to open the file in exclusive mode?
        if (__IsAccessExclusive(access)) {
            ERROR("VerifyPermissions can't get exclusive lock on file, it is already opened");
            osStatus = OsInvalidPermissions;
            break;
        }

        // Is the file already opened in exclusive mode
        if (__IsAccessExclusive(handle->Access)) {
            ERROR("VerifyPermissions can't open file, it is locked");
            osStatus = OsInvalidPermissions;
            break;
        }
    }
    usched_mtx_unlock(&entry->lock);
    return osStatus;
}

OsStatus_t
VfsHandleCreate(
        _In_  UUId_t                  processId,
        _In_  FileSystemCacheEntry_t* entry,
        _In_  unsigned int            options,
        _In_  unsigned int            access,
        _Out_ FileSystemHandle_t**    handleOut)
{
    FileSystem_t*       filesystem;
    FileSystemHandle_t* handle;
    OsStatus_t          osStatus;

    TRACE("VfsHandleCreate(entry=0x%" PRIxIN ", handleOut=0x%" PRIxIN ")",
          entry, handleOut);

    if (!entry || !handleOut) {
        return OsInvalidParameters;
    }

    // we should at this point check other handles to see how many have this file
    // opened, and see if there is any other handles using it
    osStatus = VerifyHandleAccess(entry, access);
    if (osStatus != OsSuccess) {
        return osStatus;
    }

    handle = malloc(sizeof(FileSystemHandle_t));
    if (!handle) {
        return OsOutOfMemory;
    }

    TRACE("VfsHandleCreate opening %s", MStringRaw(entry->path));
    filesystem = entry->filesystem;
    osStatus   = filesystem->module->OpenHandle(&filesystem->base, entry->base, &handle->base);
    if (osStatus != OsSuccess) {
        ERROR("VfsHandleCreate failed to initiate a new entry-handle, code %i", osStatus);
        free(handle);
        return osStatus;
    }

    handle->base->OutBuffer         = NULL;
    handle->base->OutBufferPosition = 0;
    handle->base->Position          = 0;

    // handle file specific options
    if (__IsEntryFile(entry->base)) {
        TRACE("VfsOpenHandleInternal entry is a file, validating options 0x%x", handle->base->Options);

        // Initialise buffering as long as the file
        // handle is not opened as volatile
        if (!(handle->base->Options & __FILE_VOLATILE)) {
            handle->base->OutBuffer = malloc(filesystem->base.Disk.descriptor.SectorSize);
            memset(handle->base->OutBuffer, 0, filesystem->base.Disk.descriptor.SectorSize);
        }

        // Now comes the step where we handle options
        // - but only options that are handle-specific
        if (handle->base->Options & __FILE_APPEND) {
            osStatus = filesystem->module->SeekInEntry(
                    &filesystem->base,
                    entry->base,
                    handle->base,
                    entry->base->Descriptor.Size.QuadPart);
        }
    }

    entry->references++;

    // initialize our part of the data, and then we add it to our registry of handles
    handle->id             = atomic_fetch_add(&g_nextHandleId, 1);
    handle->owner          = processId;
    handle->entry          = entry;
    handle->last_operation = __FILE_OPERATION_NONE;
    handle->base->Access   = access;
    handle->base->Options  = options;
    ELEMENT_INIT(&handle->header, (uintptr_t)handle->id, handle);
    RegisterHandle(entry, handle);

    *handleOut = handle;
    return osStatus;
}

OsStatus_t
VfsHandleDestroy(
        _In_ UUId_t              processId,
        _In_ FileSystemHandle_t* handle)
{
    FileSystem_t* fileSystem;
    OsStatus_t    osStatus;

    if (!handle) {
        return OsInvalidParameters;
    }

    fileSystem = handle->entry->filesystem;

    // remove the entry after flushing
    usched_mtx_lock(&g_handlesLock);
    hashtable_remove(&g_handles, &(struct handle_wrapper) { .id = handle->id });
    usched_mtx_unlock(&g_handlesLock);

    osStatus = fileSystem->module->CloseHandle(&fileSystem->base, handle->base);
    if (osStatus == OsSuccess) {
        // Take care of any entry cleanup / reduction
        handle->entry->references--;
        if (!handle->entry->references) {
            VfsFileSystemCacheRemove(fileSystem, handle->entry->path);
        }
    }
    return osStatus;
}

OsStatus_t
VfsHandleAccess(
        _In_  UUId_t               processId,
        _In_  UUId_t               handleId,
        _In_  unsigned int         requiredAccess,
        _Out_ FileSystemHandle_t** handleOut)
{
    struct handle_wrapper* wrapper;

    TRACE("VfsIsHandleValid(processId=%u, handleId=%u, requiredAccess=0x%x)",
          processId, handleId, requiredAccess);

    usched_mtx_lock(&g_handlesLock);
    wrapper = hashtable_get(&g_handles, &(struct handle_wrapper) { .id = handleId });
    usched_mtx_unlock(&g_handlesLock);

    if (!wrapper) {
        ERROR("VfsIsHandleValid not found: %u", handleId);
        return OsInvalidParameters;
    }

    if (requiredAccess != 0 && wrapper->handle->owner != processId) {
        ERROR("VfsIsHandleValid Owner of the handle did not match the requester. Access Denied.");
        return OsInvalidPermissions;
    }

    if (requiredAccess != 0 && (wrapper->handle->base->Access & requiredAccess) != requiredAccess) {
        ERROR("VfsIsHandleValid handle was not opened with the required access parameter. Access Denied.");
        return OsInvalidPermissions;
    }

    *handleOut = wrapper->handle;
    return OsSuccess;
}

OsStatus_t
VfsFileSystemGetByFileHandle(
        _In_  UUId_t         handleId,
        _Out_ FileSystem_t** fileSystem)
{
    struct handle_wrapper* wrapper;

    usched_mtx_lock(&g_handlesLock);
    wrapper = hashtable_get(&g_handles, &(struct handle_wrapper) { .id = handleId });
    usched_mtx_unlock(&g_handlesLock);

    if (wrapper) {
        *fileSystem = wrapper->handle->entry->filesystem;
        return OsSuccess;
    }
    return OsDoesNotExist;
}

static uint64_t vfs_handle_hash(const void* element)
{
    const struct handle_wrapper* handle = element;
    return handle->id;
}

static int vfs_handle_cmp(const void* element1, const void* element2)
{
    const struct handle_wrapper* lh = element1;
    const struct handle_wrapper* rh = element2;
    return lh->id != rh->id;
}
