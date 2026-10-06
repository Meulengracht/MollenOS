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

#include "loader.h"
#include <os/pe.h>
#include <string.h>

extern unsigned char __rpi_loader_start[];

// Validated source metadata; pointers borrow the immutable packaged PE file.
struct __PeImage {
    const unsigned char*     File;
    PeHeader_t               Header;
    PeOptionalHeader64_t     Optional;
    const PeSectionHeader_t* Sections;
    const unsigned char*     Relocations;
};

static int
__PeContains(
    uint64_t offset,
    uint64_t length,
    uint64_t limit)
{
    return offset <= limit && length <= limit - offset;
}

static const PeSectionHeader_t*
__PeFindSection(
    _In_ const struct __PeImage* image,
    _In_ uint64_t                rva,
    _In_ uint32_t                length,
    _In_ int                     fileBacked)
{
    // Image bounds alone would also admit headers, alignment holes and BSS.
    // Directories need real section storage, and relocation metadata must
    // additionally exist in the file rather than in a zero-filled tail.
    for (unsigned int i = 0; i < image->Header.NumSections; i++) {
        const PeSectionHeader_t* section = &image->Sections[i];
        uint64_t offset;

        if (rva < section->VirtualAddress) {
            continue;
        }
        offset = rva - section->VirtualAddress;
        
        // Matching an RVA alone does not prove the complete range fits the section.
        if (!__PeContains(offset, length, section->VirtualSize)) {
            continue;
        }
        
        // Metadata read from the file cannot borrow bytes from a zero-filled tail.
        if (fileBacked && !__PeContains(offset, length, section->RawSize)) {
            continue;
        }
        return section;
    }
    return NULL;
}

static enum RpiBootStatus
__PeValidateSections(
    _In_ const struct __PeImage* image,
    _In_ size_t                  length)
{
    uint64_t                 previousEnd = image->Optional.SizeOfHeaders;
    const PeSectionHeader_t* entry;

    // LLD emits sections in RVA order. A running end rejects overlapping RAM
    // destinations before any physical storage is written.
    for (unsigned int i = 0; i < image->Header.NumSections; i++) {
        const PeSectionHeader_t* section = &image->Sections[i];
        uint32_t                 extent = section->VirtualSize;

        if (section->RawSize > extent) {
            extent = section->RawSize;
        }
        
        // Each nonempty section must follow the previous destination on a page boundary.
        if (!extent || section->VirtualAddress < previousEnd ||
            (section->VirtualAddress & RPI_PAGE_MASK)) {
            return RpiBootInvalidPayload;
        }
        
        // Do not let section bytes extend beyond the expanded image allocation.
        if (!__PeContains(section->VirtualAddress, extent, image->Optional.SizeOfImage)) {
            return RpiBootInvalidPayload;
        }
        
        if (section->RawSize) {
            // Raw bytes must start after the headers and follow file alignment rules.
            if (section->RawAddress < image->Optional.SizeOfHeaders ||
                (section->RawAddress & (RPI_PE_FILE_ALIGNMENT - 1)) ||
                (section->RawSize & (RPI_PE_FILE_ALIGNMENT - 1))) {
                return RpiBootInvalidPayload;
            }
            // Every initialized byte must actually exist in the source file.
            if (!__PeContains(section->RawAddress, section->RawSize, length)) {
                return RpiBootInvalidPayload;
            }
        }
        previousEnd = (uint64_t)section->VirtualAddress + extent;
    }

    entry = __PeFindSection(
        image,
        image->Optional.Base.EntryPointRVA,
        RPI_ARM64_INSTRUCTION_SIZE,
        1
    );
    // The entry must name a complete aligned instruction in executable file data.
    if (!entry || !(entry->Flags & PE_SECTION_EXECUTE) ||
        (image->Optional.Base.EntryPointRVA & (RPI_ARM64_INSTRUCTION_SIZE - 1))) {
        return RpiBootInvalidPayload;
    }
    return RpiBootOk;
}

