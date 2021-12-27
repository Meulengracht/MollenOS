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

#include <console.h>
#include <depack.h>
#include <library.h>
#include <loader.h>

#include <Guid/FileInfo.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/PeCoffLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>

static EFI_GUID gFileInfoGuid            = EFI_FILE_INFO_ID;
static EFI_GUID gFileSystemProtocolGuid  = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID gLoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;

static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* gFileSystem = NULL;
static EFI_FILE_PROTOCOL*               gRoot       = NULL;

EFI_STATUS LoaderInitialize(void)
{
    EFI_STATUS        Status;
    EFI_LOADED_IMAGE* LoadedImage;

    Status = gBootServices->HandleProtocol(
        gImageHandle, 
        &gLoadedImageProtocolGuid, 
        (VOID**)&LoadedImage
    );
    if (EFI_ERROR(Status)) {
        return Status;
    }

    Status = gBootServices->HandleProtocol(
        LoadedImage->DeviceHandle,
        &gFileSystemProtocolGuid, 
        (VOID**)&gFileSystem
    );
    if (EFI_ERROR(Status)) {
        return Status;
    }

    Status = gFileSystem->OpenVolume(gFileSystem, &gRoot);
    if (EFI_ERROR(Status)) {
        return Status;
    }
    return EFI_SUCCESS;
}

EFI_STATUS __LoadFile(
    IN  CHAR16* FileName, 
    IN  BOOLEAN IsCompressed,
    OUT VOID**  BufferOut, 
    OUT UINTN*  BufferSizeOut)
{
    EFI_STATUS         Status;
    EFI_FILE_PROTOCOL* File = NULL;
    EFI_FILE_INFO*     FileInfo;
    void*              Buffer[256];
    UINTN              BufferSize = sizeof(Buffer);
    UINTN              ReadSize;
    UINT8*             CompressedData;
    ConsoleWrite(L"__LoadFile(FileName=%s, IsCompressed=%d)\n",
        FileName, IsCompressed);

    Status = gRoot->Open(gRoot, &File, FileName, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"__LoadFile Failed to open file: %s\n", FileName);
        return Status;
    }

    Status = File->GetInfo(File, &gFileInfoGuid, &BufferSize, (VOID**)&Buffer[0]);
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"__LoadFile Failed to get file info: %s\n", FileName);
        goto cleanup;
    }

    FileInfo = (EFI_FILE_INFO*)&Buffer[0];
    ReadSize = FileInfo->FileSize;

    Status = LibraryAllocateMemory(ReadSize, (VOID**)&CompressedData);
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"__LoadFile Failed to allocate memory: %s\n", FileName);
        goto cleanup;
    }

    Status = File->Read(File, &ReadSize, CompressedData);
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"__LoadFile Failed to read file: %s\n", FileName);
        goto cleanup;
    }

    // We got the data read, now decompress it
    if (IsCompressed) {
        UINT32 DecompressedSize = aP_get_orig_size(CompressedData);
        UINT32 DecompressStatus;
        VOID*  DecompressedData;

        if (!DecompressedSize) {
            ConsoleWrite(L"__LoadFile Compression header was incorrect: %s\n", FileName);
            ConsoleWrite(L"__LoadFile 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n",
                ((UINT32*)CompressedData)[0], ((UINT32*)CompressedData)[1], 
                ((UINT32*)CompressedData)[2], ((UINT32*)CompressedData)[3],
                ((UINT32*)CompressedData)[4]);
            LibraryFreeMemory(CompressedData);
            Status = EFI_COMPROMISED_DATA;
            goto cleanup;
        }

        Status = LibraryAllocateMemory(DecompressedSize, &DecompressedData);
        if (EFI_ERROR(Status)) {
            ConsoleWrite(L"__LoadFile Failed to allocate memory for decompression: %s\n", FileName);
            LibraryFreeMemory(CompressedData);
            goto cleanup;
        }

        DecompressStatus = aP_depack_safe(
            CompressedData,
            ReadSize,
            DecompressedData,
            DecompressedSize
        );
        LibraryFreeMemory(CompressedData);
        
        if (DecompressStatus == APLIB_ERROR) {
            ConsoleWrite(L"__LoadFile Failed to decompress file: %s\n", FileName);
            LibraryFreeMemory(DecompressedData);
            Status = EFI_COMPROMISED_DATA;
            goto cleanup;
        }

        *BufferOut     = DecompressedData;
        *BufferSizeOut = DecompressedSize;
    }
    else {
        *BufferOut = CompressedData;
        *BufferSizeOut = ReadSize;
    }

cleanup:
    if (File) {
        File->Close(File);
    }
    return Status;
}

EFI_STATUS __AllocateKernelStack(
    IN  struct VBoot* VBoot,
    IN  UINTN         Size,
    OUT VOID**        Stack)
{
    EFI_PHYSICAL_ADDRESS StackBase;
    EFI_STATUS           Status = gBootServices->AllocatePages(
        AllocateAnyPages,
        EfiRuntimeServicesData,
        EFI_SIZE_TO_PAGES(Size),
        &StackBase
    );
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"__AllocateKernelStack Failed to allocate stack\n");
        return Status;
    }

    // Update the VBoot structure
    VBoot->Stack.Base   = (unsigned long long)StackBase;
    VBoot->Stack.Length = Size;

    *Stack = (VOID*)(StackBase + Size - sizeof(VOID*));
    return EFI_SUCCESS;
}

EFI_STATUS __AllocateRamdiskSpace(
    IN  UINTN  Size,
    OUT VOID** Memory)
{
    return gBootServices->AllocatePages(
        AllocateAnyPages,
        EfiRuntimeServicesData,
        EFI_SIZE_TO_PAGES(Size),
        (EFI_PHYSICAL_ADDRESS*)Memory
    );
}

