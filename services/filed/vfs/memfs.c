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

#include <ddk/utils.h>
#include <ds/hashtable.h>
#include <os/usched/mutex.h>
#include <stdlib.h>
#include <string.h>
#include <vfs/vfs.h>
#include "private.h"

struct MemFSDirectory {
    hashtable_t Entries;
};

enum {
    MEMFS_ENTRY_TYPE_INVALID,
    MEMFS_ENTRY_TYPE_FILE,
    MEMFS_ENTRY_TYPE_DIRECTORY,
};

struct MemFSEntry {
    mstring_t*        Name;
    uint32_t          Owner;
    struct usched_mtx Mutex;
    int               Type;
    union {
        struct MemFSDirectory Directory;
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
    struct MemFSEntry* Entry;
};

static mstring_t g_rootName = mstr_const(U"/");

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

    if (entry->Type == MEMFS_ENTRY_TYPE_DIRECTORY) {
        hashtable_enumerate(&entry->Data.Directory.Entries, __directory_entries_delete, NULL);
        hashtable_destroy(&entry->Data.Directory.Entries);
    }
    mstr_delete(entry->Name);
    free(entry);
}

static int __MemFSDirectory_construct(struct MemFSDirectory* directory)
{
    return hashtable_construct(&directory->Entries, 0, sizeof(struct __DirectoryEntry),
                               __directory_entries_hash, __directory_entries_cmp);
}

static struct MemFSEntry* __MemFSEntry_new(mstring_t* name, int Type)
{
    struct MemFSEntry* entry = malloc(sizeof(struct MemFSEntry));
    if (entry == NULL) {
        return NULL;
    }
    memset(entry, 0, sizeof(struct MemFSEntry));