static int
__PeDirectorySupported(
    _In_ unsigned int directory,
    _In_ int          userspace)
{
    // These directories contain data the kernel can retain without a loader runtime.
    switch (directory) {
        case PE_SECTION_EXPORT:
        case PE_SECTION_RESOURCE:
        case PE_SECTION_EXCEPTION:
        case PE_SECTION_DEBUG:
        case PE_SECTION_BASE_RELOCATION:
            return 1;
        case PE_SECTION_TLS:
            return userspace;
        default:
            return 0;
    }
}

static enum RpiBootStatus
__PeValidateDirectories(
    _InOut_ struct __PeImage* image,
    _In_    int               userspace)
{
    // Passive metadata may remain, but directories requiring another runtime
    // must fail rather than produce a half-initialized kernel.
    image->Relocations = NULL;
    for (unsigned int i = 0; i < PE_NUM_DIRECTORIES; i++) {
        const PeDataDirectory_t* directory = &image->Optional.Directories[i];
        const PeSectionHeader_t* section;

        if (!directory->AddressRVA && !directory->Size) {
            continue;
        }
        
        // Vali LLD publishes the end address of an empty auto-relocation table.
        if (i == PE_SECTION_GLOBAL_PTR && !directory->Size &&
            directory->AddressRVA < image->Optional.SizeOfImage) {
            continue;
        }
        
        // A directory needs both its address and length.
        if (!directory->AddressRVA || !directory->Size) {
            return RpiBootInvalidPayload;
        }
        
        // Reject directories that would require imports, callbacks or another runtime.
        if (!__PeDirectorySupported(i, userspace)) {
            return RpiBootUnsupported;
        }
        
        section = __PeFindSection(image, directory->AddressRVA, directory->Size, 1);
        if (!section) {
            return RpiBootInvalidPayload;
        }
        
        if (i == PE_SECTION_BASE_RELOCATION) {
            // A file cannot both prohibit relocation and provide relocation records.
            if (image->Header.Attributes & PE_ATTRIBUTE_NORELOCATION) {
                return RpiBootInvalidPayload;
            }
            image->Relocations = image->File + section->RawAddress +
                (directory->AddressRVA - section->VirtualAddress);
        }
    }
    return RpiBootOk;
}

