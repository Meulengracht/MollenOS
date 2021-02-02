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
 * File and storage service, File Cache implementation
 *   Handles caching of all file entries for quick retrieval
 */

//#define __TRACE

#include "include/vfs.h"
#include <ddk/utils.h>
#include <ds/hashtable.h>
#include <ds/hash_sip.h>

struct FileCacheEntry {
    MString_t*         path;
    FileSystemEntry_t* file;
};

static uint64_t      file_hash(const void*);
static int           file_cmp(const void*, const void*);
static FileSystem_t* __GetFileSystemFromPath(MString_t* path, MString_t** subPathOut);

static hashtable_t g_openFiles;
static uint8_t     g_hashKey[16] = {196, 179, 43, 202, 48, 240, 236, 199, 229, 122, 94, 143, 20, 251, 63, 66 };

static inline int __IsEntryFile(FileSystemEntry_t* entry)
{
    return (entry->Descriptor.Flags & FILE_FLAG_DIRECTORY) == 0 ? 1 : 0;
}

void
VfsCacheInitialize()
{
    hashtable_construct(&g_openFiles, HASHTABLE_MINIMUM_CAPACITY,
                        sizeof(struct FileCacheEntry), file_hash, file_cmp);
}

OsStatus_t
VfsCacheGetFile(
        _In_  MString_t*          path,
        _In_  unsigned int        options,
        _Out_ FileSystemEntry_t** fileOut)
{
    struct FileCacheEntry* cacheEntry;
    FileSystem_t*          Filesystem;
    FileSystemEntry_t*     entry   = NULL;
    MString_t*             subPath = NULL;
    OsStatus_t             status;
    int                    created = 0;

    TRACE("[vfs] [cache_get] path %s", MStringRaw(path));

    cacheEntry = hashtable_get(&g_openFiles, &(struct FileCacheEntry) { .path = path });
    if (cacheEntry) {
        *fileOut = cacheEntry->file;
        return OsSuccess;
    }

    Filesystem  = __GetFileSystemFromPath(path, &subPath);
    if (Filesystem == NULL) {
        return OsDoesNotExist;
    }

    // Let the module do the rest
    status = Filesystem->module->OpenEntry(&Filesystem->descriptor, subPath, &entry);
    if (status == OsDoesNotExist && (options & (__FILE_CREATE | __FILE_CREATE_RECURSIVE))) {
        TRACE("[vfs] [cache_get] file was not found, but options are to create 0x%x", options);
        status  = Filesystem->module->CreatePath(&Filesystem->descriptor, subPath, options, &entry);
        created = 1;
    }

    // Sanitize the open otherwise we must cleanup
    if (status == OsSuccess) {
        // It's important here that we check if the flag
        // __FILE_FAILONEXIST has been set, then we return
        // the appropriate code instead of opening a new handle
        // Also this is ok if file was just created
        if ((options & __FILE_FAILONEXIST) && created == 0) {
            ERROR("[vfs] [cache_get] entry already exists in path. FailOnExists has been specified.");
            status = Filesystem->module->CloseEntry(&Filesystem->descriptor, entry);
            entry  = NULL;
        }
        else {
            entry->System     = (uintptr_t*)Filesystem;
            entry->Path       = MStringCreate((void*)MStringRaw(path), StrUTF8);
            entry->References = 0;

            // Take care of truncation flag if file was not newly created. The entry type
            // must equal to file otherwise we will ignore the flag
            if ((options & __FILE_TRUNCATE) && created == 0 && __IsEntryFile(entry)) {
                status = Filesystem->module->ChangeFileSize(&Filesystem->descriptor, entry, 0);
            }

            *fileOut = entry;
            hashtable_set(&g_openFiles, &(struct FileCacheEntry) { .path = entry->Path, .file = entry });
        }
    }
    else {
        TRACE("[vfs] [cache_get] file opening/creation failed with code: %i", status);
        entry = NULL;
    }
    MStringDestroy(subPath);
    return status;
}

void
VfsCacheRemoveFile(
        _In_ MString_t* path)
{
    struct FileCacheEntry* entry;

    TRACE("[vfs] [cache_remove] path %s", MStringRaw(path));

    // just remove it from the hash-table
    entry = hashtable_remove(&g_openFiles, &(struct FileCacheEntry) { .path = path });
    if (entry) {
        MString_t*    subPath;
        FileSystem_t* fileSystem = __GetFileSystemFromPath(path, &subPath);
        if (fileSystem) {
            fileSystem->module->CloseEntry(&fileSystem->descriptor, entry->file);
            MStringDestroy(subPath);
        }
    }
}

static FileSystem_t* __GetFileSystemFromPath(MString_t* path, MString_t** subPathOut)
{
    element_t* header;
    MString_t* identifier;
    MString_t* subPath;
    int        index;

    // To open a new file we need to find the correct
    // filesystem identifier and seperate it from it's absolute path
    index = MStringFind(path, ':', 0);
    if (index == MSTRING_NOT_FOUND) {
        return NULL;
    }

    identifier = MStringSubString(path, 0, index);
    subPath    = MStringSubString(path, index + 2, -1);

    _foreach(header, VfsGetFileSystems()) {
        FileSystem_t* fileSystem = (FileSystem_t*)header->value;
        if (fileSystem->state != FSLoaded) {
            continue;
        }

        if (MStringCompare(identifier, fileSystem->identifier, 1)) {
            MStringDestroy(identifier);

            // set path out and return
            *subPathOut = subPath;
            return fileSystem;
        }
    }

    // clean up, not found
    MStringDestroy(identifier);
    MStringDestroy(subPath);
    return NULL;
}

static uint64_t file_hash(const void* element)
{
    const struct FileCacheEntry* cacheEntry = element;
    return siphash_64((const uint8_t*)MStringRaw(cacheEntry->path), MStringLength(cacheEntry->path), &g_hashKey[0]);
}

static int file_cmp(const void* element1, const void* element2)
{
    const struct FileCacheEntry* cacheEntry1 = element1;
    const struct FileCacheEntry* cacheEntry2 = element2;
    return MStringCompare(cacheEntry1->path, cacheEntry2->path, 0) == MSTRING_FULL_MATCH ? 0 : 1;
}
