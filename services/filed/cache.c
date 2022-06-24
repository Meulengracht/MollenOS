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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * File and storage service, File Cache implementation
 *   Handles caching of all file entries for quick retrieval
 */

//#define __TRACE

#include <assert.h>
#include <ddk/utils.h>
#include <ds/hashtable.h>
#include <ds/hash_sip.h>
#include <vfs/cache.h>
#include <vfs/filesystem.h>
#include <stdlib.h>

struct cache_entry_wrapper {
    struct FileSystemCacheEntry* entry;
    MString_t*                   path;
};

static uint64_t file_hash(const void*);
static int      file_cmp(const void*, const void*);

static uint8_t g_hashKey[16] = {196, 179, 43, 202, 48, 240, 236, 199, 229, 122, 94, 143, 20, 251, 63, 66 };

static inline int __IsEntryFile(FileSystemEntryBase_t* entry)
{
    return (entry->Descriptor.Flags & FILE_FLAG_DIRECTORY) == 0 ? 1 : 0;
}

void
VfsFileSystemCacheInitialize(
        _In_ FileSystem_t* fileSystem)
{
    hashtable_construct(&fileSystem->cache,
                        HASHTABLE_MINIMUM_CAPACITY,
                        sizeof(struct cache_entry_wrapper),
                        file_hash, file_cmp);
}

static struct cache_entry_wrapper*
AddEntryToCache(
        _In_ FileSystem_t*          fileSystem,
        _In_ FileSystemEntryBase_t* base,
        _In_ MString_t*             path)
{
    struct FileSystemCacheEntry* entry;
    struct cache_entry_wrapper*  result;

    entry = malloc(sizeof(struct FileSystemCacheEntry));
    if (!entry) {
        return NULL;
    }

    entry->filesystem = fileSystem;
    entry->base       = base;
    entry->path       = MStringClone(path);
    entry->references = 0;
    usched_mtx_init(&entry->lock);
    list_construct(&entry->handles);

    usched_mtx_lock(&fileSystem->lock);
    result = hashtable_get(&fileSystem->cache, &(struct cache_entry_wrapper) { .path = path });
    if (!result) {
        hashtable_set(&fileSystem->cache, &(struct cache_entry_wrapper) { .entry = entry, .path = path });
        result = hashtable_get(&fileSystem->cache, &(struct cache_entry_wrapper) { .path = path });
    }
    else {
        // entry was already added
        free(entry);
    }
    usched_mtx_unlock(&fileSystem->lock);
    return result;
}

OsStatus_t
VfsFileSystemCacheGet(
        _In_  FileSystem_t*            fileSystem,
        _In_  MString_t*               subPath,
        _In_  unsigned int             options,
        _Out_ FileSystemCacheEntry_t** entryOut)
{
    struct cache_entry_wrapper* cacheEntry;
    OsStatus_t                  status;
    int                         created = 0;

    TRACE("VfsFileSystemCacheGet(subPath=%s)", MStringRaw(subPath));
    if (!fileSystem || !subPath || !entryOut) {
        return OsInvalidParameters;
    }

    usched_mtx_lock(&fileSystem->lock);
    cacheEntry = hashtable_get(&fileSystem->cache, &(struct cache_entry_wrapper) { .path = subPath });
    usched_mtx_unlock(&fileSystem->lock);

    if (!cacheEntry) {
        FileSystemEntryBase_t* entry;

        // Let the module do the rest
        status = fileSystem->module->OpenEntry(&fileSystem->base, subPath, &entry);
        if (status == OsNotExists && (options & (__FILE_CREATE | __FILE_CREATE_RECURSIVE))) {
            TRACE("VfsCacheGetFile file was not found, but options are to create 0x%x", options);
            status  = fileSystem->module->CreatePath(&fileSystem->base, subPath, options, &entry);
            created = 1;
        }

        if (status != OsOK) {
            WARNING("VfsCacheGetFile %s opening/creation failed with code: %i", MStringRaw(subPath), status);
            return status;
        }

        // Take care of truncation flag if file was not newly created. The entry type
        // must equal to file otherwise we will ignore the flag
        if ((options & __FILE_TRUNCATE) && created == 0 && __IsEntryFile(entry)) {
            status = fileSystem->module->ChangeFileSize(&fileSystem->base, entry, 0);
        }

        cacheEntry = AddEntryToCache(fileSystem, entry, subPath);
    }

    if ((options & __FILE_FAILONEXIST) && created == 0) {
        WARNING("VfsCacheGetFile %s fail on exist was specified, path exists", MStringRaw(subPath), status);
        return OsExists;
    }

    *entryOut = (cacheEntry ? cacheEntry->entry : NULL);
    return OsOK;
}

void
VfsFileSystemCacheRemove(
        _In_  FileSystem_t* fileSystem,
        _In_  MString_t*    subPath)
{
    struct cache_entry_wrapper* wrapper;
    assert(fileSystem != NULL);
    assert(subPath != NULL);

    TRACE("VfsCacheRemoveFile(subPath=%s)", MStringRaw(subPath));

    // just remove it from the hash-table
    usched_mtx_lock(&fileSystem->lock);
    wrapper = hashtable_remove(&fileSystem->cache, &(struct cache_entry_wrapper) { .path = subPath });
    if (wrapper) {
        fileSystem->module->CloseEntry(&fileSystem->base, wrapper->entry->base);
        MStringDestroy(wrapper->entry->path);
        free(wrapper->entry);
    }
    usched_mtx_unlock(&fileSystem->lock);
}

static uint64_t file_hash(const void* element)
{
    const struct cache_entry_wrapper* cacheEntry = element;
    if (!MStringLength(cacheEntry->path)) {
        return 0;
    }
    return siphash_64((const uint8_t*)MStringRaw(cacheEntry->path), MStringLength(cacheEntry->path), &g_hashKey[0]);
}

static int file_cmp(const void* element1, const void* element2)
{
    const struct cache_entry_wrapper* cacheEntry1 = element1;
    const struct cache_entry_wrapper* cacheEntry2 = element2;
    return MStringCompare(cacheEntry1->path, cacheEntry2->path, 0) == MSTRING_FULL_MATCH ? 0 : 1;
}
