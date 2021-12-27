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

    // Add additional entries for the next allocation, up to 2 times 2 additional entries
    // can be allocated, to take into account the next allocations.
    // We also need to allocate up to 2 addition entries for inserting the kernel region
    // which may cause an entry before, and the actual entry for the kernel
    MemoryMapSize += 6 * DescriptorSize;

    // calculate the number of vboot entries we need, include one extra
    // as we need to take into account the allocation we are going to make
    DescriptorCount = (MemoryMapSize / DescriptorSize) + 1;
    Status = LibraryAllocateMemory(
        sizeof(struct VBootMemoryEntry) * DescriptorCount,
        (VOID**)&VBoot->Memory.Entries 
    );
    return Status;
}

static void __DumpMemoryMap(
    IN struct VBoot* VBoot)
{
    struct VBootMemoryEntry* Entries;
    UINTN                    i;

    ConsoleWrite(L"MemoryMap (NumberOfEntries=%d):\n", VBoot->Memory.NumberOfEntries);
    Entries = (struct VBootMemoryEntry*)VBoot->Memory.Entries;
    for (i = 0; i < VBoot->Memory.NumberOfEntries; i++) {
        ConsoleWrite(L"  %d: Type=%d, PhysicalBase=0x%lx, Length=0x%lx, Attributes=0x%lx\n", i, 
            Entries[i].Type, Entries[i].PhysicalBase, 
            Entries[i].Length, Entries[i].Attributes);
    }
}

EFI_STATUS __FillMemoryMap(
    IN  struct VBoot* VBoot,
    OUT VOID**        MemoryMap,
    OUT UINTN*        MemoryMapKey)
{
    struct VBootMemoryEntry* Entries;
    EFI_STATUS               Status;
    VOID*                    MemoryMapLocal;
    UINTN                    DescriptorCount;
    UINTN                    DescriptorSize;
    UINTN                    i;
    ConsoleWrite(L"__FillMemoryMap()\n");

    // Now retrieve the final memory map
    Status = __GetMemoryMap(&MemoryMapLocal, MemoryMapKey, &DescriptorCount, &DescriptorSize);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    ConsoleWrite(L"__FillMemoryMap Filling %lu entries\n", DescriptorCount);
    VBoot->Memory.NumberOfEntries = 0;
    Entries = (struct VBootMemoryEntry*)VBoot->Memory.Entries;
    for (i = 0; i < DescriptorCount; i++) {
        EFI_MEMORY_DESCRIPTOR* MemoryDescriptor = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)MemoryMapLocal + (i * DescriptorSize));
        enum VBootMemoryType Type = __ConvertEfiType(MemoryDescriptor->Type);
        
        Entries[VBoot->Memory.NumberOfEntries].Type         = Type;
        Entries[VBoot->Memory.NumberOfEntries].PhysicalBase = MemoryDescriptor->PhysicalStart;
        Entries[VBoot->Memory.NumberOfEntries].VirtualBase  = MemoryDescriptor->VirtualStart;
        Entries[VBoot->Memory.NumberOfEntries].Length       = MemoryDescriptor->NumberOfPages * EFI_PAGE_SIZE;
        Entries[VBoot->Memory.NumberOfEntries].Attributes   = MemoryDescriptor->Attribute;
        VBoot->Memory.NumberOfEntries++;
    }
    *MemoryMap = MemoryMapLocal;
    return EFI_SUCCESS;
}

static void __MemMoveReverse(
    IN const void* Source,
    IN void*       Destination,
    IN UINTN       Size)
{
    const UINT8* SourceReverse      = (const UINT8*)Source + Size - 1;
    UINT8*       DestinationReverse = (UINT8*)Destination + Size - 1;
    UINTN  i;
    for (i = 0; i < Size; i++) {
        *DestinationReverse-- = *SourceReverse--;
    }
}

static void __MemMove(
    IN const void* Source,
    IN void*       Destination,
    IN UINTN       Size)
{
    const UINT8* SourceReverse      = (const UINT8*)Source;
    UINT8*       DestinationReverse = (UINT8*)Destination;
    UINTN  i;
    for (i = 0; i < Size; i++) {
        *DestinationReverse++ = *SourceReverse++;
    }
}

static void __ConsolidateMemoryMap(
    IN struct VBoot* VBoot)
{
    struct VBootMemoryEntry* Entries;
    UINTN                    i;
    ConsoleWrite(L"__ConsolidateMemoryMap()\n");

    Entries = (struct VBootMemoryEntry*)VBoot->Memory.Entries;
    for (i = 1; i < VBoot->Memory.NumberOfEntries;) {
        if (Entries[i - 1].Type == Entries[i].Type &&
            Entries[i - 1].Attributes == Entries[i].Attributes &&
            Entries[i - 1].PhysicalBase + Entries[i - 1].Length == Entries[i].PhysicalBase) {
            
            // This block is a continuation of the previous block, consolidate them
            Entries[i - 1].Length += Entries[i].Length;

            // Move the entire memory map one entry back
            if (i < VBoot->Memory.NumberOfEntries - 1) {
                __MemMove(
                    &Entries[i + 1], &Entries[i], 
                    sizeof(struct VBootMemoryEntry) * (VBoot->Memory.NumberOfEntries - i - 1)
                );
            }
            VBoot->Memory.NumberOfEntries--;
        }
        else {
            // No match in blocks, move on
            i++;
        }
    }
    __DumpMemoryMap(VBoot);
}

