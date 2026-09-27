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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <ddk/service.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/types/process.h>
#include <os/unwind.h>
#include <os/pe.h>
#include <string.h>

#include <sys_process_service_client.h>

static void
ProcessGetLibraryHandles(
    _In_  Handle_t* ModuleList,
    _Out_ int*      ModuleCountOut)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    uint32_t                 moduleCount = PROCESS_MAXMODULES;

    // Initialize it to 0
    *ModuleCountOut = 0;
    
    sys_process_get_modules(GetGrachtClient(), &msg.base, __crt_process_id());
    gracht_client_await(GetGrachtClient(), &msg.base, 0);
    (void)sys_process_get_modules_result(
        GetGrachtClient(), &msg.base,
        (uintptr_t*)ModuleList,
        &moduleCount,
        ModuleCountOut
    );
}

oserr_t
UnwindGetSection(
    _In_ void*            MemoryAddress,
    _In_ UnwindSection_t* Section)
{
    Handle_t ModuleList[PROCESS_MAXMODULES] = { 0 };
    int      ModuleCount;
    
    if (__crt_is_phoenix()) {
        return OS_ENOENT;
    }
    
    ProcessGetLibraryHandles(ModuleList, &ModuleCount);

    // Foreach module, get section headers
    for (unsigned i = 0; i < ModuleCount; i++) {
        MzHeader_t*         dosHeader;
        PeHeader_t*         peHeader;
        PeOptionalHeader_t* optHeader;
        PeSectionHeader_t*  peSection;
        int                 foundObj = 0;
        int                 foundHdr = 0;
        
        if (ModuleList[i] == NULL) {
            break;
        }

        Section->ModuleBase = ModuleList[i];
        
        dosHeader   = (MzHeader_t*)ModuleList[i];
        peHeader    = (PeHeader_t*)(((uint8_t*)dosHeader) + dosHeader->PeHeaderAddress);
        optHeader   = (PeOptionalHeader_t*)(((uint8_t*)dosHeader) + dosHeader->PeHeaderAddress + sizeof(PeHeader_t));
        if (optHeader->Architecture == PE_ARCHITECTURE_32) {
            peSection = (PeSectionHeader_t*)(((uint8_t*)dosHeader) + dosHeader->PeHeaderAddress + sizeof(PeHeader_t) + sizeof(PeOptionalHeader32_t));
        } else if (optHeader->Architecture == PE_ARCHITECTURE_64) {
            peSection = (PeSectionHeader_t*)(((uint8_t*)dosHeader) + dosHeader->PeHeaderAddress + sizeof(PeHeader_t) + sizeof(PeOptionalHeader64_t));
        } else {
            continue;
        }

        // ARM64 uses the PE exception directory, which may be empty for an
        // image containing only leaf functions. Return the containing module
        // even then: libunwind must distinguish a known leaf from an unknown PC.
        if (peHeader->Machine == PE_MACHINE_ARM64 &&
            optHeader->Architecture == PE_ARCHITECTURE_64) {
            PeOptionalHeader64_t* header64 = (PeOptionalHeader64_t*)optHeader;
            uintptr_t offset = (uintptr_t)MemoryAddress - (uintptr_t)ModuleList[i];
            PeDataDirectory_t directory = { 0, 0 };

            if ((uintptr_t)MemoryAddress < (uintptr_t)ModuleList[i] ||
                offset >= header64->SizeOfImage) {
                continue;
            }
            if (header64->NumDataDirectories > PE_SECTION_EXCEPTION) {
                directory = header64->Directories[PE_SECTION_EXCEPTION];
            }
            if ((directory.AddressRVA & 3) || (directory.Size & 7) ||
                directory.AddressRVA > header64->SizeOfImage ||
                directory.Size > header64->SizeOfImage - directory.AddressRVA) {
                return OS_ENOENT;
            }
            Section->UnwindSectionBase = directory.Size
                ? (uint8_t*)ModuleList[i] + directory.AddressRVA : NULL;
            Section->UnwindSectionLength = directory.Size;
            return OS_EOK;
        }

        // Iterate sections and spot correct one
        for (unsigned j = 0; j < peHeader->NumSections; j++, peSection++) {
            uintptr_t begin = peSection->VirtualAddress + (uintptr_t)ModuleList[i];
            uintptr_t end = begin + peSection->VirtualSize;
            if (!strcmp((const char *)peSection->Name, ".text")) {
                if ((uintptr_t)MemoryAddress >= begin && (uintptr_t)MemoryAddress < end)
                    foundObj = 1;
            } else if (!strncmp((const char *)peSection->Name, ".eh_frame", PE_SECTION_NAME_LENGTH)) {
                Section->UnwindSectionBase = (void*)begin;
                Section->UnwindSectionLength = peSection->VirtualSize;
                foundHdr = 1;
            }
            
            if (foundObj && foundHdr)
                return OS_EOK;
        }
    }
    return OS_ENOENT;
}
