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
#include <loader.h>
#include <console.h>
#include <video.h>
#include <Guid/Acpi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>

static struct VBoot* gBootDescriptor = NULL;


BOOLEAN __CompareGUID(EFI_GUID lh, EFI_GUID rh)
{
    return (
        lh.Data1    == rh.Data1 &&
        lh.Data2    == rh.Data2 &&
        lh.Data3    == rh.Data3 &&
        lh.Data4[0] == rh.Data4[0] &&
        lh.Data4[1] == rh.Data4[1] &&
        lh.Data4[2] == rh.Data4[2] &&
        lh.Data4[3] == rh.Data4[3] &&
        lh.Data4[4] == rh.Data4[4] &&
        lh.Data4[5] == rh.Data4[5] &&
        lh.Data4[6] == rh.Data4[6] &&
        lh.Data4[7] == rh.Data4[7]);
}

static void __LocateRsdp(void)
{
    EFI_GUID Guid = EFI_ACPI_20_TABLE_GUID;
    UINTN    i;
    
    for (i = 0; i < gSystemTable->NumberOfTableEntries; i++){
        if (__CompareGUID(gSystemTable->ConfigurationTable[i].VendorGuid, Guid)) {
            CHAR8* TablePointer = (CHAR8*)gSystemTable->ConfigurationTable[i].VendorTable;
            if (TablePointer[0] == 'R' && TablePointer[1] == 'S' && TablePointer[2] == 'D' && TablePointer[3] == ' ' && 
                TablePointer[4] == 'P' && TablePointer[5] == 'T' && TablePointer[6] == 'R' && TablePointer[7] == ' ') {
                
            }
        }
    }
}

static EFI_STATUS __InitializeBootDescriptor(void)
{
    EFI_STATUS Status;

    Status = LibraryAllocateMemory(sizeof(struct VBoot), (VOID**)&gBootDescriptor);
    if (EFI_ERROR(Status)) {
        return Status;
    }
    SetMem(gBootDescriptor, sizeof(struct VBoot), 0);
    gBootDescriptor->Magic    = VBOOT_MAGIC;
    gBootDescriptor->Version  = VBOOT_VERSION;
    gBootDescriptor->Firmware = VBootFirmware_UEFI;
    gBootDescriptor->ConfigurationTable = gSystemTable->ConfigurationTable;
    gBootDescriptor->ConfigurationTableCount = gSystemTable->NumberOfTableEntries;
    __LocateRsdp();
    return Status;
}

static void __JumpToKernel(
    IN EFI_PHYSICAL_ADDRESS EntryPoint)
{
    BASE_LIBRARY_JUMP_BUFFER JumpBuffer = { 0 };
    SetJump(&JumpBuffer);
    JumpBuffer.Rip = EntryPoint;
    LongJump(&JumpBuffer, 1);
}

EFI_STATUS EFIAPI EfiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE* SystemTable)
{
    EFI_STATUS           Status;
    EFI_PHYSICAL_ADDRESS EntryPoint;

    // Initialize bootloader systems
    Status = LibraryInitialize(ImageHandle, SystemTable);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // Initialize console
    Status = ConsoleInitialize();
    if (EFI_ERROR(Status)) {
        return Status;
    }
    
    // Print header
    ConsoleWrite(L"Vali UEFI Loader\n");
    ConsoleWrite(L"- architecture amd64\n\n");

    // Initialize boot descriptor
    Status = __InitializeBootDescriptor();
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"Failed to allocate memory for boot descriptor\n");
        return Status;
    }

    // Load kernel and ramdisk resources
    Status = LoaderInitialize();
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"Failed to initialize loader\n");
        return Status;
    }

    Status = LoadKernel(gBootDescriptor, &EntryPoint);
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"Failed to load kernel or ramdisk\n");
        return Status;
    }

    // Initialize video output last as we lose console output
    Status = VideoInitialize(gBootDescriptor);
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"Failed to initialize video output\n");
        return Status;
    }

    // Last step is to get the memory map before moving to kernel
    Status = LibraryGetMemoryMap(gBootDescriptor);
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"Failed to get memory map\n");
        return Status;
    }

    // Cleanup bootloader systems
    LibraryCleanup();
    __JumpToKernel(EntryPoint);
    return EFI_SUCCESS;
}
