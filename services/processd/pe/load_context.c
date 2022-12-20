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
#include <ddk/memory.h>
#include <ddk/utils.h>
#include <ds/mstring.h>
#include <internal/_syscalls.h>
#include <module.h>
#include "private.h"
#include "pe.h"
#include <stdlib.h>
#include <string.h>

static uintptr_t g_systemBaseAddress = 0;

static uintptr_t
__GetLoadAddress(void)
{
    if (g_systemBaseAddress == 0) {
        Syscall_GetProcessBaseAddress(&g_systemBaseAddress);
    }
    return g_systemBaseAddress;
}

static uint64_t __module_hash(const void* element)
{
    const struct ModuleMapEntry* entry = element;
    return mstr_hash(entry->Name);
}

static int __module_cmp(const void* element1, const void* element2)
{
    _CRT_UNUSED(element1);
    _CRT_UNUSED(element2);
    // Make the assumption that the primary key (hashed name) is safe enough,
    // and that we do not need a secondary ID. So if this is called, the hashes
    // match, and thus we just assume OK.
    return 0;
}

struct PEImageLoadContext*
PEImageLoadContextNew(
        _In_ uuid_t      scope,
        _In_ const char* paths)
{
    struct PEImageLoadContext* loadContext;
    oserr_t                    oserr;

    loadContext = malloc(sizeof(struct PEImageLoadContext));
    if (loadContext == NULL) {
        return NULL;
    }
    memset(loadContext, 0, sizeof(struct PEImageLoadContext));

    oserr = CreateMemorySpace(0, &loadContext->MemorySpace);
    if (oserr != OS_EOK) {
        free(loadContext);
        return NULL;
    }

    hashtable_construct(
            &loadContext->ModuleMap, 0,
            sizeof(struct ModuleMapEntry),
            __module_hash, __module_cmp
    );

    loadContext->Scope = scope;
    loadContext->Paths = strdup(paths);
    loadContext->LoadAddress = __GetLoadAddress();
    return loadContext;
}

static void
__ClearImportListEntry(
        _In_ element_t* element,
        _In_ void*      context)
{
    _CRT_UNUSED(context);
    free(element);
}


static void
__module_cleanup_enum(
        _In_ int         index,
        _In_ const void* element,
        _In_ void*       userContext)
{
    const struct ModuleMapEntry* entry = element;
    _CRT_UNUSED(index);
    list_clear((list_t*)&entry->Imports, __ClearImportListEntry, userContext);
}

void
PEImageLoadContextDelete(
        _In_ struct PEImageLoadContext* loadContext)
{
    if (loadContext == NULL) {
        return;
    }

    PEImageUnload(loadContext, loadContext->RootModule, true);
    hashtable_enumerate(&loadContext->ModuleMap, __module_cleanup_enum, NULL);
    hashtable_destroy(&loadContext->ModuleMap);
    free(loadContext->Paths);
    free(loadContext);
}

int
PEImageLoadContextGetID(
        _In_ struct PEImageLoadContext* loadContext)
{
    if (loadContext == NULL) {
        return -1;
    }

    for (int i = 0; i < PROCESS_MAXMODULES; i++) {
        int     block  = i / sizeof(uint8_t);
        uint8_t offset = i % sizeof(uint8_t);
        if (!(loadContext->IDBitmap[block] & (1 << offset))) {
            loadContext->IDBitmap[block] |= (1 << offset);
            return i;
        }
    }
    return -1;
}

void
PEImageLoadContextPutID(
        _In_ struct PEImageLoadContext* loadContext,
        _In_ int                        id)
{
    int     block  = id / sizeof(uint8_t);
    uint8_t offset = id % sizeof(uint8_t);
    loadContext->IDBitmap[block] &= ~(1 << offset);
}

struct __ImageDetailsContext {
    bool      Resolved;
    uintptr_t MappedAddress;

    mstring_t* Name;
    uintptr_t  MappedBase;
};

static void
__module_imgdt_enum(
        _In_ int         index,
        _In_ const void* element,
        _In_ void*       userContext)
{
    const struct ModuleMapEntry*  entry = element;
    struct __ImageDetailsContext* context = userContext;
    uintptr_t                     mapStart, mapEnd;
    _CRT_UNUSED(index);

    if (context->Resolved) {
        return;
    }

    mapStart = entry->BaseMapping + entry->Module->CodeBaseRVA;
    mapEnd = entry->BaseMapping + entry->Module->CodeBaseRVA + entry->Module->CodeSize;
    if (context->MappedAddress >= mapStart && context->MappedAddress < mapEnd) {
        context->Name = entry->Name;
        context->MappedBase = entry->BaseMapping;
        context->Resolved = true;
    }
}

oserr_t
PEImageLoadContextImageDetailsByAddress(
        _In_  struct PEImageLoadContext* loadContext,
        _In_  uintptr_t                  mappedAddress,
        _Out_ uintptr_t*                 moduleBaseOut,
        _Out_ mstring_t**                moduleNameOut)
{
    struct __ImageDetailsContext context = {
            .Resolved = false,
            .MappedAddress = mappedAddress
    };
    hashtable_enumerate(&loadContext->ModuleMap, __module_imgdt_enum, &context);
    if (context.Resolved) {
        *moduleBaseOut = context.MappedBase;
        *moduleNameOut = context.Name;
        return OS_EOK;
    }
    return OS_ENOENT;
}

oserr_t
PEImageLoadContextModulePath(
        _In_  struct PEImageLoadContext* loadContext,
        _In_  mstring_t*                 moduleName,
        _Out_ mstring_t**                modulePathOut)
{
    struct ModuleMapEntry* entry;

    if (loadContext == NULL) {
        return OS_EINVALPARAMS;
    }

    entry = hashtable_get(
            &loadContext->ModuleMap,
            &(struct ModuleMapEntry) {
                .Name = moduleName
            }
    );
    if (entry == NULL) {
        return OS_ENOENT;
    }
    *modulePathOut = entry->Path;
    return OS_EOK;
}

oserr_t
PEImageLoadContextModuleEntryPoint(
        _In_  struct PEImageLoadContext* loadContext,
        _In_  mstring_t*                 moduleName,
        _Out_ uintptr_t*                 moduleEntryPointOut)
{
    struct ModuleMapEntry* entry;

    if (loadContext == NULL) {
        return OS_EINVALPARAMS;
    }

    entry = hashtable_get(
            &loadContext->ModuleMap,
            &(struct ModuleMapEntry) {
                    .Name = moduleName
            }
    );
    if (entry == NULL) {
        return OS_ENOENT;
    }
    *moduleEntryPointOut = entry->BaseMapping + entry->Module->EntryPointRVA;
    return OS_EOK;
}