static enum RpiBootStatus
__PeReadImage(
    _In_ const unsigned char* file,
    _In_ size_t               length,
    _In_ struct __PeImage*    image,
    _In_ int                  userspace)
{
    MzHeader_t         mz;
    uint64_t           offset;
    enum RpiBootStatus status;

    // Establish the first readable header before copying any fields from the file.
    if (length < sizeof(mz)) {
        return RpiBootInvalidPayload;
    }
    
    // The appended file need not align every PE field. Copies into packed
    // definitions retain the shared format without requiring unaligned loads
    // while translation is off; the wrapper is built with -mstrict-align.
    memcpy(&mz, file, sizeof(mz));
    
    // The DOS header must point past itself to a complete PE header in the file.
    if (mz.Signature != MZ_MAGIC || mz.PeHeaderAddress < sizeof(mz) ||
        !__PeContains(mz.PeHeaderAddress, sizeof(image->Header), length)) {
        return RpiBootInvalidPayload;
    }
    
    memcpy(&image->Header, file + mz.PeHeaderAddress, sizeof(image->Header));
    
    // A valid executable must have its signature, sections and executable flag.
    if (image->Header.Magic != PE_MAGIC || !image->Header.NumSections ||
        !(image->Header.Attributes & PE_ATTRIBUTE_VALID)) {
        return RpiBootInvalidPayload;
    }
    
    // This loader accepts only static ARM64 files with the full PE32+ header.
    if (image->Header.Machine != PE_MACHINE_ARM64 ||
        (image->Header.Attributes & (PE_ATTRIBUTE_DLL | PE_ATTRIBUTE_32BIT)) ||
        image->Header.SizeOfOptionalHeader != sizeof(image->Optional)) {
        return RpiBootUnsupported;
    }
    
    offset = (uint64_t)mz.PeHeaderAddress + sizeof(image->Header);
    // Verify the optional header's bytes before decoding it.
    if (!__PeContains(offset, sizeof(image->Optional), length)) {
        return RpiBootInvalidPayload;
    }
    
    memcpy(&image->Optional, file + offset, sizeof(image->Optional));
    // Directory and field layouts must match the shared PE32+ definitions.
    if (image->Optional.Base.Architecture != PE_ARCHITECTURE_64 ||
        image->Optional.NumDataDirectories != PE_NUM_DIRECTORIES) {
        return RpiBootUnsupported;
    }

    // The supported linker profile uses page-sized sections and 512-byte file blocks.
    if (image->Optional.SectionAlignment != RPI_PAGE_SIZE ||
        image->Optional.FileAlignment != RPI_PE_FILE_ALIGNMENT) {
        return RpiBootUnsupported;
    }
    
    // Bound expanded storage and require whole pages before choosing a destination.
    if (!image->Optional.SizeOfImage || image->Optional.SizeOfImage > RPI_PE_IMAGE_MAX_SIZE ||
        (image->Optional.SizeOfImage & RPI_PAGE_MASK)) {
        return RpiBootInvalidPayload;
    }

    // Headers must occupy complete file blocks and contain at least one byte.
    if (!image->Optional.SizeOfHeaders ||
        (image->Optional.SizeOfHeaders & (RPI_PE_FILE_ALIGNMENT - 1))) {
        return RpiBootInvalidPayload;
    }

    // Header copies must fit both the source file and the expanded destination.
    if (image->Optional.SizeOfHeaders > length ||
        image->Optional.SizeOfHeaders > image->Optional.SizeOfImage) {
        return RpiBootInvalidPayload;
    }

    // The preferred base must be aligned and leave room for a nonwrapping image end.
    if ((image->Optional.BaseAddress & (RPI_PE_IMAGE_ALIGNMENT - 1)) ||
        image->Optional.BaseAddress > UINT64_MAX - image->Optional.SizeOfImage) {
        return RpiBootInvalidPayload;
    }
    
    offset += sizeof(image->Optional);
    // All section records must fit in the validated header area.
    if (!__PeContains(offset, (uint64_t)image->Header.NumSections * sizeof(PeSectionHeader_t),
            image->Optional.SizeOfHeaders)) {
        return RpiBootInvalidPayload;
    }
    
    image->File = file;
    image->Sections = (const PeSectionHeader_t*)(file + offset);
    status = __PeValidateSections(image, length);
    if (status != RpiBootOk) {
        return status;
    }
    return __PeValidateDirectories(image, userspace);
}

static enum RpiBootStatus
__PeRelocateImage(
    _In_ const struct __PeImage* image,
    _In_ unsigned char*          destination)
{
    uint32_t length = image->Optional.Directories[PE_SECTION_BASE_RELOCATION].Size;
    uint32_t offset = 0;
    uint64_t delta = (uintptr_t)destination - image->Optional.BaseAddress;

    // Read the immutable file, never the copied .reloc section. Otherwise a
    // fixup targeting relocation storage could change the remaining walk.
    // A validation pass with no destination keeps failures ahead of allocation.
    while (offset < length) {
        uint32_t page;
        uint32_t blockSize;

        // A truncated relocation header cannot supply a safe block length.
        if (length - offset < RPI_PE_RELOCATION_HEADER_SIZE) {
            return RpiBootInvalidPayload;
        }
        
        memcpy(&page, image->Relocations + offset, sizeof(page));
        memcpy(&blockSize, image->Relocations + offset + sizeof(page), sizeof(blockSize));
        
        // Relocation offsets are relative to a complete page inside this image.
        if ((page & RPI_PAGE_MASK) || page >= image->Optional.SizeOfImage) {
            return RpiBootInvalidPayload;
        }

        // The complete aligned block must fit in the remaining relocation bytes.
        if (blockSize < RPI_PE_RELOCATION_HEADER_SIZE ||
            (blockSize & (sizeof(uint32_t) - 1)) || blockSize > length - offset) {
            return RpiBootInvalidPayload;
        }
        
        for (uint32_t i = RPI_PE_RELOCATION_HEADER_SIZE; i < blockSize; i += sizeof(uint16_t)) {
            uint16_t fixup;
            uint64_t target;
            uint64_t value;
            const PeSectionHeader_t* section;

            memcpy(&fixup, image->Relocations + offset + i, sizeof(fixup));
            
            // The upper four bits select the relocation kind; the lower twelve
            // address a byte within the block's 4 KiB page.
            if ((fixup >> RPI_PE_RELOCATION_TYPE_SHIFT) == PE_RELOCATION_ALIGN) {
                continue;
            }
            
            // Only 64-bit absolute pointer updates are supported by this profile.
            if ((fixup >> RPI_PE_RELOCATION_TYPE_SHIFT) != PE_RELOCATION_RELATIVE64) {
                return RpiBootUnsupported;
            }
            
            target = (uint64_t)page + (fixup & RPI_PE_RELOCATION_OFFSET_MASK);
            section = __PeFindSection(image, target, sizeof(value), 0);
            
            // The whole pointer must belong to a section, not an image hole.
            if (!section) {
                return RpiBootInvalidPayload;
            }
            
            if (destination) {
                // Absolute pointers use modulo-64-bit base differences, which
                // also handles loading below the preferred address. memcpy
                // permits packed pointers without assuming aligned storage.
                memcpy(&value, destination + target, sizeof(value));
                value += delta;
                memcpy(destination + target, &value, sizeof(value));
            }
        }
        offset += blockSize;
    }
    return RpiBootOk;
}