    entry->Type = Type;
    if (Type == MEMFS_ENTRY_TYPE_DIRECTORY) {
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

    usched_mtx_init(&entry->Mutex);
    entry->Owner = UUID_INVALID;
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

    memfs->Root = __MemFSEntry_new(&g_rootName, MEMFS_ENTRY_TYPE_DIRECTORY);
    if (memfs->Root == NULL) {
        __MemFS_delete(memfs);
        return NULL;
    }
    return memfs;
}

static oserr_t
__MemFSInitialize(
        _In_ struct VFSCommonData* vfsCommonData)
{
    TRACE("__MemFSInitialize()");

    struct MemFS* memfs = __MemFS_new();
    if (memfs == NULL) {
        return OsOutOfMemory;
    }

    vfsCommonData->Data = memfs;
    return OsOK;
}

static oserr_t
__MemFSDestroy(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ unsigned int          unmountFlags)
{
    TRACE("__MemFSDestroy()");
    if (vfsCommonData->Data == NULL) {
        return OsInvalidParameters;
    }
    __MemFS_delete(vfsCommonData->Data);
    return OsOK;
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

    handle->Entry = entry;
    return handle;
}

static oserr_t __FindNode(struct MemFSEntry* root, mstring_t* path, struct MemFSEntry** entryOut)
{
    mstring_t** tokens;
    int         tokensCount;
    oserr_t     oserr = OsOK;

    TRACE("__FindNode(root=%ms, path=%ms)", root->Name, path);

    // Special case, we are trying to open root path
    if (__PathIsRoot(path)) {
        *entryOut = root;
        return OsOK;
    }

    // Otherwise split path into tokens and find the entry requested
    tokensCount = mstr_path_tokens(path, &tokens);
    if (tokensCount <= 0) {
        return OsInvalidParameters;
    }

    struct MemFSEntry* n = root;
    for (int i = 0; i < tokensCount; i++) {
        struct __DirectoryEntry* directoryEntry;
        struct MemFSEntry*       entry = NULL;

        usched_mtx_lock(&n->Mutex);
        directoryEntry = hashtable_get(&n->Data.Directory.Entries, &(struct __DirectoryEntry) {
            .Name = tokens[i]
        });
        if (directoryEntry != NULL) {
            entry = directoryEntry->Entry;
        }
        usched_mtx_unlock(&n->Mutex);

        if (entry == NULL) {
            oserr = OsNotExists;
            break;
        }

        // If we are not at last token, then the entry *must* be a directory
        // otherwise it'll not be good for this loop.
        if (i != (tokensCount - 1) && entry->Type != MEMFS_ENTRY_TYPE_DIRECTORY) {
            oserr = OsPathIsNotDirectory;
            break;
        }
        n = entry;
    }

    mstrv_delete(tokens);
    *entryOut = n;
    return oserr;
}

static oserr_t
__MemFSOpen(
        _In_      struct VFSCommonData* vfsCommonData,
        _In_      mstring_t*            path,
        _Out_Opt_ void**                dataOut)
{
    struct MemFS*       memfs = vfsCommonData->Data;
    struct MemFSHandle* handle;
    struct MemFSEntry*  entry;
    oserr_t             oserr;

    TRACE("__MemFSOpen(path=%ms)", path);
    if (vfsCommonData->Data == NULL) {
        return OsInvalidParameters;
    }

    oserr = __FindNode(memfs->Root, path, &entry);
    if (oserr != OsOK) {
        return oserr;
    }

    handle = __MemFSHandle_new(entry);
    if (handle == NULL) {
        return OsOutOfMemory;
    }

    *dataOut = handle;
    return OsOK;
}

static oserr_t __CreateInNode(
        _In_ struct MemFSEntry* entry, mstring_t* name)
{

}

static oserr_t
__MemFSCreate(
        _In_  struct VFSCommonData* vfsCommonData,
        _In_  void*                 data,
        _In_  mstring_t*            name,
        _In_  uint32_t              owner,
        _In_  uint32_t              flags,
        _In_  uint32_t              permissions,
        _Out_ void**                dataOut)
{
    struct MemFS*       memfs  = vfsCommonData->Data;
    struct MemFSHandle* handle = data;

    TRACE("__MemFSCreate(name=%ms)", name);
    if (vfsCommonData->Data == NULL || data == NULL) {
        return OsInvalidParameters;
    }

    return __CreateInNode(handle->Entry, name, ownher, flags, permissions);
}

static oserr_t
__MemFSClose(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ void*                 data)
{
    TRACE("__MemFSClose()");
    if (vfsCommonData->Data == NULL || data == NULL) {
        return OsInvalidParameters;
    }

    __MemFSHandle_delete(data);
    return OsOK;
}

static oserr_t
__MemFSStat(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ struct VFSStatFS*     stat)
{
    TRACE("__MemFSStat()");
    if (vfsCommonData->Data == NULL) {
        return OsInvalidParameters;
    }

    return OsOK;
}

static oserr_t
__MemFSLink(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ void*                 data,
        _In_ mstring_t*            linkName,
        _In_ mstring_t*            linkTarget,
        _In_ int                   symbolic)
{
    TRACE("__MemFSLink()");
    if (vfsCommonData->Data == NULL) {
        return OsInvalidParameters;
    }

    return OsOK;
}

static oserr_t
__MemFSUnlink(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ mstring_t*            path)
{
    TRACE("__MemFSUnlink()");
    if (vfsCommonData->Data == NULL) {
        return OsInvalidParameters;
    }

    return OsOK;
}

static oserr_t
__MemFSReadLink(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ mstring_t*            path,
        _In_ mstring_t*            pathOut)
{
    TRACE("__MemFSReadLink()");
    if (vfsCommonData->Data == NULL) {
        return OsInvalidParameters;
    }

    return OsOK;
}

static oserr_t
__MemFSMove(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ mstring_t*            from,
        _In_ mstring_t*            to,
        _In_ int                   copy)
{
    TRACE("__MemFSMove(from=%ms, to=%ms, copy=%i)", from, to, copy);
    if (vfsCommonData->Data == NULL) {
        return OsInvalidParameters;
    }

    return OsOK;
}

static oserr_t
__MemFSRead(
        _In_  struct VFSCommonData* vfsCommonData,
        _In_  void*                 data,
        _In_  uuid_t                bufferHandle,
        _In_  void*                 buffer,
        _In_  size_t                bufferOffset,
        _In_  size_t                unitCount,
        _Out_ size_t*               unitsRead)
{
    TRACE("__MemFSRead()");
    if (vfsCommonData->Data == NULL) {
        return OsInvalidParameters;
    }

    return OsOK;
}

static oserr_t
__MemFSWrite(
        _In_  struct VFSCommonData* vfsCommonData,
        _In_  void*                 data,
        _In_  uuid_t                bufferHandle,
        _In_  void*                 buffer,
        _In_  size_t                bufferOffset,
        _In_  size_t                unitCount,
        _Out_ size_t*               unitsWritten)
{
    TRACE("__MemFSWrite()");
    if (vfsCommonData->Data == NULL) {
        return OsInvalidParameters;
    }

    return OsOK;
}

static oserr_t
__MemFSTruncate(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ void*                 data,
        _In_ uint64_t              size)
{
    TRACE("__MemFSTruncate()");
    if (vfsCommonData->Data == NULL) {
        return OsInvalidParameters;
    }

    return OsOK;
}

static oserr_t
__MemFSSeek(
        _In_  struct VFSCommonData* vfsCommonData,
        _In_  void*                 data,
        _In_  uint64_t              absolutePosition,
        _Out_ uint64_t*             absolutePositionOut)
{
    TRACE("__MemFSSeek()");
    if (vfsCommonData->Data == NULL) {
        return OsInvalidParameters;
    }

    return OsOK;
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
    return VFSInterfaceNew(FileSystemType_MEMFS, NULL, &g_memfsOperations);
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
