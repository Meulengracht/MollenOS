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
#include <string.h>
#include <malloc.h>
#include <strings.h>
#include <ctype.h>

static oserr_t
SymbolLoadMapFile(
        _In_  const char* binaryName,
        _Out_ void**      fileBufferOut,
        _Out_ long*       fileSizeOut);

static uint64_t SymbolContextHash(const void* element);
static int      SymbolContextCmp(const void* element1, const void* element2);

static hashtable_t g_loadedSymbolContexts;

void
SymbolInitialize(void)
{
    // Initialize the hashtable
    hashtable_construct(&g_loadedSymbolContexts,
                        HASHTABLE_MINIMUM_CAPACITY,
                        sizeof(struct symbol_context),
                        SymbolContextHash,
                        SymbolContextCmp);
}

oserr_t
SymbolsLoadContext(
        _In_  const char*             binaryName,
        _Out_ struct symbol_context** symbolContextOut)
{
    // replace extension with .map and see if it exists
    struct symbol_context symbolContext = { 0 };
    long                  fileSize;
    void*                 fileBuffer;
    oserr_t            status;

    TRACE("[SymbolsLoadContext] loading map file");
    status = SymbolLoadMapFile(binaryName, &fileBuffer, &fileSize);
    if (status != OS_EOK) {
        WARNING("[SymbolsLoadContext] failed to load map for %s", binaryName);
        return status;
    }

    TRACE("[SymbolsLoadContext] parsing map file, 0x%llx - %llu", fileBuffer, fileSize);
    status = SymbolParseMapFile(&symbolContext, fileBuffer, fileSize);
    free(fileBuffer);
    if (status != OS_EOK) {
        WARNING("[SymbolsLoadContext] failed to parse map for %s", binaryName);
        return status;
    }

    // ok create key now everything is done
    symbolContext.key = strdup(binaryName);

    TRACE("[SymbolsLoadContext] context loaded %s", symbolContext.key);
    hashtable_set(&g_loadedSymbolContexts, &symbolContext);
    *symbolContextOut = hashtable_get(&g_loadedSymbolContexts, &(struct symbol_context) { .key = binaryName });
    return OS_EOK;
}

oserr_t
SymbolLookup(
        _In_  const char*  binaryName,
        _In_  uintptr_t    binaryOffset,
        _Out_ const char** symbolName,
        _Out_ uintptr_t*   symbolOffset)
{
    struct symbol_context* symbolContext;
    struct map_symbol*     symbol = NULL;
    oserr_t             status;
    int                    i;

    if (!binaryName) {
        return OS_EINVALPARAMS;
    }

    // Check for loaded context
    symbolContext = (struct symbol_context*)hashtable_get(&g_loadedSymbolContexts, &(struct symbol_context) { .key = binaryName });
    if (!symbolContext) {
        status = SymbolsLoadContext(binaryName, &symbolContext);
        if (status != OS_EOK) {
            return status;
        }
    }

    // iterate symbols and find matching symbol, always selects last
    // symbol if the loop reaches end of list
    for (i = 0; i < symbolContext->symbol_count; i++) {
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
        _In_  const char* binaryName,
        _Out_ void**      fileBufferOut,
        _Out_ long*       fileSizeOut)
{
    char                   path[_MAXPATH];
    char                   tmp[_MAXPATH] = { 0 };
    const char*            lastDot = strrchr(binaryName, '.');
    FILE*                  file;
    long                   fileSize;
    void*                  fileBuffer;
    size_t                 bytesRead;

    if (lastDot) {
        size_t length = (size_t)((uintptr_t)lastDot - (uintptr_t)binaryName);
        strncpy(&tmp[0], binaryName, length);
    }
    else {
        strcpy(&tmp[0], binaryName);
    }

    sprintf(&path[0], "$bin/../maps/%s.map", &tmp[0]);

    TRACE("[SymbolsLoadContext] trying to load %s", &path[0]);
    file = fopen(&path[0], "r");
    if (!file) {
        // map did not exist
        ERROR("[SymbolsLoadContext] map file not found at %s", &path[0]);
        return OS_ENOENT;
    }

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

static uint64_t SymbolContextHash(const void* element)
{
    const struct symbol_context* context = element;
    uint8_t*                     pointer;
    uint64_t                     hash = 5381;
    int                          character;

    if (!context || !context->key) {
        return 0;
    }

    pointer = (uint8_t*)context->key;
    while ((character = tolower(*pointer)) != 0) {
        hash = ((hash << 5) + hash) + character; /* hash * 33 + c */
        pointer++;
    }

    return hash;
}

static int SymbolContextCmp(const void* element1, const void* element2)
{
    const struct symbol_context* lh = element1;
    const struct symbol_context* rh = element2;
    return strcasecmp(lh->key, rh->key);
}