static uint64_t
__PeFindDestination(
    _In_ const struct RpiBootContext* context,
    _In_ const struct __PeImage*      image,
    _In_ int                          staging)
{
    uint64_t fallback = 0;
    uint64_t preferred = image->Optional.BaseAddress;
    uint32_t length = image->Optional.SizeOfImage;
    uint64_t sourceEnd = (uintptr_t)__rpi_loader_start + context->Kernel.ImageLength;

    // Platform preparation already excluded DTB, initrd, firmware and wrapper
    // storage. Only a single available interval may back the entire image;
    // assembling an allocation across holes would overwrite somebody else's RAM.
    for (uint32_t i = 0; i < context->MemoryMapCount; i++) {
        const struct VBootMemoryEntry* entry = &context->MemoryMap[i];

        uint64_t base = entry->PhysicalBase;
        uint64_t end;
        uint64_t candidate;

        // Only mapped free RAM with a representable end can back the image.
        if (entry->Type != VBootMemoryType_Available ||
            entry->Length > UINTPTR_MAX - base) {
            continue;
        }
        
        end = base + entry->Length;
        if (base < sourceEnd) {
            base = sourceEnd;
        }
        
        // Prefer the linked address when the complete image fits one interval.
        if (preferred >= base && preferred < end && length <= end - preferred) {
            return preferred;
        }
        
        // Rounding the fallback upward must not wrap the physical address.
        if (base > UINTPTR_MAX - (RPI_PE_IMAGE_ALIGNMENT - 1)) {
            continue;
        }
        
        candidate = (base + RPI_PE_IMAGE_ALIGNMENT - 1) & ~(uint64_t)(RPI_PE_IMAGE_ALIGNMENT - 1);
        // Retain the first nonzero fallback; subtraction is safe only below end.
        if (!fallback && candidate && candidate < end) {
            if (length <= end - candidate) {
                fallback = candidate;
            }
        }
    }
    // Without a relocation directory there is no proof that absolute pointers
    // can move. A fixed image is usable only at its preferred physical address.
    return (staging || image->Relocations) ? fallback : 0;
}

static void
__PeCopyImage(
    _In_  const struct __PeImage* image,
    _Out_ unsigned char*         destination)
{
    memset(destination, 0, image->Optional.SizeOfImage);
    memcpy(destination, image->File, image->Optional.SizeOfHeaders);
    for (unsigned int i = 0; i < image->Header.NumSections; i++) {
        const PeSectionHeader_t* section = &image->Sections[i];

        if (section->RawSize) {
            memcpy(
                destination + section->VirtualAddress,
                image->File + section->RawAddress,
                section->RawSize
            );
        }
    }
}

enum RpiBootStatus
RpiLoadKernel(
    struct RpiBootContext* context)
{
    struct __PeImage   image;
    enum RpiBootStatus status;
    uint64_t           base;
    unsigned char*     destination;
    oserr_t            oserr;

    context->BootInformation.Kernel = (struct VBootModule){0};
    
