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
 */

#include <library.h>
#include <console.h>

EFI_HANDLE         gImageHandle;
EFI_SYSTEM_TABLE*  gSystemTable;
EFI_BOOT_SERVICES* gBootServices;
UINTN              gMemoryMapKey;

EFI_STATUS LibraryInitialize(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE* SystemTable)
{
    gImageHandle = ImageHandle;
    gSystemTable = SystemTable;
    gBootServices = gSystemTable->BootServices;
    
    return EFI_SUCCESS;
}

EFI_STATUS LibraryCleanup(void)
{
    EFI_STATUS Status;

    // Disable the watchdog timer
    gBootServices->SetWatchdogTimer(0, 0, 0, NULL);
    Status = gBootServices->ExitBootServices(gImageHandle, gMemoryMapKey);
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"Failed to exit boot services: %r\n", Status);
    }
    return Status;
}

EFI_STATUS LibraryAllocateMemory(
    IN UINTN   Size,
    OUT VOID** Memory)
{
    return gBootServices->AllocatePool(EfiLoaderData, Size, Memory);
}

EFI_STATUS LibraryFreeMemory(
    IN VOID* Memory)
{
    return gBootServices->FreePool(Memory);
}

EFI_STATUS __GetMemoryInformation(
    OUT UINTN*  MemoryMapSize,
    OUT UINTN*  DescriptorSize)
{
    EFI_STATUS Status;
    UINTN      MapSize = 0;
    UINT32     DescriptorVersion;
    
    Status = gBootServices->GetMemoryMap(
        &MapSize, NULL, 
        &gMemoryMapKey,
        DescriptorSize,
        &DescriptorVersion
    );
    if (Status != EFI_BUFFER_TOO_SMALL && EFI_ERROR(Status)) {
        return Status;
    }
    
    *MemoryMapSize = MapSize;
    return EFI_SUCCESS;
}

EFI_STATUS __GetMemoryMap(
    OUT VOID**  MemoryMap,
    OUT UINTN*  DescriptorCount)
{
    EFI_STATUS             Status;
    UINTN                  MemoryMapSize;
    UINTN                  DescriptorSize;
    UINT32                 DescriptorVersion;
    UINTN                  MapKey;
    EFI_MEMORY_DESCRIPTOR* MemoryMapLocal;

    // Get memory information
    Status = __GetMemoryInformation(&MemoryMapSize, &DescriptorSize);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // Allocate memory for the memory map
    Status = LibraryAllocateMemory(MemoryMapSize, (VOID**)&MemoryMapLocal);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    Status = gBootServices->GetMemoryMap(&MemoryMapSize, 
        MemoryMapLocal, &MapKey, &DescriptorSize, &DescriptorVersion
    );
    if (EFI_ERROR(Status)) {
        return Status;
    }

    *MemoryMap       = MemoryMapLocal;
    *DescriptorCount = MemoryMapSize / DescriptorSize;
    return EFI_SUCCESS;
}

enum VBootMemoryType __ConvertEfiType(
    IN EFI_MEMORY_TYPE Type)
{
    switch (Type) {
        case EfiLoaderCode:
        case EfiLoaderData:
        case EfiBootServicesCode:
        case EfiBootServicesData:
        case EfiRuntimeServicesCode:
        case EfiRuntimeServicesData:
        case EfiMemoryMappedIO:
        case EfiPalCode:
            return VBootMemoryType_Firmware;

        case EfiACPIReclaimMemory:
            return VBootMemoryType_ACPI;
        case EfiACPIMemoryNVS:
            return VBootMemoryType_NVS;
        case EfiConventionalMemory:
        case EfiPersistentMemory:
            return VBootMemoryType_Available;
        default:
            return VBootMemoryType_Reserved;
    }
}

EFI_STATUS LibraryGetMemoryMap(
    IN struct VBoot* VBoot)
{
    EFI_STATUS             Status;
    VOID*                  MemoryMap;
    UINTN                  MemoryMapSize;
    UINTN                  DescriptorCount;
    UINTN                  DescriptorSize;
    UINTN                  i;
    ConsoleWrite(L"LibraryGetMemoryMap()\n");

    Status = __GetMemoryInformation(&MemoryMapSize, &DescriptorSize);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // calculate the number of vboot entries we need, include one extra
    // as we need to take into account the allocation we are going to make
    DescriptorCount = (MemoryMapSize / DescriptorSize) + 1;
    ConsoleWrite(L"LibraryGetMemoryMap allocation %lu vboot memory entries\n",
        DescriptorCount);
    Status = LibraryAllocateMemory(
        sizeof(struct VBootMemoryEntry) * DescriptorCount,
        (VOID**)&VBoot->Memory.Entries 
    );
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // Now retrieve the final memory map
    Status = __GetMemoryMap(&MemoryMap, &DescriptorCount);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    ConsoleWrite(L"LibraryGetMemoryMap Actual descriptor count=%lu\n", DescriptorCount);
    for (i = 0; i < DescriptorCount; i++) {
        EFI_MEMORY_DESCRIPTOR* MemoryDescriptor = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)MemoryMap + (i * DescriptorSize));
        enum VBootMemoryType Type = __ConvertEfiType(MemoryDescriptor->Type);
        ConsoleWrite(L"ENTRY %lu: Type=%u, PhysicalBase=0x%lx, NumberOfPages=0x%lx\n",
            i, MemoryDescriptor->Type,
            MemoryDescriptor->PhysicalStart, MemoryDescriptor->NumberOfPages);
        
        VBoot->Memory.Entries[VBoot->Memory.NumberOfEntries].Type         = Type;
        VBoot->Memory.Entries[VBoot->Memory.NumberOfEntries].PhysicalBase = MemoryDescriptor->PhysicalStart;
        VBoot->Memory.Entries[VBoot->Memory.NumberOfEntries].Length       = MemoryDescriptor->NumberOfPages * EFI_PAGE_SIZE;
        VBoot->Memory.Entries[VBoot->Memory.NumberOfEntries].Attributes   = MemoryDescriptor->Attribute;
        VBoot->Memory.NumberOfEntries++;
    }

    LibraryFreeMemory(MemoryMap);
    return EFI_SUCCESS;
}
