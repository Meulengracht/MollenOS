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
    gBootDescriptor->ConfigurationTableCount = gSystemTable->NumberOfTableEntries;
    gBootDescriptor->ConfigurationTable = (unsigned long long)gSystemTable->ConfigurationTable;
    return Status;
}

// Jump into kernel fun-land :-)
static void __JumpToKernel(
    IN EFI_PHYSICAL_ADDRESS EntryPoint,
    IN VOID*                KernelStack)
{
    BASE_LIBRARY_JUMP_BUFFER JumpBuffer;
    SetMem(&JumpBuffer, sizeof(BASE_LIBRARY_JUMP_BUFFER), 0);
    ConsoleWrite(L"__JumpToKernel(EntryPoint=0x%lx, KernelStack=0x%lx)\n",
        EntryPoint, KernelStack);

#if defined(__amd64__)
    JumpBuffer.Rbx = (UINTN)gBootDescriptor;
    JumpBuffer.Rip = EntryPoint;
    JumpBuffer.Rsp = (UINTN)KernelStack;
    JumpBuffer.Rbp = (UINTN)KernelStack;
#elif defined(__i386__)
    JumpBuffer.Ebx = (UINTN)gBootDescriptor;
    JumpBuffer.Eip = EntryPoint;
    JumpBuffer.Esp = (UINTN)KernelStack;
    JumpBuffer.Ebp = (UINTN)KernelStack;
#else
#error "Unsupported architecture"
#endif
    LongJump(&JumpBuffer, 1);
}

EFI_STATUS EFIAPI EfiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE* SystemTable)
{
    EFI_STATUS Status;
    VOID*      KernelStack;

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
    ConsoleWrite(L"- implementing vboot functionality\n\n");

    // Initialize boot descriptor
    Status = __InitializeBootDescriptor();
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"Failed to allocate memory for boot descriptor\n");
        return Status;
    }

    // Load kernel, ramdisk and bootstrapper resources
    Status = LoaderInitialize();
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"Failed to initialize loader\n");
        return Status;
    }

    Status = LoadResources(gBootDescriptor, &KernelStack);
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"Failed to load system resources\n");
        return Status;
    }

    // Initialize video output last as we lose console output
    Status = VideoInitialize(gBootDescriptor);
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"Failed to initialize video output\n");
        return Status;
    }

    // Last step is to get the memory map before moving to kernel
    Status = LibraryCleanup(gBootDescriptor);
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"Failed to get memory map\n");
        return Status;
    }

    __JumpToKernel(gBootDescriptor->Kernel.EntryPoint, KernelStack);
    return EFI_SUCCESS;
}
