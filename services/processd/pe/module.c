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

#include <module.h>
#include "private.h"
#include <stdlib.h>
#include <string.h>

// Reuse the ordinal_cmp for name as well, the hashtable only calls cmp
// once hashes match, and if hashes match on function names, we just do
// an ordinal compare.
static uint64_t __expfn_ordinal_hash(const void* element);
static int      __expfn_ordinal_cmp(const void* element1, const void* element2);
static uint64_t __expfn_name_hash(const void* element);

struct Module*
ModuleNew(
        _In_ void*  moduleBuffer,
        _In_ size_t bufferSize)
{
    struct Module* module;
    int            status;

    module = malloc(sizeof(struct Module));
    if (module == NULL) {
        return NULL;
    }
    memset(module, 0, sizeof(struct Module));

    module->ImageBuffer = moduleBuffer;
    module->ImageBufferSize = bufferSize;
    usched_mtx_init(&module->Mutex, USCHED_MUTEX_PLAIN);
    status = hashtable_construct(
            &module->ExportedOrdinals, 0, sizeof(struct ExportedFunction),
            __expfn_ordinal_hash, __expfn_ordinal_cmp);
    if (status) {
        free(module);
        return NULL;
    }
    status = hashtable_construct(
            &module->ExportedNames, 0, sizeof(struct ExportedFunction),
            __expfn_name_hash, __expfn_ordinal_cmp);
    if (status) {
        hashtable_destroy(&module->ExportedOrdinals);
        free(module);
        return NULL;
    }
    return module;
}

void
ModuleDelete(
        _In_ struct Module* module)
{
    if (module == NULL) {
        return;
    }

    // No further cleanup is needed for exported functions, as no allocations
    // are made for the structs themselves.
    hashtable_destroy(&module->ExportedOrdinals);
    hashtable_destroy(&module->ExportedNames);
    free(module->Sections);
    free(module->ImageBuffer);
    free(module);
}

uint32_t
ModuleArchitecture(
        _In_ struct Module* module)
{
    return module->Architecture;
}

PeDataDirectory_t*
ModuleDataDirectories(
        _In_ struct Module* module)
{
    return &module->DataDirectories[0];
}

static uint64_t __expfn_ordinal_hash(const void* element)
{
    const struct ExportedFunction* function = element;
    return (uint64_t)function->Ordinal;
}

static int __expfn_ordinal_cmp(const void* element1, const void* element2)
{
    const struct ExportedFunction* function1 = element1;
    const struct ExportedFunction* function2 = element2;
    return function1->Ordinal == function2->Ordinal ? 0 : -1;
}

uint32_t __hash(const char* string)
{
    uint32_t hash = 5381;
    size_t   i    = 0;
    if (string == NULL) {
        return 0;
    }
    while (string[i]) {
        hash = ((hash << 5) + hash) + string[i]; /* hash * 33 + c */
    }
    return hash;
}

static uint64_t __expfn_name_hash(const void* element)
{
    const struct ExportedFunction* function = element;
    return __hash(function->Name);
}