EFI_STATUS
__LoadKernel(
    IN  struct VBoot* VBoot)
{
    EFI_STATUS Status;
    VOID*      Buffer;
    UINTN      BufferSize;
    ConsoleWrite(L"__LoadKernel()\n");

    PE_COFF_LOADER_IMAGE_CONTEXT ImageContext;

    Status = __LoadFile(L"\\EFI\\VALI\\kernel.mos", 1, &Buffer, &BufferSize);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // Initialize the image context
    SetMem(&ImageContext, sizeof(ImageContext), 0);

    ImageContext.Handle = Buffer;
    ImageContext.ImageRead = PeCoffLoaderImageReadFromMemory;

    // Get information about the pe image
    Status = PeCoffLoaderGetImageInfo(&ImageContext);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // We would like to request the kernel loaded at 1mb mark
    Status = PeCoffLoaderLoadImage(&ImageContext);
    if (EFI_ERROR(Status)) {
        return Status;
    }
    
    // Relocate the image in our new buffer
    Status = PeCoffLoaderRelocateImage(&ImageContext);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // Update the VBoot structure
    VBoot->Kernel.Base       = (unsigned long long)ImageContext.ImageAddress;
    VBoot->Kernel.EntryPoint = (unsigned long long)ImageContext.EntryPoint;
    VBoot->Kernel.Length     = ImageContext.ImageSize;

    // Flush not needed for all architectures. We could have a processor specific
    // function in this library that does the no-op if needed.
    InvalidateInstructionCacheRange(
        (VOID *)(UINTN)ImageContext.ImageAddress, 
        ImageContext.ImageSize
    );
    ConsoleWrite(L"__LoadKernel loaded at 0x%x, Size=0x%x\n", 
        ImageContext.ImageAddress, ImageContext.ImageSize);
    return EFI_SUCCESS;
}

EFI_STATUS
__LoadRamdisk(
    IN  struct VBoot* VBoot)
{
    EFI_STATUS Status;
    VOID*      Buffer;
    UINTN      BufferSize;
    ConsoleWrite(L"__LoadRamdisk()\n");

    // Load and relocate the ramdisk
    Status = __LoadFile(L"\\EFI\\VALI\\initrd.mos", 1, &Buffer, &BufferSize);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    VBoot->Ramdisk.Length = (UINT32)BufferSize;
    Status = __AllocateRamdiskSpace(BufferSize, (VOID**)&VBoot->Ramdisk.Data);
    if (EFI_ERROR(Status)) {
        return Status;
    }
    
    ConsoleWrite(L"LoadKernel Loaded ramdisk at 0x%x, Size=0x%x\n", 
        VBoot->Ramdisk.Data, BufferSize);

    CopyMem((VOID*)VBoot->Ramdisk.Data, Buffer, BufferSize);
    LibraryFreeMemory(Buffer);
    return EFI_SUCCESS;
}

EFI_STATUS
__LoadPhoenix(
    IN  struct VBoot* VBoot)
{
    EFI_STATUS Status;
    VOID*      Buffer;
    UINTN      BufferSize;
    ConsoleWrite(L"__LoadPhoenix()\n");

    PE_COFF_LOADER_IMAGE_CONTEXT ImageContext;

    Status = __LoadFile(L"\\EFI\\VALI\\phoenix.mos", 0, &Buffer, &BufferSize);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // Initialize the image context
    SetMem(&ImageContext, sizeof(ImageContext), 0);

    ImageContext.Handle = Buffer;
    ImageContext.ImageRead = PeCoffLoaderImageReadFromMemory;

    // Get information about the pe image
    Status = PeCoffLoaderGetImageInfo(&ImageContext);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // We would like to allocate some new memory for the image
    // to be relocated into, and we do not perform any relocation
    // to this new address as we are still loading the image at the
    // preffered base address
    Status = LibraryAllocateMemory(
        ImageContext.ImageSize,
        (VOID**)&ImageContext.ImageAddress
    );

    Status = PeCoffLoaderLoadImage(&ImageContext);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // Free the filebuffer, we don't need that anymore
    LibraryFreeMemory(Buffer);

    // Update the VBoot structure
    VBoot->Phoenix.Base       = (unsigned long long)ImageContext.ImageAddress;
    VBoot->Phoenix.EntryPoint = (unsigned long long)ImageContext.EntryPoint;
    VBoot->Phoenix.Length     = ImageContext.ImageSize;

    // Flush not needed for all architectures. We could have a processor specific
    // function in this library that does the no-op if needed.
    InvalidateInstructionCacheRange(
        (VOID *)(UINTN)ImageContext.ImageAddress, 
        ImageContext.ImageSize
    );
    ConsoleWrite(L"__LoadPhoenix loaded at 0x%x, Size=0x%x\n", 
        ImageContext.ImageAddress, ImageContext.ImageSize);

    return EFI_SUCCESS;
}

EFI_STATUS LoadResources(
    IN  struct VBoot* VBoot,
    OUT VOID**        KernelStack)
{
    EFI_STATUS Status;
    ConsoleWrite(L"LoadResources()\n");

    Status = __LoadKernel(VBoot);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    Status = __LoadRamdisk(VBoot);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    Status = __LoadPhoenix(VBoot);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // Allocate a stack for the kernel
    Status = __AllocateKernelStack(VBoot, LOADER_KERNEL_STACK_SIZE, KernelStack);
    if (EFI_ERROR(Status)) {
        return Status;
    }
    return EFI_SUCCESS;
}