static void __FixupMemoryMap(
    IN struct VBoot* VBoot)
{
    struct VBootMemoryEntry* Entries;
    EFI_PHYSICAL_ADDRESS     KernelAddress;
    UINTN                    i;
    ConsoleWrite(L"__FixupMemoryMap()\n");

    KernelAddress = (EFI_PHYSICAL_ADDRESS)VBoot->Kernel.Base;
    Entries = (struct VBootMemoryEntry*)VBoot->Memory.Entries;
    for (i = 0; i < VBoot->Memory.NumberOfEntries; i++) {
        struct VBootMemoryEntry* Entry = &Entries[i];

        if (Entry->PhysicalBase                <= KernelAddress && 
            Entry->PhysicalBase + Entry->Length > KernelAddress) {
            // We found the containing entry for the kernel, now we cut it up
            ConsoleWrite(L"__FixupMemoryMap Removing entry %lu\n", i);
            
            // We have two cases here, either we are at the start of the entry,
            // or we are in the middle of it. Should we move the entire memory map
            // 1 or 2 entries
            if (Entry->PhysicalBase == KernelAddress) {
                struct VBootMemoryEntry* KernelMapping   = &Entries[i];
                struct VBootMemoryEntry* OriginalMapping = &Entries[i + 1];
                // Start of entry, move the entire memory map one entry
                ConsoleWrite(L"__FixupMemoryMap Moving memory map one entry\n");
                __MemMoveReverse(
                    &Entries[i], &Entries[i + 1],
                    (VBoot->Memory.NumberOfEntries - i) * sizeof(struct VBootMemoryEntry));

                // Create the kernel mapping
                KernelMapping->Type         = VBootMemoryType_Reserved; // Eh firmware?
                KernelMapping->PhysicalBase = KernelAddress;
                KernelMapping->VirtualBase  = 0;
                KernelMapping->Length       = VBoot->Kernel.Length;
                KernelMapping->Attributes   = OriginalMapping->Attributes;

                // Adjust the original mapping
                OriginalMapping->PhysicalBase += VBoot->Kernel.Length;
                OriginalMapping->Length       -= VBoot->Kernel.Length;

                // Adjust the number of entries
                VBoot->Memory.NumberOfEntries++;
            } else {
                struct VBootMemoryEntry* FirstMapping    = &Entries[i];
                struct VBootMemoryEntry* KernelMapping   = &Entries[i + 1];
                struct VBootMemoryEntry* OriginalMapping = &Entries[i + 2];
                // Middle of entry, move the memory map two entries
                ConsoleWrite(L"__FixupMemoryMap Moving memory map two entry\n");
                __MemMoveReverse(
                    &Entries[i], &Entries[i + 2],
                    (VBoot->Memory.NumberOfEntries - i) * sizeof(struct VBootMemoryEntry));
                

                // Create the initial mapping that contains the new free space
                FirstMapping->Type         = OriginalMapping->Type;
                FirstMapping->PhysicalBase = OriginalMapping->PhysicalBase;
                FirstMapping->VirtualBase  = OriginalMapping->VirtualBase;
                FirstMapping->Length       = KernelAddress - OriginalMapping->PhysicalBase;
                FirstMapping->Attributes   = OriginalMapping->Attributes;

                // Create the kernel mapping
                KernelMapping->Type         = VBootMemoryType_Reserved; // Eh firmware?
                KernelMapping->PhysicalBase = KernelAddress;
                KernelMapping->VirtualBase  = 0;
                KernelMapping->Length       = VBoot->Kernel.Length;
                KernelMapping->Attributes   = OriginalMapping->Attributes;

                // Adjust the original mapping
                OriginalMapping->PhysicalBase = KernelAddress + VBoot->Kernel.Length;
                OriginalMapping->Length       -= VBoot->Kernel.Length + FirstMapping->Length;

                // Adjust the number of entries
                VBoot->Memory.NumberOfEntries += 2;
            }

            // We fixed the kernel mapping, we can stop!
            break;
        }
    }
    __DumpMemoryMap(VBoot);
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

        // Make sure console is disabled at this point
        ConsoleDisable();
    }

    // Do some post-processing of the memory map to simplify and prepare for
    // kernel
    __DumpMemoryMap(VBoot);
    __ConsolidateMemoryMap(VBoot);
    __FixupMemoryMap(VBoot);
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
