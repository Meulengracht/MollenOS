/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Process Service Definitions & Structures
 * - This header describes the base process-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <os/unwind.h>
#include <os/pe.h>

OsStatus_t
UnwindGetSection(
    _In_ void*            MemoryAddress,
    _In_ UnwindSection_t* Section)
{
    Handle_t ModuleList[PROCESS_MAXMODULES];

    // Get a list of loaded modules
    if (ProcessGetLibraryHandles(ModuleList) != OsSuccess) {
        return false;
    }

    // Foreach module, get section headers
    for (unsigned i = 0; i < PROCESS_MAXMODULES; i++) {
        MzHeader_t *DosHeader           = NULL;
        PeHeader_t *PeHeader            = NULL;
        PeOptionalHeader_t *OptHeader   = NULL;
        PeSectionHeader_t *Section      = NULL;
        bool found_obj                  = false;
        bool found_hdr                  = false;
        if (ModuleList[i] == NULL) {
            break;
        }

        // Initiate values
        Section->ModuleBase = (uintptr_t)ModuleList[i];
        DosHeader   = (MzHeader_t*)ModuleList[i];
        PeHeader    = (PeHeader_t*)(((uint8_t*)DosHeader) + DosHeader->PeHeaderAddress);
        OptHeader   = (PeOptionalHeader_t*)(((uint8_t*)DosHeader) + DosHeader->PeHeaderAddress + sizeof(PeHeader_t));
        if (OptHeader->Architecture == PE_ARCHITECTURE_32) {
            Section = (PeSectionHeader_t*)(((uint8_t*)DosHeader) + DosHeader->PeHeaderAddress + sizeof(PeHeader_t) + sizeof(PeOptionalHeader32_t));
        }
        else if (OptHeader->Architecture == PE_ARCHITECTURE_64) {
            Section = (PeSectionHeader_t*)(((uint8_t*)DosHeader) + DosHeader->PeHeaderAddress + sizeof(PeHeader_t) + sizeof(PeOptionalHeader64_t));
        }
        else {
            return false;
        }

        // Iterate sections and spot correct one
        for (unsigned j = 0; j < PeHeader->NumSections; j++, Section++) {
            uintptr_t begin = Section->VirtualAddress + (uintptr_t)ModuleList[i];
            uintptr_t end = begin + Section->VirtualSize;
            if (!strncmp((const char *)Section->Name, ".text", PE_SECTION_NAME_LENGTH)) {
                if (targetAddr >= begin && targetAddr < end)
                    found_obj = true;
            } else if (!strncmp((const char *)Section->Name, ".eh_frame", PE_SECTION_NAME_LENGTH)) {
                Section->UnwindSectionBase = begin;
                Section->UnwindSectionLength = Section->VirtualSize;
                found_hdr = true;
            }
            if (found_obj && found_hdr)
                return true;
        }
    }
}
