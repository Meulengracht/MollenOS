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
 *
 */

#define __TRACE
#define __need_minmax
#include <ddk/utils.h>
#include <ds/hashtable.h>
#include <os/usched/mutex.h>
#include <os/handle.h>
#include <os/shm.h>
#include <os/time.h>
#include <stdlib.h>
#include <string.h>
#include <vfs/vfs.h>
#include "private.h"

struct MemFSFile {
    void*  Buffer;
    size_t BufferSize;
};

struct MemFSDirectory {
    hashtable_t Entries;
};

struct MemFSSymlink {
    mstring_t* Target;
};

struct MemFSLink {
    struct MemFSEntry* Original;
};

enum {
    MEMFS_ENTRY_TYPE_INVALID,
    MEMFS_ENTRY_TYPE_FILE,
    MEMFS_ENTRY_TYPE_DIRECTORY,
    MEMFS_ENTRY_TYPE_SYMLINK,
    MEMFS_ENTRY_TYPE_LINK
};

struct MemFSEntry {
    mstring_t*        Name;
    uint32_t          UserID;
    uint32_t          GroupID;
    uint32_t          Permissions;
    OSTimestamp_t     Accessed;
    OSTimestamp_t     Modified;
    OSTimestamp_t     Created;
    struct usched_mtx Mutex;
    int               Type;
    int               Links;
    union {
        struct MemFSFile      File;
        struct MemFSDirectory Directory;
        struct MemFSSymlink   Symlink;
        struct MemFSLink      Link;
    } Data;
};

// hashtable handlers for MemFSDirectory::Entries
struct __DirectoryEntry {
    mstring_t*         Name;
    struct MemFSEntry* Entry;
};

static uint64_t __directory_entries_hash(const void* element);
static int      __directory_entries_cmp(const void* element1, const void* element2);

static void __MemFSEntry_delete(struct MemFSEntry* entry);

struct MemFS {
    struct MemFSEntry* Root;
};

struct MemFSHandle {
    size_t             Position;
    struct MemFSEntry* Entry;
};

static mstring_t g_rootName = mstr_const("/");

static inline void __CopyTime(OSTimestamp_t* destination, OSTimestamp_t* source) {
    destination->Seconds = source->Seconds;
    destination->Nanoseconds = source->Nanoseconds;
}

static void __directory_entries_delete(int index, const void* element, void* userContext)
{
    struct __DirectoryEntry* entry = (struct __DirectoryEntry*)element;
    __MemFSEntry_delete(entry->Entry);
}

static void __MemFSEntry_delete(struct MemFSEntry* entry)
{
    if (entry == NULL) {
        return;
    }

    // Reduce the link count by one.
    entry->Links--;
    if (entry->Links) {
        // There are still links that refer to this entry, so while we remove it from
        // the parent directory, the entry itself should keep on existing.
        return;
    }

    if (entry->Type == MEMFS_ENTRY_TYPE_FILE) {
        free(entry->Data.File.Buffer);
    } else if (entry->Type == MEMFS_ENTRY_TYPE_DIRECTORY) {
        hashtable_enumerate(&entry->Data.Directory.Entries, __directory_entries_delete, NULL);
        hashtable_destroy(&entry->Data.Directory.Entries);
    } else if (entry->Type == MEMFS_ENTRY_TYPE_SYMLINK) {
        mstr_delete(entry->Data.Symlink.Target);
    } else if (entry->Type == MEMFS_ENTRY_TYPE_LINK) {
        // Reduce the original entry link count by one. If we are the last one to do
        // so then the original entry should be deleted here as well.
        __MemFSEntry_delete(entry->Data.Link.Original);
    }

    mstr_delete(entry->Name);
    free(entry);
}

static int __MemFSDirectory_construct(struct MemFSDirectory* directory)
{
    return hashtable_construct(&directory->Entries, 0, sizeof(struct __DirectoryEntry),
                               __directory_entries_hash, __directory_entries_cmp);
}

static struct MemFSEntry* __MemFSEntry_new(mstring_t* name, uint32_t owner, int type, uint32_t permissions)
{
    struct MemFSEntry* entry;
    TRACE("__MemFSEntry_new(name=%ms, owner=%u, type=%i, perms=0x%x)", name, owner, type, permissions);

