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
#include <depack.h>

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

    Status = gRoot->Open(gRoot, &File, FileName, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    Status = File->GetInfo(File, &gFileInfoGuid, &BufferSize, (VOID**)&Buffer[0]);
    if (EFI_ERROR(Status)) {
        goto cleanup;
    }

    FileInfo = (EFI_FILE_INFO*)&Buffer[0];
    ReadSize = FileInfo->FileSize;

    Status = LibraryAllocateMemory(ReadSize, (VOID**)&CompressedData);
    if (EFI_ERROR(Status)) {
        goto cleanup;
    }

    Status = File->Read(File, &ReadSize, CompressedData);
    if (EFI_ERROR(Status)) {
        goto cleanup;
    }

    // We got the data read, now decompress it
    if (IsCompressed) {
        UINT32 DecompressedSize = aP_get_orig_size(CompressedData);
        UINT32 DecompressStatus;
        VOID*  DecompressedData;

        if (!DecompressedSize) {
            LibraryFreeMemory(CompressedData);
            Status = EFI_COMPROMISED_DATA;
            goto cleanup;
        }

        Status = LibraryAllocateMemory(DecompressedSize, &DecompressedData);
        if (EFI_ERROR(Status)) {
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

EFI_STATUS LoadKernel(
    IN  struct VBoot*         VBoot,
    OUT EFI_PHYSICAL_ADDRESS* EntryPoint)
{
    EFI_STATUS Status;
    VOID*      Buffer;
    UINTN      BufferSize;

    PE_COFF_LOADER_IMAGE_CONTEXT ImageContext;

    Status = __LoadFile(L"\\EFI\\VALI\\syskrnl.mos", 1, &Buffer, &BufferSize);
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
    ImageContext.ImageAddress = 0x100000;
    Status = PeCoffLoaderLoadImage(&ImageContext);
    if (EFI_ERROR(Status)) {
        return Status;
    }
    
    // Relocate the image in our new buffer
    Status = PeCoffLoaderRelocateImage(&ImageContext);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // Flush not needed for all architectures. We could have a processor specific
    // function in this library that does the no-op if needed.
    InvalidateInstructionCacheRange((VOID *)(UINTN)ImageContext.ImageAddress, ImageContext.ImageSize);

    // Load and relocate the ramdisk
    Status = __LoadFile(L"\\EFI\\VALI\\initrd.mos", 1, &Buffer, &BufferSize);

    // Copy the ramdisk into memory at 0x2000000
    CopyMem((VOID*)0x2000000, Buffer, BufferSize);

    // Cleanup allocated buffer
    LibraryFreeMemory(Buffer);

    *EntryPoint = ImageContext.EntryPoint;
    return EFI_SUCCESS;
}
