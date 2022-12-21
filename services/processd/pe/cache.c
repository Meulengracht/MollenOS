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

//#define __TRACE

#include <errno.h>
#include <ddk/utils.h>
#include <module.h>
#include "private.h"
#include "process.h"
#include <stdlib.h>
#include <stdio.h>

struct __PathEntry {
    mstring_t* Path;
    uint32_t   ModuleHash;
};

struct __ModuleLoaderEntry {
    uint32_t       Hash;
    int            References;
    struct Module* Module;
};

struct MappingManager {
    hashtable_t       Paths;    // hashtable<string, hash>
    struct usched_mtx PathsMutex;
    hashtable_t       Modules;  // hashtable<hash, module>
    struct usched_mtx ModulesMutex;
};

static uint64_t __module_hash(const void* element);
static int      __module_cmp(const void* element1, const void* element2);
static uint64_t __path_hash(const void* element);
static int      __path_cmp(const void* element1, const void* element2);

static struct MappingManager g_mapper = { 0 };

oserr_t
PECacheInitialize(void)
{
    int status;

    usched_mtx_init(&g_mapper.ModulesMutex, USCHED_MUTEX_PLAIN);
    usched_mtx_init(&g_mapper.PathsMutex, USCHED_MUTEX_PLAIN);

    status = hashtable_construct(
            &g_mapper.Modules, 0, sizeof(struct __ModuleLoaderEntry),
            __module_hash, __module_cmp);
    if (status) {
        return OS_EOOM;
    }

    status = hashtable_construct(
            &g_mapper.Paths, 0, sizeof(struct __PathEntry),
            __path_hash, __path_cmp);
    if (status) {
        return OS_EOOM;
    }
    return OS_EOK;
}

void
PECacheDestroy(void)
{
    hashtable_destroy(&g_mapper.Modules);
    hashtable_destroy(&g_mapper.Paths);
}

oserr_t
__LoadFile(
        _In_  mstring_t* fullPath,
        _Out_ void**     bufferOut,
        _Out_ size_t*    lengthOut)
{
    FILE*   file;
    long    fileSize;
    void*   fileBuffer;
    size_t  bytesRead;
    oserr_t osStatus = OS_EOK;
    char*   pathu8;
    ENTRY("__LoadFile %ms", fullPath);

    pathu8 = mstr_u8(fullPath);
    if (pathu8 == NULL) {
        return OS_EOOM;
    }

    // special case:
    // load from ramdisk
    if (mstr_find_u8(fullPath, "/initfs/", 0) != -1) {
        return PmBootstrapFindRamdiskFile(fullPath, bufferOut, lengthOut);
    }

    file = fopen(pathu8, "rb");
    free(pathu8);
    if (!file) {
        ERROR("__LoadFile fopen failed: %i", errno);
        osStatus = OS_ENOENT;
        goto exit;
    }

    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    rewind(file);

    TRACE("__LoadFile size %" PRIuIN, fileSize);
    fileBuffer = malloc(fileSize);
    if (!fileBuffer) {
        ERROR("__LoadFile null");
        fclose(file);
        osStatus = OS_EOOM;
        goto exit;
    }

    bytesRead = fread(fileBuffer, 1, fileSize, file);
    fclose(file);

    TRACE("__LoadFile read %" PRIuIN " bytes from file", bytesRead);
    if (bytesRead != fileSize) {
        osStatus = OS_EINCOMPLETE;
    }

    *bufferOut = fileBuffer;
    *lengthOut = fileSize;

exit:
EXIT("__LoadFile");
    return osStatus;
}

static oserr_t
__GetModuleHash(
        _In_  mstring_t* path,
        _Out_ uint32_t*  hashOut)
{
    struct __PathEntry* entry;
    oserr_t             oserr = OS_ENOENT;
    TRACE("__GetModuleHash(path=%ms)", path);
    usched_mtx_lock(&g_mapper.PathsMutex);
    entry = hashtable_get(
            &g_mapper.Paths,
            &(struct __PathEntry) {
                .Path = path
            }
    );
    if (entry) {
        *hashOut = entry->ModuleHash;
        oserr = OS_EOK;
    }
    usched_mtx_unlock(&g_mapper.PathsMutex);
    return oserr;
}

static void
__AddModuleHash(
        _In_ mstring_t* path,
        _In_ uint32_t   hash)
{
    TRACE("__AddModuleHash(path=%ms)", path);
    usched_mtx_lock(&g_mapper.PathsMutex);
    hashtable_set(
            &g_mapper.Paths,
            &(struct __PathEntry) {
                    .Path = path, .ModuleHash = hash
            }
    );
    usched_mtx_unlock(&g_mapper.PathsMutex);
}