    entry = malloc(sizeof(struct MemFSEntry));
    if (entry == NULL) {
        return NULL;
    }
    memset(entry, 0, sizeof(struct MemFSEntry));

    entry->Type = type;
    entry->UserID = owner;
    entry->GroupID = 0; // TODO: add support
    entry->Permissions = permissions;
    entry->Links = 1;
    OSGetTime(OSTimeSource_UTC, &entry->Created);

    // Do some type based initialization where we don't need additional parameters
    // to set up. The rest of the initialization should be handled by the create/link
    // function.
    if (type == MEMFS_ENTRY_TYPE_DIRECTORY) {
        if (__MemFSDirectory_construct(&entry->Data.Directory)) {
            free(entry);
            return NULL;
        }
    }

    entry->Name = mstr_clone(name);
    if (entry->Name == NULL) {
        __MemFSEntry_delete(entry);
        return NULL;
    }

    usched_mtx_init(&entry->Mutex, USCHED_MUTEX_PLAIN);
    return entry;
}

static void __MemFS_delete(struct MemFS* memfs)
{
    if (memfs == NULL) {
        return;
    }

    __MemFSEntry_delete(memfs->Root);
    free(memfs);
}

static struct MemFS* __MemFS_new(void)
{
    struct MemFS* memfs = malloc(sizeof(struct MemFS));
    if (memfs == NULL) {
        return NULL;
    }

    memfs->Root = __MemFSEntry_new(
            &g_rootName,
            0, MEMFS_ENTRY_TYPE_DIRECTORY,
            FILE_PERMISSION_READ | FILE_PERMISSION_WRITE | FILE_PERMISSION_EXECUTE
    );
    if (memfs->Root == NULL) {
        __MemFS_delete(memfs);
        return NULL;
    }
    return memfs;
}

static oserr_t
__MemFSInitialize(
        _In_  struct VFSInterface*         interface,
        _In_  struct VFSStorageParameters* storageParameters,
        _Out_ void**                       instanceData)
{
    TRACE("__MemFSInitialize()");
    _CRT_UNUSED(storageParameters);

    struct MemFS* memfs = __MemFS_new();
    if (memfs == NULL) {
        return OS_EOOM;
    }

    *instanceData = memfs;
    return OS_EOK;
}

static oserr_t
__MemFSDestroy(
        _In_ struct VFSInterface* interface,
        _In_ void*                instanceData,
        _In_ unsigned int         unmountFlags)
{
    TRACE("__MemFSDestroy()");
    if (instanceData == NULL) {
        return OS_EINVALPARAMS;
    }
    __MemFS_delete(instanceData);
    return OS_EOK;
}

static void __MemFSHandle_delete(struct MemFSHandle* handle)
{
    free(handle);
}

static struct MemFSHandle* __MemFSHandle_new(struct MemFSEntry* entry)
{
    struct MemFSHandle* handle = malloc(sizeof(struct MemFSHandle));
    if (handle == NULL) {
        return NULL;
    }

    handle->Position = 0;
    handle->Entry    = entry;
    return handle;
}

static oserr_t __FindNode(
        struct MemFSEntry*  root,
        mstring_t*          path,
        struct MemFSEntry** parentOut,
        struct MemFSEntry** entryOut)
{
    mstring_t** tokens;
    int         tokensCount;
    oserr_t     oserr = OS_EOK;

    TRACE("__FindNode(root=%ms, path=%ms)", root->Name, path);

    // Special case, we are trying to open root path
    if (__PathIsRoot(path)) {
        *entryOut = root;
        return OS_EOK;
    }

    // Otherwise split path into tokens and find the entry requested
    tokensCount = mstr_path_tokens(path, &tokens);
    if (tokensCount <= 0) {
        return OS_EINVALPARAMS;
    }

    struct MemFSEntry* n = root;
    struct MemFSEntry* p = root;
    for (int i = 0; i < tokensCount; i++) {
        struct __DirectoryEntry* directoryEntry;
        struct MemFSEntry*       entry = NULL;
        bool                     isLastEntry = i == (tokensCount - 1);

        usched_mtx_lock(&n->Mutex);
        directoryEntry = hashtable_get(&n->Data.Directory.Entries, &(struct __DirectoryEntry) {
            .Name = tokens[i]
        });
        if (directoryEntry != NULL) {
            entry = directoryEntry->Entry;
        }
        usched_mtx_unlock(&n->Mutex);

        if (entry == NULL) {
            oserr = OS_ENOENT;
            break;
        }

        // The entry we've found must be a directory if we are not at the last entry
        if (!isLastEntry && entry->Type != MEMFS_ENTRY_TYPE_DIRECTORY) {
            oserr = OS_ENOTDIR;
            break;
        }
        p = n;
        n = entry;
    }

