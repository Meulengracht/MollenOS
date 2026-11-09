/**
 * Copyright, Philip Meulengracht
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

#include <platform/arm64.h>
#include <platform/arm64-image.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PeCoffLib.h>
#include <Library/CacheMaintenanceLib.h>

struct __ImageFile {
    const UINT8* Bytes;
    UINTN        Size;
};

static RETURN_STATUS EFIAPI
__ReadImage(
    IN     VOID*  Handle,
    IN     UINTN  Offset,
    IN OUT UINTN* Size,
    OUT    VOID*  Buffer)
{
    const struct __ImageFile* file = Handle;

    // Reject reads outside the decompressed PE file.
    if (Offset > file->Size || *Size > file->Size - Offset) {
        *Size = 0;
        return RETURN_LOAD_ERROR;
    }

    CopyMem(Buffer, file->Bytes + Offset, *Size);
    return RETURN_SUCCESS;
}

static BOOLEAN
__Contains(
    IN UINT64 Base,
    IN UINT64 Length,
    IN UINT64 Offset,
    IN UINT64 Size)
{
    return Offset >= Base && Offset - Base <= Length && Size <= Length - (Offset - Base);
}

static BOOLEAN
__InSection(
    IN EFI_IMAGE_SECTION_HEADER* Sections,
    IN UINTN                     Count,
    IN UINT32                    Rva,
    IN UINTN                     Length,
    IN BOOLEAN                   Executable)
{
    for (UINTN i = 0; i < Count; i++) {
        UINT32 size = Sections[i].Misc.VirtualSize;

        if (Executable && !(Sections[i].Characteristics & EFI_IMAGE_SCN_MEM_EXECUTE)) {
            continue;
        }

        if (Executable) {
            // The entry must point to file-backed code, not zero-filled storage.
            size = MIN(size, Sections[i].SizeOfRawData);
        }

        if (__Contains(Sections[i].VirtualAddress, size, Rva, Length)) {
            return TRUE;
        }
    }
    return FALSE;
}

static EFI_STATUS
__ValidateFileSections(
    IN const struct __ImageFile*           File,
    IN const PE_COFF_LOADER_IMAGE_CONTEXT* Context)
{
    EFI_IMAGE_NT_HEADERS64 header;
    RETURN_STATUS          status;
    UINTN                  sectionOffset;
    UINTN                  imageLength;

    // Validate destination ranges too. EDK2 copies raw data even when VirtualSize is zero.
    imageLength = sizeof(EFI_IMAGE_NT_HEADERS64);
    
    status = __ReadImage((VOID*)File, Context->PeCoffHeaderOffset, &imageLength, &header);
    if (EFI_ERROR(status)) {
        return status;
    }

    if (header.OptionalHeader.Magic != EFI_IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        header.FileHeader.SizeOfOptionalHeader < sizeof(header.OptionalHeader)) {
        return EFI_UNSUPPORTED;
    }

    sectionOffset = Context->PeCoffHeaderOffset 
        + sizeof(UINT32) 
        + sizeof(EFI_IMAGE_FILE_HEADER) 
        + header.FileHeader.SizeOfOptionalHeader;
    if (Context->SizeOfHeaders > File->Size || Context->SizeOfHeaders > Context->ImageSize ||
        !__Contains(0, Context->SizeOfHeaders, sectionOffset,
            header.FileHeader.NumberOfSections * sizeof(EFI_IMAGE_SECTION_HEADER))) {
        return EFI_LOAD_ERROR;
    }

    for (UINTN i = 0; i < header.FileHeader.NumberOfSections; i++) {
        EFI_IMAGE_SECTION_HEADER section;
        UINT64                   length;

        CopyMem(
            &section,
            File->Bytes + sectionOffset + (i * sizeof(EFI_IMAGE_SECTION_HEADER)),
            sizeof(EFI_IMAGE_SECTION_HEADER)
        );

        length = MAX(section.Misc.VirtualSize, section.SizeOfRawData);
        
        if (section.VirtualAddress < Context->SizeOfHeaders ||
            !__Contains(0, Context->ImageSize, section.VirtualAddress, length)) {
            return EFI_LOAD_ERROR;
        }

        if (section.VirtualAddress % Context->SectionAlignment ||
            !__Contains(0, File->Size, section.PointerToRawData, section.SizeOfRawData)) {
            return EFI_LOAD_ERROR;
        }

        for (UINTN j = 0; j < i; j++) {
            EFI_IMAGE_SECTION_HEADER previous;
            UINT64                   previousLength;

            CopyMem(
                &previous,
                File->Bytes + sectionOffset + (j * sizeof(EFI_IMAGE_SECTION_HEADER)),
                sizeof(EFI_IMAGE_SECTION_HEADER)
            );

            previousLength = MAX(previous.Misc.VirtualSize, previous.SizeOfRawData);
            if (section.VirtualAddress < (UINT64)previous.VirtualAddress + previousLength &&
                previous.VirtualAddress < (UINT64)section.VirtualAddress + length) {
                return EFI_LOAD_ERROR;
            }
        }
    }
    return EFI_SUCCESS;
}

static EFI_STATUS
__ValidateLoadedImage(
    IN PE_COFF_LOADER_IMAGE_CONTEXT* Context,
    OUT BOOLEAN*                     CanMove)
{
    EFI_IMAGE_NT_HEADERS64*   header;
    EFI_IMAGE_SECTION_HEADER* sections;
    EFI_IMAGE_DATA_DIRECTORY  directory;
    UINT8*                    image = (VOID*)(UINTN)Context->ImageAddress;
    UINTN                     sectionOffset;
    UINTN                     offset;

    *CanMove = FALSE;
    
    // Validate the loaded headers before reading section and relocation metadata.
    if (!__Contains(0, Context->ImageSize, Context->PeCoffHeaderOffset, sizeof(EFI_IMAGE_NT_HEADERS64))) {
        return EFI_LOAD_ERROR;
    }

    header = (VOID*)(image + Context->PeCoffHeaderOffset);
    if (header->OptionalHeader.Magic != EFI_IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        header->FileHeader.SizeOfOptionalHeader < sizeof(header->OptionalHeader)) {
        return EFI_UNSUPPORTED;
    }

    sectionOffset = Context->PeCoffHeaderOffset 
        + sizeof(UINT32) 
        + sizeof(EFI_IMAGE_FILE_HEADER) 
        + header->FileHeader.SizeOfOptionalHeader;
    if (!__Contains(0, Context->SizeOfHeaders, sectionOffset,
            header->FileHeader.NumberOfSections * sizeof(*sections))) {
        return EFI_LOAD_ERROR;
    }

    sections = (VOID*)(image + sectionOffset);
    for (UINTN i = 0; i < header->FileHeader.NumberOfSections; i++) {
        UINT64 end = (UINT64)sections[i].VirtualAddress + sections[i].Misc.VirtualSize;
        
        if (sections[i].VirtualAddress < Context->SizeOfHeaders || end > Context->ImageSize) {
            return EFI_LOAD_ERROR;
        }

        for (UINTN j = 0; j < i; j++) {
            if (sections[i].VirtualAddress < (UINT64)sections[j].VirtualAddress + sections[j].Misc.VirtualSize &&
                sections[j].VirtualAddress < end) {
                return EFI_LOAD_ERROR;
            }
        }
    }

    if ((header->OptionalHeader.AddressOfEntryPoint & 3) ||
        !__InSection(sections, header->FileHeader.NumberOfSections,
            header->OptionalHeader.AddressOfEntryPoint, 4, TRUE)) {
        return EFI_LOAD_ERROR;
    }

    if (header->OptionalHeader.NumberOfRvaAndSizes <= EFI_IMAGE_DIRECTORY_ENTRY_BASERELOC) {
        return EFI_SUCCESS;
    }
    
    directory = header->OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (!directory.Size) {
        return EFI_SUCCESS;
    }

    if (Context->RelocationsStripped || !__InSection(sections, header->FileHeader.NumberOfSections,
            directory.VirtualAddress, directory.Size, FALSE)) {
        return EFI_LOAD_ERROR;
    }

    offset = 0;
    while (offset < directory.Size) {
        EFI_IMAGE_BASE_RELOCATION block;

        if (directory.Size - offset < sizeof(EFI_IMAGE_BASE_RELOCATION)) {
            return EFI_LOAD_ERROR;
        }
        
        CopyMem(
            &block,
            image + directory.VirtualAddress + offset,
            sizeof(EFI_IMAGE_BASE_RELOCATION)
        );

        if (block.SizeOfBlock < sizeof(EFI_IMAGE_BASE_RELOCATION) || block.SizeOfBlock % 4 ||
            block.SizeOfBlock > directory.Size - offset) {
            return EFI_LOAD_ERROR;
        }
        
        if ((block.VirtualAddress & (EFI_PAGE_SIZE - 1)) || block.VirtualAddress >= Context->ImageSize) {
            return EFI_LOAD_ERROR;
        }
        
        // Relocation entries are 16-bit values after the block header.
        for (UINTN item = sizeof(EFI_IMAGE_BASE_RELOCATION); item < block.SizeOfBlock; item += sizeof(UINT16)) {
            UINT16 fixup;
            UINT64 target;
            
            CopyMem(&fixup, image + directory.VirtualAddress + offset + item, sizeof(UINT16));
            
            if ((fixup >> 12) == EFI_IMAGE_REL_BASED_ABSOLUTE) {
                continue;
            }

            // Accept only DIR64 relocations for ARM64.
            if ((fixup >> 12) != EFI_IMAGE_REL_BASED_DIR64) {
                return EFI_UNSUPPORTED;
            }

            target = (UINT64)block.VirtualAddress + (fixup & 0xFFF);
            if (target > MAX_UINT32 || !__InSection(sections, header->FileHeader.NumberOfSections,
                    (UINT32)target, sizeof(UINT64), FALSE)) {
                return EFI_LOAD_ERROR;
            }

            // A fixup cannot overwrite the directory while EDK2 is walking it.
            if (target < (UINT64)directory.VirtualAddress + directory.Size &&
                directory.VirtualAddress < target + sizeof(UINT64)) {
                return EFI_LOAD_ERROR;
            }
        }

        offset += block.SizeOfBlock;
    }

    *CanMove = TRUE;
    return EFI_SUCCESS;
}

static EFI_STATUS
__AllocateImage(
    IN UINT64                 Preferred,
    IN UINTN                  Pages,
    IN UINTN                  Alignment,
    OUT EFI_PHYSICAL_ADDRESS* Base,
    OUT UINTN*                AllocatedPages)
{
    EFI_STATUS status;

    // Try the preferred address first. A failed reservation leaves existing firmware data untouched.
    *Base = Preferred;
    *AllocatedPages = Pages;
    
    if (Preferred && !(Preferred & (Alignment - 1)) && Preferred < ARM64_KERNEL_LIMIT &&
        EFI_PAGES_TO_SIZE(Pages) <= ARM64_KERNEL_LIMIT - Preferred) {
        status = gBS->AllocatePages(AllocateAddress, EfiLoaderCode, Pages, Base);
        if (!EFI_ERROR(status)) {
            return status;
        }
    }
    
    // Reserve alignment padding with the image so cleanup can free one allocation.
    *AllocatedPages = Pages + EFI_SIZE_TO_PAGES(Alignment) - 1;
    *Base = ARM64_KERNEL_LIMIT - 1;
    return gBS->AllocatePages(AllocateMaxAddress, EfiLoaderCode, *AllocatedPages, Base);
}

EFI_STATUS
Arm64LoadKernelImage(
    IN  const VOID*         Buffer,
    IN  UINTN               Size,
    OUT struct VBootModule* Kernel)
{
    struct __ImageFile           file = { Buffer, Size };
    PE_COFF_LOADER_IMAGE_CONTEXT context = { 0 };
    EFI_PHYSICAL_ADDRESS         allocation;
    UINT64                       preferred;
    UINTN                        pages;
    UINTN                        allocatedPages;
    UINTN                        alignment;
    EFI_STATUS                   status;
    BOOLEAN                      canMove;

    // Clear the output before any operation can fail.
    SetMem(Kernel, sizeof(struct VBootModule), 0);
    
    if (!Size || Size > ARM64_KERNEL_MAX_SIZE) {
        return EFI_BAD_BUFFER_SIZE;
    }

    context.Handle = &file;
    context.ImageRead = __ReadImage;
    
    status = PeCoffLoaderGetImageInfo(&context);
    if (EFI_ERROR(status)) {
        return status;
    }

    if (context.Machine != EFI_IMAGE_MACHINE_AARCH64 || context.IsTeImage ||
        !context.ImageSize || context.ImageSize > ARM64_KERNEL_MAX_SIZE) {
        return EFI_UNSUPPORTED;
    }

    if (!context.SectionAlignment || (context.SectionAlignment & (context.SectionAlignment - 1)) ||
        context.SectionAlignment > ARM64_KERNEL_MAX_SIZE) {
        return EFI_UNSUPPORTED;
    }

    status = __ValidateFileSections(&file, &context);
    if (EFI_ERROR(status)) {
        return status;
    }

    preferred = context.ImageAddress;
    alignment = MAX(0x10000U, context.SectionAlignment);
    pages = EFI_SIZE_TO_PAGES(context.ImageSize);
    status = __AllocateImage(preferred, pages, alignment, &allocation, &allocatedPages);
    if (EFI_ERROR(status)) {
        return status;
    }

    context.ImageAddress = (allocation + alignment - 1) & ~(UINT64)(alignment - 1);
    status = PeCoffLoaderLoadImage(&context);
    if (EFI_ERROR(status)) {
        goto failed;
    }

    status = __ValidateLoadedImage(&context, &canMove);
    if (EFI_ERROR(status)) {
        goto failed;
    }
    
    // Images without relocations can load only at their preferred address.
    if (context.ImageAddress != preferred && !canMove) {
        status = EFI_UNSUPPORTED;
        goto failed;
    }

    status = PeCoffLoaderRelocateImage(&context);
    if (EFI_ERROR(status)) {
        goto failed;
    }
    
    InvalidateInstructionCacheRange((VOID*)(UINTN)context.ImageAddress, context.ImageSize);
   
    Kernel->Base = context.ImageAddress;
    Kernel->Length = context.ImageSize;
    Kernel->EntryPoint = context.EntryPoint;
    
    // Keep alignment padding reserved with the image; the kernel can reclaim it later.
    return EFI_SUCCESS;

failed:
    gBS->FreePages(allocation, allocatedPages);
    return status;
}
