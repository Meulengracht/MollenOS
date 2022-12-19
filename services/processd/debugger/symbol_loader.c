/**
 * MollenOS
 *
 * Copyright 2020, Philip Meulengracht
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
 * Process Manager - Symbol loader
 *  Contains the implementation of debugging facilities for the process manager which is
 *  invoked once a process crashes
 */

//#define __TRACE

#include <ddk/utils.h>
#include <ds/hashtable.h>
#include "symbols.h"
#include <stdio.h>
#include <malloc.h>

static oserr_t
SymbolLoadMapFile(
        _In_  mstring_t* moduleName,
        _Out_ void**     fileBufferOut,
        _Out_ long*      fileSizeOut);

static uint64_t __symbol_hash(const void* element);
static int      __symbol_cmp(const void* element1, const void* element2);

static hashtable_t g_loadedSymbolContexts;

void
SymbolInitialize(void)
{
    // Initialize the hashtable
    hashtable_construct(&g_loadedSymbolContexts,
                        HASHTABLE_MINIMUM_CAPACITY,
                        sizeof(struct symbol_context),
                        __symbol_hash,
                        __symbol_cmp);
}

oserr_t
SymbolsLoadContext(
        _In_  mstring_t*              moduleName,
        _Out_ struct symbol_context** symbolContextOut)
{
    // replace extension with .map and see if it exists
    struct symbol_context symbolContext = { 0 };
    long                  fileSize;
    void*                 fileBuffer;
    oserr_t               oserr;
    TRACE("SymbolsLoadContext(moduleName=%ms)", moduleName);

    oserr = SymbolLoadMapFile(moduleName, &fileBuffer, &fileSize);
    if (oserr != OS_EOK) {
        return oserr;
    }

    TRACE("[SymbolsLoadContext] parsing map file, 0x%llx - %llu", fileBuffer, fileSize);
    oserr = SymbolParseMapFile(&symbolContext, fileBuffer, fileSize);
    free(fileBuffer);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // ok create key now everything is done
    symbolContext.key = mstr_clone(moduleName);

    TRACE("[SymbolsLoadContext] context loaded %s", symbolContext.key);
    hashtable_set(
            &g_loadedSymbolContexts,
            &symbolContext
    );
    *symbolContextOut = hashtable_get(
            &g_loadedSymbolContexts,
            &(struct symbol_context) {
                .key = moduleName
            }
    );
    return OS_EOK;
}

oserr_t
SymbolLookup(
        _In_  mstring_t*   moduleName,
        _In_  uintptr_t    binaryOffset,
        _Out_ const char** symbolName,
        _Out_ uintptr_t*   symbolOffset)
{
    struct symbol_context* symbolContext;
    struct map_symbol*     symbol = NULL;
    oserr_t                status;

    if (moduleName == NULL) {
        return OS_EINVALPARAMS;
    }

    // Check for loaded context
    symbolContext = (struct symbol_context*)hashtable_get(
            &g_loadedSymbolContexts,
            &(struct symbol_context) {
                .key = moduleName
            }
    );
    if (!symbolContext) {
        status = SymbolsLoadContext(moduleName, &symbolContext);
        if (status != OS_EOK) {
            return status;
        }
    }

    // iterate symbols and find matching symbol, always selects last
    // symbol if the loop reaches end of list
    for (int i = 0; i < symbolContext->symbol_count; i++) {
        if (i == (symbolContext->symbol_count - 1)) {
            symbol = &symbolContext->symbols[i];
            break;
        }

        if (binaryOffset >= symbolContext->symbols[i].address &&
            binaryOffset <  symbolContext->symbols[i + 1].address) {
            symbol = &symbolContext->symbols[i];
            break;
        }
    }

    *symbolName   = symbol->name;
    *symbolOffset = binaryOffset - symbol->address;
    return OS_EOK;
}

static oserr_t
SymbolLoadMapFile(
        _In_  mstring_t* moduleName,
        _Out_ void**     fileBufferOut,
        _Out_ long*      fileSizeOut)
{
    FILE*      file;
    long       fileSize;
    void*      fileBuffer;
    size_t     bytesRead;
    mstring_t* mapFileName;
    mstring_t* mapFilePath;
    char*      mapFilePathu8;

    mapFileName = mstr_path_change_extension_u8(moduleName, ".map");
    mapFilePath = mstr_fmt("/initfs/maps/%ms", mapFileName);
    mapFilePathu8 = mstr_u8(mapFilePath);

    TRACE("[SymbolsLoadContext] trying to load %s", mapFilePathu8);
    file = fopen(mapFilePathu8, "r");
    // After the open call, we do not need any of the path data anymore, so lets
    // just free it immediately, so we don't have to take care of it anymore
    mstr_delete(mapFileName);
    mstr_delete(mapFilePath);
    if (!file) {
        // map did not exist
        ERROR("[SymbolsLoadContext] map file not found at %s", mapFilePathu8);
        free(mapFilePathu8);
        return OS_ENOENT;
    }
    free(mapFilePathu8);

    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    rewind(file);

    if (!fileSize) {
        fclose(file);
        return OS_EINVALPARAMS;
    }

    fileBuffer = malloc(fileSize);
    if (!fileBuffer) {
        fclose(file);
        return OS_EOOM;
    }

    bytesRead = fread(fileBuffer, 1, fileSize, file);
    fclose(file);

    if (bytesRead != fileSize) {
        ERROR("[SymbolsLoadContext] fread returned %i", (int)bytesRead);
        return OS_EUNKNOWN;
    }

    *fileBufferOut = fileBuffer;
    *fileSizeOut   = fileSize;
    return OS_EOK;
}

static uint64_t __symbol_hash(const void* element)
{
    const struct symbol_context* context = element;
    return mstr_hash(context->key);
}

static int __symbol_cmp(const void* element1, const void* element2)
{
    const struct symbol_context* lh = element1;
    const struct symbol_context* rh = element2;
    return mstr_cmp(lh->key, rh->key);
}
