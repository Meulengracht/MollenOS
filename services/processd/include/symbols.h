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

#ifndef __PROCESSMANAGER_SYMBOLS_H__
#define __PROCESSMANAGER_SYMBOLS_H__

#ifndef __TEST
#include <os/osdefs.h>
#include <ds/mstring.h>
#endif

struct map_symbol {
    const char* section;
    const char* file;
    const char* name;
    uintptr_t   address;
    size_t      length;
};

struct MapContext {
    char*              SectionStorage;
    char*              FileStorage;
    char*              SymbolStorage;
    struct map_symbol* Symbols;
    int                SymbolCount;
};

struct symbol_context {
    mstring_t*         Key;
    struct MapContext  MapContext;
};

/**
 * SymbolInitialize
 */
void
SymbolInitialize(void);

/**
 * @brief Retrieves the symbol name given an offset into a binary file. Will try to locate any matching symbol file.
 *
 * @param moduleName   Name of the module file, ex processmanager.dll
 * @param binaryOffset Address offset into the binary file
 * @param symbolName   A pointer where the resulting symbol name can be stored, does not need to be freed
 * @param symbolOffset A pointer to a uintptr_t where the offset into the symbol that was found
 * @return             Status of the lookup
 */
__EXTERN oserr_t
SymbolLookup(
        _In_  mstring_t*   moduleName,
        _In_  uintptr_t    binaryOffset,
        _Out_ const char** symbolName,
        _Out_ uintptr_t*   symbolOffset);

/**
 * @brief Parses the provided map file data and stores all information into the provided symbol context
 *
 * @param mapContext
 * @param fileBuffer
 * @param fileLength
 * @return
 */
__EXTERN oserr_t
SymbolParseMapFile(
        _In_ struct MapContext* mapContext,
        _In_ void*              fileBuffer,
        _In_ size_t             fileLength);

/**
 * @brief Frees any resources allocated in regards to the map context.
 * @param mapContext The map context filled by SymbolParseMapFile
 */
__EXTERN void
MapContextDelete(
        _In_ struct MapContext* mapContext);

#endif //__PROCESSMANAGER_SYMBOLS_H__
