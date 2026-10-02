/**
 * Copyright 2021, Philip Meulengracht
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
 * ARM64 uses a conservative map: only EfiConventionalMemory is free.
 * This retains firmware table graphs, loader image, pools and all payloads.
 */

#include <platform/arm64.h>
#include <console.h>
#include <Library/BaseMemoryLib.h>

EFI_STATUS LibraryInitialize(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE* SystemTable)
{
    gImageHandle = ImageHandle;
    gST = SystemTable;
    gBS = gST->BootServices;
    
    return EFI_SUCCESS;
}

EFI_STATUS LibraryAllocateMemory(
    IN UINTN   Size,
    OUT VOID** Memory)
{
    return gBS->AllocatePool(EfiLoaderData, Size, Memory);
}

EFI_STATUS LibraryFreeMemory(
    IN VOID* Memory)
{
    return gBS->FreePool(Memory);
}

struct __MemoryMap {
    UINTN                    Size;
    UINTN                    Stride;
    UINTN                    Capacity;
    UINT32                   Version;
    UINTN                    Key;
    EFI_MEMORY_DESCRIPTOR*   Map;
    struct VBootMemoryEntry* Entries;
};

static EFI_STATUS __LoadMemoryMap(
    IN struct __MemoryMap* MemoryMap)
{
    EFI_STATUS status = gBS->GetMemoryMap(
        &MemoryMap->Size,
        NULL,
        &MemoryMap->Key,
        &MemoryMap->Stride,
        &MemoryMap->Version
    );
    if (status != EFI_BUFFER_TOO_SMALL || MemoryMap->Stride < sizeof(*MemoryMap->Map)) {
        return EFI_UNSUPPORTED;
    }

    /* No allocations after the first ExitBootServices attempt. */
    MemoryMap->Capacity = MemoryMap->Size + 64 * MemoryMap->Stride;
    status = LibraryAllocateMemory(
        MemoryMap->Capacity,
        (VOID**)&MemoryMap->Map
    );
    if (EFI_ERROR(status)) {
        return status;
    }
    
    status = LibraryAllocateMemory(
        (MemoryMap->Capacity / sizeof(*MemoryMap->Map)) * sizeof(*MemoryMap->Entries),
        (VOID**)&MemoryMap->Entries
    );
    if (EFI_ERROR(status)) {
        return status;
    }
    
    if (!Arm64Identity((UINTN)MemoryMap->Map, MemoryMap->Capacity)) {
        return EFI_UNSUPPORTED;
    }
    
    if (!Arm64Identity((UINTN)MemoryMap->Entries, (MemoryMap->Capacity / sizeof(*MemoryMap->Map)) * sizeof(*MemoryMap->Entries))) {
        return EFI_UNSUPPORTED;
    }
    return EFI_SUCCESS;
}

static enum VBootMemoryType __ToVBootMemoryType(
    IN UINT8 EfiType)
{
    switch (EfiType) {
        case EfiConventionalMemory:
            return VBootMemoryType_Available;
        case EfiACPIReclaimMemory:
            return VBootMemoryType_ACPI;
        case EfiACPIMemoryNVS:
            return VBootMemoryType_NVS;
        default:
            return VBootMemoryType_Reserved;
    }
}

static EFI_STATUS __FillMemoryMapEntries(
    IN struct __MemoryMap* MemoryMap)
{
    UINTN      size;
    EFI_STATUS status;

    size = MemoryMap->Capacity;
    
    status = gBS->GetMemoryMap(
        &size,
        MemoryMap->Map,
        &MemoryMap->Key,
        &MemoryMap->Stride,
        &MemoryMap->Version
    );
    if (EFI_ERROR(status)) {
        return status;
    }

    if (MemoryMap->Stride < sizeof(*MemoryMap->Map) || 
            size % MemoryMap->Stride || 
            MemoryMap->Version != 1) {
        return EFI_UNSUPPORTED;
    }

    Boot->Memory.NumberOfEntries = size / MemoryMap->Stride;
    for (UINTN i = 0; i < size / MemoryMap->Stride; ++i) {
        EFI_MEMORY_DESCRIPTOR* d = (VOID*)((UINT8*)MemoryMap->Map + i * MemoryMap->Stride);
        
        MemoryMap->Entries[i].Type = __ToVBootMemoryType(d->Type);
        MemoryMap->Entries[i].PhysicalBase = d->PhysicalStart;
        MemoryMap->Entries[i].VirtualBase = 0;
        MemoryMap->Entries[i].Length = d->NumberOfPages * EFI_PAGE_SIZE;
        MemoryMap->Entries[i].Attributes = d->Attribute;
    }
    return EFI_SUCCESS;
}

EFI_STATUS LibraryCleanup(struct VBoot* Boot)
{
    struct __MemoryMap memoryMap = { 0 };
    EFI_STATUS         status;
    
    status = __LoadMemoryMap(&memoryMap);
    if (EFI_ERROR(status)) {
        return status;
    }

    // Store the entries, as we exit in-loop later
    Boot->Memory.Entries = (UINTN)memoryMap.Entries;
    
    for (UINTN attempt = 0; attempt < 8; ++attempt) {
        status = __FillMemoryMapEntries(&memoryMap);
        if (EFI_ERROR(status)) {
            return status;
        }
        
        status = gBS->ExitBootServices(gImageHandle, memoryMap.Key);
        if (status == EFI_SUCCESS) {
            ConsoleDisable();
            gBS = NULL;
            return EFI_SUCCESS;
        }
        
        if (status != EFI_INVALID_PARAMETER) {
            ConsoleWrite(L"Failed to exit boot services: %r\n", status);
            break;
        }
    }
    
    // Firmware may be partially shut down: 
    // never return to it or use ConOut.
    ConsoleWrite(L"ARM64: final memory map/ExitBootServices failed: %r\n", status);
    return status;
}