    mstrv_delete(tokens);
    *parentOut = p;
    *entryOut  = n;
    return oserr;
}

static oserr_t
__MemFSOpen(
        _In_      struct VFSInterface* interface,
        _In_      void*                instanceData,
        _In_      mstring_t*           path,
        _Out_Opt_ void**               dataOut)
{
    struct MemFS*       memfs = instanceData;
    struct MemFSHandle* handle;
    struct MemFSEntry*  parent;
    struct MemFSEntry*  entry;
    oserr_t             oserr;

    TRACE("__MemFSOpen(path=%ms)", path);
    if (instanceData == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = __FindNode(memfs->Root, path, &parent, &entry);
    if (oserr != OS_EOK) {
        return oserr;
    }

    usched_mtx_lock(&entry->Mutex);
    handle = __MemFSHandle_new(entry);
    if (handle == NULL) {
        usched_mtx_unlock(&entry->Mutex);
        return OS_EOOM;
    }

    // Update the accessed time of entry before continuing
    OSGetTime(OSTimeSource_UTC, &entry->Accessed);
    usched_mtx_unlock(&entry->Mutex);

    *dataOut = handle;
    return OS_EOK;
}

static int __TypeFromFlags(uint32_t flags) {
    switch (FILE_FLAG_TYPE(flags)) {
        case FILE_FLAG_FILE: return MEMFS_ENTRY_TYPE_FILE;
        case FILE_FLAG_DIRECTORY: return MEMFS_ENTRY_TYPE_DIRECTORY;
        case FILE_FLAG_LINK: return MEMFS_ENTRY_TYPE_SYMLINK;
        default: return MEMFS_ENTRY_TYPE_INVALID;
    }
}

static oserr_t __CreateInNode(
        _In_  struct MemFSEntry*  entry,
        _In_  mstring_t*          name,
        _In_  uint32_t            owner,
        _In_  uint32_t            flags,
        _In_  uint32_t            permissions,
        _Out_ struct MemFSEntry** entryOut)
{
    struct __DirectoryEntry* lookup;
    struct MemFSEntry*       newEntry;

    usched_mtx_lock(&entry->Mutex);
    lookup = hashtable_get(&entry->Data.Directory.Entries, &(struct __DirectoryEntry) { .Name = name });
    if (lookup) {
        usched_mtx_unlock(&entry->Mutex);
        return OS_EEXISTS;
    }

    newEntry = __MemFSEntry_new(name, owner, __TypeFromFlags(flags), permissions);
    if (newEntry == NULL) {
        usched_mtx_unlock(&entry->Mutex);
        return OS_EOOM;
    }

    *entryOut = newEntry;

    // Important to note here that we set .Name to the name pointer *inside* the
    // entry structure, as we must always use memory owned by the entry.
    hashtable_set(
            &entry->Data.Directory.Entries,
            &(struct __DirectoryEntry) { .Name = newEntry->Name, newEntry }
    );
    usched_mtx_unlock(&entry->Mutex);
    return OS_EOK;
}

static oserr_t
__MemFSCreate(
        _In_  struct VFSInterface* interface,
        _In_  void*                instanceData,
        _In_  void*                data,
        _In_  mstring_t*           name,
        _In_  uint32_t             owner,
        _In_  uint32_t             flags,
        _In_  uint32_t             permissions,
        _Out_ void**               dataOut)
{
    //struct MemFS*       memfs  = instanceData;
    struct MemFSHandle* handle = data;
    struct MemFSHandle* newHandle;
    oserr_t             oserr;
    struct MemFSEntry*  entry;

    TRACE("__MemFSCreate(name=%ms)", name);
    if (instanceData == NULL || data == NULL) {
        return OS_EINVALPARAMS;
    }

    // The handle must point to a directory
    if (handle->Entry->Type != MEMFS_ENTRY_TYPE_DIRECTORY) {
        WARNING("__MemFSCreate %ms is not a directory (%i)", handle->Entry->Name, handle->Entry->Type);
        return OS_ENOTDIR;
    }

    oserr = __CreateInNode(handle->Entry, name, owner, flags, permissions, &entry);
    if (oserr != OS_EOK) {
        return oserr;
    }

    usched_mtx_lock(&entry->Mutex);
    newHandle = __MemFSHandle_new(entry);
    if (newHandle == NULL) {
        usched_mtx_unlock(&entry->Mutex);
        return OS_EOOM;
    }

    // Update the accessed time of entry before continuing
    OSGetTime(OSTimeSource_UTC, &entry->Accessed);
    usched_mtx_unlock(&entry->Mutex);
    *dataOut = newHandle;
    return OS_EOK;
}

static oserr_t
__MemFSClose(
        _In_ struct VFSInterface* interface,
        _In_ void*                instanceData,
        _In_ void*                data)
{
    TRACE("__MemFSClose()");
    if (instanceData == NULL || data == NULL) {
        return OS_EINVALPARAMS;
    }

    __MemFSHandle_delete(data);
    return OS_EOK;
}

static oserr_t
__MemFSStat(
        _In_ struct VFSInterface* interface,
        _In_ void*                instanceData,
        _In_ struct VFSStatFS*    stat)
{
    TRACE("__MemFSStat()");
    if (instanceData == NULL) {
        return OS_EINVALPARAMS;
    }

    stat->Label = NULL;
    stat->BlockSize = 512;
    stat->BlocksPerSegment = 1;
    stat->MaxFilenameLength = 255;
    stat->SegmentsFree = 0;
    stat->SegmentsTotal = 0;
    return OS_EOK;
}

static oserr_t __SymlinkInNode(
        _In_  struct MemFSEntry*  entry,
        _In_  mstring_t*          name,
        _In_  mstring_t*          target,
        _In_  uint32_t            owner,
        _Out_ struct MemFSEntry** entryOut)
{
    struct __DirectoryEntry* lookup;
    struct MemFSEntry*       newEntry;
    TRACE("__SymlinkInNode(node=%ms, name=%ms, target=%ms)", entry->Name, name, target);

    usched_mtx_lock(&entry->Mutex);
    lookup = hashtable_get(&entry->Data.Directory.Entries, &(struct __DirectoryEntry) { .Name = name });
    if (lookup) {
        usched_mtx_unlock(&entry->Mutex);
        return OS_EEXISTS;
    }

    newEntry = __MemFSEntry_new(
            name, owner, MEMFS_ENTRY_TYPE_SYMLINK,
            FILE_PERMISSION_WRITE | FILE_PERMISSION_READ | FILE_PERMISSION_EXECUTE
    );
    if (newEntry == NULL) {
        usched_mtx_unlock(&entry->Mutex);
        return OS_EOOM;
    }

    // setup symlink-specific data
    newEntry->Data.Symlink.Target = mstr_clone(target);
    if (newEntry->Data.Symlink.Target == NULL) {
        usched_mtx_unlock(&entry->Mutex);
        __MemFSEntry_delete(newEntry);
        return OS_EOOM;
    }

    *entryOut = newEntry;

    // Important to note here that we set .Name to the name pointer *inside* the
    // entry structure, as we must always use memory owned by the entry.
    hashtable_set(
            &entry->Data.Directory.Entries,
            &(struct __DirectoryEntry) { .Name = newEntry->Name, newEntry }
    );
    usched_mtx_unlock(&entry->Mutex);
    return OS_EOK;
}

static oserr_t
__MemFSLink(
        _In_ struct VFSInterface* interface,
        _In_ void*                instanceData,
        _In_ void*                data,
        _In_ mstring_t*           linkName,
        _In_ mstring_t*           linkTarget,
        _In_ int                  symbolic)
{
    //struct MemFS*       memfs  = instanceData;
    struct MemFSHandle* handle = data;
    oserr_t             oserr;
    struct MemFSEntry*  entry;

    TRACE("__MemFSLink(name=%ms, target=%ms)", linkName, linkTarget);
    if (instanceData == NULL || data == NULL) {
        return OS_EINVALPARAMS;
    }

    // The handle must point to a directory
    if (handle->Entry->Type != MEMFS_ENTRY_TYPE_DIRECTORY) {
        WARNING("__MemFSLink %ms is not a directory (%i)", handle->Entry->Name, handle->Entry->Type);
        return OS_ENOTDIR;
    }

    if (symbolic) {
        oserr = __SymlinkInNode(
                handle->Entry,
                linkName,
                linkTarget,
                handle->Entry->UserID,
                &entry
        );
        if (oserr != OS_EOK) {
            return oserr;
        }

        // Update the accessed time of entry before continuing
        usched_mtx_lock(&entry->Mutex);
        OSGetTime(OSTimeSource_UTC, &entry->Accessed);
        usched_mtx_unlock(&entry->Mutex);
    } else {
        // TODO hard links
        oserr = OS_ENOTSUPPORTED;
    }
    return oserr;
}

static oserr_t
__MemFSUnlink(
        _In_  struct VFSInterface* interface,
        _In_ void*                 instanceData,
        _In_ mstring_t*            path)
{
    struct MemFS*      memfs = instanceData;
    struct MemFSEntry* parent;
    struct MemFSEntry* node;
    oserr_t            oserr;

    TRACE("__MemFSUnlink(path=%ms)", path);
    if (instanceData == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = __FindNode(memfs->Root, path, &parent, &node);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Remove it from the parent node
    usched_mtx_lock(&parent->Mutex);
    hashtable_remove(
            &parent->Data.Directory.Entries,
            &(struct __DirectoryEntry) { .Name = node->Name }
    );
    usched_mtx_unlock(&parent->Mutex);

    // Remove one link to the entry
    __MemFSEntry_delete(node);
    return OS_EOK;
}

static oserr_t
__MemFSReadLink(
        _In_  struct VFSInterface* interface,
        _In_ void*                 instanceData,
        _In_ mstring_t*            path,
        _In_ mstring_t**           pathOut)
{
    struct MemFS*      memfs = instanceData;
    struct MemFSEntry* parent;
    struct MemFSEntry* node;
    oserr_t            oserr;

    TRACE("__MemFSReadLink(path=%ms)", path);
    if (instanceData == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = __FindNode(memfs->Root, path, &parent, &node);
    if (oserr != OS_EOK) {
        return oserr;
    }

    if (node->Type != MEMFS_ENTRY_TYPE_SYMLINK) {
        return OS_ENOTSUPPORTED;
    }

    *pathOut = mstr_clone(node->Data.Symlink.Target);
    if (*pathOut == NULL) {
        return OS_EOOM;
    }
    return OS_EOK;
}

static oserr_t
__MemFSMove(
        _In_ struct VFSInterface* interface,
        _In_ void*                instanceData,
        _In_ mstring_t*           from,
        _In_ mstring_t*           to,
        _In_ int                  copy)
{
    TRACE("__MemFSMove(from=%ms, to=%ms, copy=%i)", from, to, copy);
    if (instanceData == NULL) {
        return OS_EINVALPARAMS;
    }

    return OS_ENOTSUPPORTED;
}

static oserr_t __ReadFile(
        _In_  struct MemFSHandle* handle,
        _In_  void*               buffer,
        _In_  size_t              bufferOffset,
        _In_  size_t              unitCount,
        _Out_ size_t*             unitsRead)
{
    size_t bytesToRead;

    TRACE("__ReadFile()");

    // Calculate the number of bytes to read based on current position
    // and the size of the file.
    bytesToRead = MIN((handle->Entry->Data.File.BufferSize - handle->Position), unitCount);

    // Copy that number of bytes, but guard against zero-reads
    if (bytesToRead) {
        memcpy(
                (uint8_t*)buffer + bufferOffset,
                (uint8_t*)handle->Entry->Data.File.Buffer + handle->Position,
                bytesToRead
        );
        handle->Position += bytesToRead;
    }

    *unitsRead = bytesToRead;
    return OS_EOK;
}

struct __ReadDirectoryContext {
    uint8_t* Buffer;
    size_t   BytesLeftInBuffer;
    int      EntriesToSkip;
    int      EntriesRead;
};

static uint32_t __TypeToFlags(int type) {
    switch (type) {
        case MEMFS_ENTRY_TYPE_DIRECTORY: return FILE_FLAG_DIRECTORY;
        case MEMFS_ENTRY_TYPE_FILE: return FILE_FLAG_FILE;
        case MEMFS_ENTRY_TYPE_SYMLINK: return FILE_FLAG_LINK;
        default: return 0;
    }
}

static void __ReadDirectoryEntry(int index, const void* element, void* userContext)
{
    const struct __DirectoryEntry* entry   = element;
    struct __ReadDirectoryContext* context = userContext;
    struct VFSDirectoryEntry*      entryOut = (struct VFSDirectoryEntry*)context->Buffer;
    size_t                         entrySize;
    char*                          name = (char*)(context->Buffer + sizeof(struct VFSDirectoryEntry));
    char*                          nameu8;
    TRACE("__ReadDirectoryEntry(entry=%ms)", entry->Name);

    // ensure enough buffer space for this
    entrySize = sizeof(struct VFSDirectoryEntry) + mstr_bsize(entry->Name);
    if (context->BytesLeftInBuffer < entrySize) {
        return;
    }

    // ensure that we have skipped enough entries
    if (context->EntriesToSkip) {
        context->EntriesToSkip--;
        return;
    }

    nameu8 = mstr_u8(entry->Name);
    if (nameu8 == NULL) {
        return;
    }

    // Fill in the VFS structure
    entryOut->NameLength = strlen(nameu8) + 1;
    entryOut->LinkLength = 0; // TODO support for links
    entryOut->UserID = entry->Entry->UserID;
    entryOut->GroupID = entry->Entry->GroupID;
    entryOut->Size = (entry->Entry->Type == MEMFS_ENTRY_TYPE_FILE) ? entry->Entry->Data.File.BufferSize : 0;
    entryOut->SizeOnDisk = 0;
    entryOut->Permissions = entry->Entry->Permissions;
    entryOut->Flags = __TypeToFlags(entry->Entry->Type);
    __CopyTime(&entryOut->Accessed, &entry->Entry->Accessed);
    __CopyTime(&entryOut->Modified, &entry->Entry->Modified);
    __CopyTime(&entryOut->Created, &entry->Entry->Created);
    memcpy(name, nameu8, entryOut->NameLength);
    free(nameu8);

    // Update iterators
    context->EntriesRead++;
    context->Buffer += (sizeof(struct VFSDirectoryEntry) + entryOut->NameLength);
    context->BytesLeftInBuffer -= (sizeof(struct VFSDirectoryEntry) + entryOut->NameLength);
}

static oserr_t __ReadDirectory(
        _In_  struct MemFSHandle* handle,
        _In_  void*               buffer,
        _In_  size_t              bufferOffset,
        _In_  size_t              unitCount,
        _Out_ size_t*             unitsRead)
{
    struct __ReadDirectoryContext context = {
            .Buffer = (uint8_t*)buffer + bufferOffset,
            .BytesLeftInBuffer = unitCount,
            .EntriesToSkip = (int)handle->Position,
            .EntriesRead = 0
    };
    TRACE("__ReadDirectory()");

    // Ensure that a minimum of *one* stat structure can fit
    if (unitCount < sizeof(struct VFSDirectoryEntry)) {
        *unitsRead = 0;
        return OS_ECANCELLED;
    }

    usched_mtx_lock(&handle->Entry->Mutex);
    hashtable_enumerate(
            &handle->Entry->Data.Directory.Entries,
            __ReadDirectoryEntry, &context
    );
    usched_mtx_unlock(&handle->Entry->Mutex);

    handle->Position += context.EntriesRead;
    *unitsRead = unitCount - context.BytesLeftInBuffer;
    return OS_EOK;
}

static oserr_t
__MapUserBufferRead(
        _In_ uuid_t      handle,
        _In_ OSHandle_t* shm)
{
    oserr_t oserr;

    oserr = SHMAttach(handle, shm);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // When mapping the buffer for reading, we need write access to the buffer,
    // so we can do buffer combining.
    oserr = SHMMap(shm, 0, SHMBufferCapacity(shm), SHM_ACCESS_READ | SHM_ACCESS_WRITE);
    if (oserr != OS_EOK) {
        OSHandleDestroy(shm);
        return oserr;
    }
    return OS_EOK;
}

static oserr_t
__MemFSRead(
        _In_  struct VFSInterface* interface,
        _In_  void*                instanceData,
        _In_  void*                data,
        _In_  uuid_t               bufferHandle,
        _In_  void*                buffer,
        _In_  size_t               bufferOffset,
        _In_  size_t               unitCount,
        _Out_ size_t*              unitsRead)
{
    struct MemFSHandle* handle = data;
    OSHandle_t          shmHandle;
    void*               pointer;
    oserr_t             oserr;

    TRACE("__MemFSRead(entry=%ms)", handle ? handle->Entry->Name : NULL);
    if (instanceData == NULL || data == NULL) {
        return OS_EINVALPARAMS;
    }

    // All data is sourced locally, so we do not really need to use
    // the buffer-handle in this type of FS.
    if (buffer == NULL) {
        oserr = __MapUserBufferRead(
                bufferHandle,
                &shmHandle
        );
        if (oserr != OS_EOK) {
            return oserr;
        }
        pointer = SHMBuffer(&shmHandle);
    } else {
        // use the provided local buffer
        pointer = buffer;
    }

    // Handle reading of data differently based on the type of file
    // entry.
    switch (handle->Entry->Type) {
        case MEMFS_ENTRY_TYPE_FILE:
            oserr = __ReadFile(handle, pointer, bufferOffset, unitCount, unitsRead);
            break;
        case MEMFS_ENTRY_TYPE_DIRECTORY:
            oserr = __ReadDirectory(handle, pointer, bufferOffset, unitCount, unitsRead);
            break;
        default:
            oserr = OS_ENOTSUPPORTED;
            break;
    }
    if (buffer == NULL) {
        OSHandleDestroy(&shmHandle);
    }
    return oserr;
}

static oserr_t
__MemFSWrite(
        _In_  struct VFSInterface* interface,
        _In_  void*                instanceData,
        _In_  void*                data,
        _In_  uuid_t               bufferHandle,
        _In_  const void*          buffer,
        _In_  size_t               bufferOffset,
        _In_  size_t               unitCount,
        _Out_ size_t*              unitsWritten)
{
    TRACE("__MemFSWrite()");
    if (instanceData == NULL) {
        return OS_EINVALPARAMS;
    }

    return OS_ENOTSUPPORTED;
}

static oserr_t
__MemFSTruncate(
        _In_ struct VFSInterface* interface,
        _In_ void*                instanceData,
        _In_ void*                data,
        _In_ uint64_t             size)
{
    TRACE("__MemFSTruncate()");
    if (instanceData == NULL) {
        return OS_EINVALPARAMS;
    }

    return OS_ENOTSUPPORTED;
}

static oserr_t
__MemFSSeek(
        _In_  struct VFSInterface* interface,
        _In_  void*                instanceData,
        _In_  void*                data,
        _In_  uint64_t             absolutePosition,
        _Out_ uint64_t*            absolutePositionOut)
{
    TRACE("__MemFSSeek()");
    if (instanceData == NULL) {
        return OS_EINVALPARAMS;
    }

    return OS_ENOTSUPPORTED;
}

static struct VFSOperations g_memfsOperations = {
        .Initialize = __MemFSInitialize,
        .Destroy = __MemFSDestroy,
        .Stat = __MemFSStat,
        .Open = __MemFSOpen,
        .Close = __MemFSClose,
        .Link = __MemFSLink,
        .Unlink = __MemFSUnlink,
        .ReadLink = __MemFSReadLink,
        .Create = __MemFSCreate,
        .Move = __MemFSMove,
        .Truncate = __MemFSTruncate,
        .Read = __MemFSRead,
        .Write = __MemFSWrite,
        .Seek = __MemFSSeek
};

struct VFSInterface* MemFSNewInterface(void) {
    return VFSInterfaceNew(UUID_INVALID, &g_memfsOperations);
}

static uint64_t __directory_entries_hash(const void* element)
{
    const struct __DirectoryEntry* entry = element;
    return mstr_hash(entry->Name);
}

static int __directory_entries_cmp(const void* element1, const void* element2)
{
    const struct __DirectoryEntry* entry1 = element1;
    const struct __DirectoryEntry* entry2 = element2;
    return mstr_cmp(entry1->Name, entry2->Name);
}
