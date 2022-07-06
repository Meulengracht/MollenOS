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
#endif

struct map_symbol {
    const char* section;
    const char* file;
    const char* name;
    uintptr_t   address;
    size_t      length;
};

struct symbol_context {
    const char*        key;
    char*              section_storage;
    char*              file_storage;
    char*              symbol_storage;
    struct map_symbol* symbols;
    int                symbol_count;
};

/**
 * SymbolInitialize
 */
void
SymbolInitialize(void);

/**
 * @brief Retrieves the symbol name given an offset into a binary file. Will try to locate any matching symbol file.
 *
 * @param binaryName   Name of the binary file, ex processmanager.dll
 * @param binaryOffset Address offset into the binary file
 * @param symbolName   A pointer where the resulting symbol name can be stored, does not need to be freed
 * @param symbolOffset A pointer to a uintptr_t where the offset into the symbol that was found
 * @return             Status of the lookup
 */
__EXTERN oscode_t
SymbolLookup(
        _In_  const char*  binaryName,
        _In_  uintptr_t    binaryOffset,
        _Out_ const char** symbolName,
        _Out_ uintptr_t*   symbolOffset);

/**
 * @brief Parses the provided map file data and stores all information into the provided symbol context
 *
 * @param symbolContext
 * @param fileBuffer
 * @param fileLength
 * @return
 */
__EXTERN oscode_t
SymbolParseMapFile(
        _In_ struct symbol_context* symbolContext,
        _In_ void*                  fileBuffer,
        _In_ size_t                 fileLength);

#endif //__PROCESSMANAGER_SYMBOLS_H__
