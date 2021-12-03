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

EFI_STATUS LibraryInitialize(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE* SystemTable)
{
    gImageHandle = ImageHandle;
    gSystemTable = SystemTable;
    gBootServices = gSystemTable->BootServices;
    
    return EFI_SUCCESS;
}

EFI_STATUS __GetMemoryInformation(
    OUT UINTN*  MemoryMapSize,
    OUT UINTN*  DescriptorSize)
{
    EFI_STATUS Status;
    UINTN      MapSize = 0;
    UINTN      MapKey;
    UINT32     DescriptorVersion;
    
    Status = gBootServices->GetMemoryMap(
        &MapSize, NULL, 
        &MapKey,
        DescriptorSize,
        &DescriptorVersion
    );
    if (Status != EFI_BUFFER_TOO_SMALL && EFI_ERROR(Status)) {
        ConsoleWrite(L"__GetMemoryInformation failed to get memory map %r\n", Status);
        return Status;
    }
    
    *MemoryMapSize = MapSize;
    return EFI_SUCCESS;
}

EFI_STATUS __GetMemoryMap(
    OUT VOID** MemoryMap,
    OUT UINTN* MemoryMapKey,
    OUT UINTN* DescriptorCount,
    OUT UINTN* DescriptorSize)
{
    EFI_STATUS             Status;
    UINTN                  MemoryMapSize;
    UINT32                 DescriptorVersion;
    UINTN                  DescriptorSizeLocal;
    EFI_MEMORY_DESCRIPTOR* MemoryMapLocal;
    ConsoleWrite(L"__GetMemoryMap()\n");

    // Get memory information
    Status = __GetMemoryInformation(&MemoryMapSize, &DescriptorSizeLocal);
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"__GetMemoryMap failed to get memory information\n");
        return Status;
    }

    // add additional entries for the next allocation, up to 2 additional entries
    // can be allocated
    MemoryMapSize += 2 * DescriptorSizeLocal;

    // Allocate memory for the memory map
    Status = LibraryAllocateMemory(MemoryMapSize, (VOID**)&MemoryMapLocal);
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"__GetMemoryMap failed to allocate memory\n");
        return Status;
    }

    Status = gBootServices->GetMemoryMap(&MemoryMapSize, 
        MemoryMapLocal, MemoryMapKey, &DescriptorSizeLocal, &DescriptorVersion
    );
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"__GetMemoryMap failed to get memory map %r\n", Status);
        return Status;
    }

    *MemoryMap       = MemoryMapLocal;
    *DescriptorCount = MemoryMapSize / DescriptorSizeLocal;
    *DescriptorSize  = DescriptorSizeLocal;
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
            return VBootMemoryType_Available;

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

EFI_STATUS __AllocateVBootMemory(
    IN struct VBoot* VBoot)
{
    EFI_STATUS Status;
    UINTN      MemoryMapSize;
    UINTN      DescriptorCount;
    UINTN      DescriptorSize;
    ConsoleWrite(L"__AllocateVBootMemory()\n");

    Status = __GetMemoryInformation(&MemoryMapSize, &DescriptorSize);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // add additional entries for the next allocation, up to 2 times 2 additional entries
    // can be allocated, to take into account the next allocations
    MemoryMapSize += 4 * DescriptorSize;

    // calculate the number of vboot entries we need, include one extra
    // as we need to take into account the allocation we are going to make
    DescriptorCount = (MemoryMapSize / DescriptorSize) + 1;
    Status = LibraryAllocateMemory(
        sizeof(struct VBootMemoryEntry) * DescriptorCount,
        (VOID**)&VBoot->Memory.Entries 
    );
    return Status;
}

EFI_STATUS __FillMemoryMap(
    IN  struct VBoot* VBoot,
    OUT VOID**        MemoryMap,
    OUT UINTN*        MemoryMapKey)
{
    EFI_STATUS             Status;
    VOID*                  MemoryMapLocal;
    UINTN                  DescriptorCount;
    UINTN                  DescriptorSize;
    UINTN                  i;
    ConsoleWrite(L"__FillMemoryMap()\n");

    // Now retrieve the final memory map
    Status = __GetMemoryMap(&MemoryMapLocal, MemoryMapKey, &DescriptorCount, &DescriptorSize);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    VBoot->Memory.NumberOfEntries = 0;
    ConsoleWrite(L"__FillMemoryMap Filling %lu entries\n", DescriptorCount);
    for (i = 0; i < DescriptorCount; i++) {
        EFI_MEMORY_DESCRIPTOR* MemoryDescriptor = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)MemoryMapLocal + (i * DescriptorSize));
        enum VBootMemoryType Type = __ConvertEfiType(MemoryDescriptor->Type);
        ConsoleWrite(L"ENTRY %lu: Type=%u, PhysicalBase=0x%lx, NumberOfPages=0x%lx, Attributes=0x%lx\n",
            i, MemoryDescriptor->Type, MemoryDescriptor->PhysicalStart, 
            MemoryDescriptor->NumberOfPages, MemoryDescriptor->Attribute);
        
        VBoot->Memory.Entries[VBoot->Memory.NumberOfEntries].Type         = Type;
        VBoot->Memory.Entries[VBoot->Memory.NumberOfEntries].PhysicalBase = MemoryDescriptor->PhysicalStart;
        VBoot->Memory.Entries[VBoot->Memory.NumberOfEntries].VirtualBase  = MemoryDescriptor->VirtualStart;
        VBoot->Memory.Entries[VBoot->Memory.NumberOfEntries].Length       = MemoryDescriptor->NumberOfPages * EFI_PAGE_SIZE;
        VBoot->Memory.Entries[VBoot->Memory.NumberOfEntries].Attributes   = MemoryDescriptor->Attribute;
        VBoot->Memory.NumberOfEntries++;
    }
    *MemoryMap = MemoryMapLocal;
    return EFI_SUCCESS;
}

EFI_STATUS LibraryCleanup(
    IN struct VBoot* VBoot)
{
    EFI_STATUS Status;

    Status = __AllocateVBootMemory(VBoot);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    Status = EFI_INVALID_PARAMETER;
    while (Status != EFI_SUCCESS) {
        VOID* MemoryMap;
        UINTN MemoryMapKey;

        Status = __FillMemoryMap(VBoot, &MemoryMap, &MemoryMapKey);
        if (EFI_ERROR(Status)) {
            ConsoleWrite(L"Failed to get memory map: %r\n", Status);
            return Status;
        }
        
        Status = gBootServices->ExitBootServices(gImageHandle, MemoryMapKey);
        if (EFI_ERROR(Status)) {
            ConsoleWrite(L"Failed to exit boot services: %r\n", Status);
            LibraryFreeMemory(MemoryMap);
        }
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