static oserr_t
__GetModule(
        _In_  uint32_t        hash,
        _Out_ struct Module** moduleOut)
{
    struct __ModuleLoaderEntry* entry;
    oserr_t               oserr = OS_ENOENT;
    TRACE("__GetModule(hash=0x%x)", hash);
    usched_mtx_lock(&g_mapper.ModulesMutex);
    entry = hashtable_get(
            &g_mapper.Modules,
            &(struct __ModuleLoaderEntry) {
                .Hash = hash
            }
    );
    if (entry) {
        entry->References++;
        *moduleOut = entry->Module;
        oserr = OS_EOK;
    }
    usched_mtx_unlock(&g_mapper.ModulesMutex);
    return oserr;
}

static oserr_t
__AddModule(
        _In_ struct Module* module,
        _In_ uint32_t       moduleHash)
{
    struct __ModuleLoaderEntry entry;
    struct __ModuleLoaderEntry* existingEntry;
    oserr_t               oserr;
    TRACE("__AddModule(hash=0x%x)", moduleHash);

    entry.Hash = moduleHash;
    entry.References = 0;
    entry.Module = module;

    usched_mtx_lock(&g_mapper.ModulesMutex);
    existingEntry = hashtable_get(
            &g_mapper.Modules,
            &(struct __ModuleLoaderEntry) {
                .Hash = moduleHash
            }
    );
    if (existingEntry) {
        oserr = OS_EEXISTS;
    } else {
        hashtable_set(&g_mapper.Modules, &entry);
        oserr = OS_EOK;
    }
    usched_mtx_unlock(&g_mapper.ModulesMutex);
    return oserr;
}

oserr_t
__LoadModule(
        _In_  mstring_t* path,
        _Out_ uint32_t*  hashOut)
{
    void*          moduleBuffer;
    struct Module* module;
    size_t         bufferSize;
    oserr_t        oserr;
    TRACE("__LoadModule(path=%ms)", path);

    oserr = __LoadFile(path, (void **) &moduleBuffer, &bufferSize);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = PEValidateImageChecksum(moduleBuffer, bufferSize, hashOut);
    if (oserr != OS_EOK) {
        free(moduleBuffer);
        return oserr;
    }

    // Insert the hash with the path, now that it's calculated we atleast
    // do not need to do this again in the future for this absolute path.
    __AddModuleHash(path, *hashOut);
    // TODO: this has one huge weakness, and that is two scopes share paths
    // but in reality use different versions of programs. I think it's vital
    // that we are aware of the scope the caller is loading in. In reality,
    // we need to know this *anyway* as the caller may load a path that doesn't exist
    // in our root scope.

    // Pre-create the module instance, so we can try to insert it immediately,
    // and then modify it after insertion. This is to avoid too much pre-init
    // if the module already exists.
    module = ModuleNew(moduleBuffer, bufferSize);
    if (module == NULL) {
        return OS_EOOM;
    }

    oserr = __AddModule(module, *hashOut);
    if (oserr == OS_EEXISTS) {
        // Module was already loaded, but we are loading under a new path.
        // This is now registered, so we can actually just abort here and return
        // OK as it should find it now with the hash.
        ModuleDelete(moduleBuffer);
        return OS_EOK;
    }
    return oserr;
}

oserr_t
PECacheGet(
        _In_  mstring_t*      path,
        _Out_ struct Module** moduleOut)
{
    struct Module* module;
    uint32_t       moduleHash;
    oserr_t        oserr;
    TRACE("PECacheGet(path=%ms)", path);

    oserr = __GetModuleHash(path, &moduleHash);
    if (oserr != OS_EOK) {
        TRACE("PECacheGet module path entry was not stored, loading...");
        // path was not found, instantiate a load of the library
        oserr = __LoadModule(path, &moduleHash);
        if (oserr != OS_EOK) {
            ERROR("PECacheGet failed to load module: %u", oserr);
            return oserr;
        }
    }

    oserr = __GetModule(moduleHash, &module);
    if (oserr != OS_EOK) {
        // should not happen at this point
        ERROR("PECacheGet failed to find module hash (0x%x): %u", moduleHash, oserr);
        return oserr;
    }

    *moduleOut = module;
    return OS_EOK;
}

static uint64_t __module_hash(const void* element)
{
    const struct __ModuleLoaderEntry* moduleEntry = element;
    return moduleEntry->Hash;
}

static int __module_cmp(const void* element1, const void* element2)
{
    const struct __ModuleLoaderEntry* moduleEntry1 = element1;
    const struct __ModuleLoaderEntry* moduleEntry2 = element2;
    return moduleEntry1->Hash == moduleEntry2->Hash ? 0 : -1;
}

static uint64_t __path_hash(const void* element)
{
    const struct __PathEntry* pathEntry = element;
    return mstr_hash(pathEntry->Path);
}

static int __path_cmp(const void* element1, const void* element2)
{
    _CRT_UNUSED(element1);
    _CRT_UNUSED(element2);
    // we have no secondary keys to check against, assume the hash check is
    // enough.
    return 0;
}