    // Bound the source payload before constructing a pointer into the wrapper file.
    if (!context->Kernel.Length || context->Kernel.Length > RPI_PAYLOAD_MAX_SIZE) {
        return RpiBootInvalidPayload;
    }
    
    // The validated trailer's payload interval must stay inside the complete file.
    if (!__PeContains(context->Kernel.Offset, context->Kernel.Length, context->Kernel.ImageLength)) {
        return RpiBootInvalidPayload;
    }

    // The complete source file must have a representable physical end address.
    if (context->Kernel.ImageLength > UINTPTR_MAX - (uintptr_t)__rpi_loader_start) {
        return RpiBootInvalidPayload;
    }
    
    // Image allocation needs the complete ownership map from platform discovery.
    if (!context->MemoryMapCount || context->MemoryMapCount > RPI_MEMORY_MAP_CAPACITY) {
        return RpiBootInvalidPlatform;
    }
    
    status = __PeReadImage(__rpi_loader_start + context->Kernel.Offset, context->Kernel.Length, &image, 0);
    if (status != RpiBootOk) {
        return status;
    }
    
    status = __PeRelocateImage(&image, NULL);
    if (status != RpiBootOk) {
        return status;
    }
    
    base = __PeFindDestination(context, &image, 0);
    if (!base) {
        return RpiBootNoMemory;
    }

    // Ownership must change before copying. A full map is a boot failure, not
    // permission to use unrecorded RAM. Reservation failure invalidates the map;
    // publish its count even on failure so VBoot cannot expose stale entries.
    oserr = DeviceTreeReserveMemory(context, base, image.Optional.SizeOfImage);
    context->BootInformation.Memory.NumberOfEntries = context->MemoryMapCount;
    if (oserr != OS_EOK) {
        return RpiBootNoMemory;
    }
   
    destination = (unsigned char*)(uintptr_t)base;
    __PeCopyImage(&image, destination);
    
    status = __PeRelocateImage(&image, destination);
    if (status != RpiBootOk) {
        return status;
    }

    // This profile executes with translation off, so entry and relocation use
    // the actual physical base. Retain the original PE headers for diagnostics;
    // the module descriptor records the loaded address. Instruction visibility
    // and the EL/stack transition still belong to RpiTransferToKernel.
    context->BootInformation.Kernel.Base = base;
    context->BootInformation.Kernel.EntryPoint = base + image.Optional.Base.EntryPointRVA;
    context->BootInformation.Kernel.Length = image.Optional.SizeOfImage;
    return RpiBootOk;
}

// Phoenix is mapped by SpawnBootstrapper at the ImageBase in its PE header.
// Applying physical staging relocations here would corrupt every absolute
// userspace pointer, even though the file and its entry would appear valid.
enum RpiBootStatus
RpiLoadPhoenix(
    _In_ struct RpiBootContext* context,
    _In_ const void*           file,
    _In_ size_t                length)
{
    struct __PeImage   image;
    enum RpiBootStatus status;
    unsigned char*     destination;
    uint64_t           base;
    oserr_t            oserr;

    context->BootInformation.Phoenix = (struct VBootModule){0};
    status = __PeReadImage(file, length, &image, 1);
    if (status != RpiBootOk) {
        return status;
    }
    
    status = __PeRelocateImage(&image, NULL);
    if (status != RpiBootOk) {
        return status;
    }

    base = __PeFindDestination(context, &image, 1);
    if (!base) {
        return RpiBootNoMemory;
    }
    
    oserr = DeviceTreeReserveMemory(context, base, image.Optional.SizeOfImage);
    context->BootInformation.Memory.NumberOfEntries = context->MemoryMapCount;
    if (oserr != OS_EOK) {
        return RpiBootNoMemory;
    }

    destination = (unsigned char*)(uintptr_t)base;
    __PeCopyImage(&image, destination);

    context->BootInformation.Phoenix.Base = base;
    context->BootInformation.Phoenix.Length = image.Optional.SizeOfImage;
    context->BootInformation.Phoenix.EntryPoint = image.Optional.BaseAddress +
        image.Optional.Base.EntryPointRVA;
    return RpiBootOk;
}
