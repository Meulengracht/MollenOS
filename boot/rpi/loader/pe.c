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

#define PE_IMAGE_MAX_SIZE (64u * 1024u * 1024u)

#define PE_IMAGE_ALIGNMENT 65536u

#define PE_PAGE_SIZE 4096u

extern unsigned char __rpi_loader_start[];

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
    const struct __PeImage* image,
    uint64_t               rva,
    uint32_t               length,
    int                    fileBacked)
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
        if (__PeContains(offset, length, section->VirtualSize) &&
            (!fileBacked || __PeContains(offset, length, section->RawSize))) {
            return section;
        }
    }
    return NULL;
}

static enum RpiBootStatus
__PeReadImage(
    const unsigned char* file,
    size_t               length,
    struct __PeImage*    image)
{
    MzHeader_t               mz;
    uint64_t                 offset;
    uint64_t                 previousEnd;
    const PeSectionHeader_t* entry;

    if (length < sizeof(mz)) {
        return RpiBootInvalidPayload;
    }
    
    // The appended file need not align every PE field. Copies into packed
    // definitions retain the shared format without requiring unaligned loads
    // while translation is off; the wrapper is built with -mstrict-align.
    memcpy(&mz, file, sizeof(mz));
    if (mz.Signature != MZ_MAGIC || mz.PeHeaderAddress < sizeof(mz) ||
        !__PeContains(mz.PeHeaderAddress, sizeof(image->Header), length)) {
        return RpiBootInvalidPayload;
    }
    
    memcpy(&image->Header, file + mz.PeHeaderAddress, sizeof(image->Header));
    if (image->Header.Magic != PE_MAGIC || !image->Header.NumSections ||
        !(image->Header.Attributes & PE_ATTRIBUTE_VALID)) {
        return RpiBootInvalidPayload;
    }
    
    if (image->Header.Machine != PE_MACHINE_ARM64 ||
        (image->Header.Attributes & (PE_ATTRIBUTE_DLL | PE_ATTRIBUTE_32BIT)) ||
        image->Header.SizeOfOptionalHeader != sizeof(image->Optional)) {
        return RpiBootUnsupported;
    }
    
    offset = (uint64_t)mz.PeHeaderAddress + sizeof(image->Header);
    if (!__PeContains(offset, sizeof(image->Optional), length)) {
        return RpiBootInvalidPayload;
    }
    
    memcpy(&image->Optional, file + offset, sizeof(image->Optional));
    if (image->Optional.Base.Architecture != PE_ARCHITECTURE_64 ||
        image->Optional.NumDataDirectories != PE_NUM_DIRECTORIES ||
        image->Optional.SectionAlignment != PE_PAGE_SIZE ||
        image->Optional.FileAlignment != 512) {
        return RpiBootUnsupported;
    }
    
    if (!image->Optional.SizeOfImage || image->Optional.SizeOfImage > PE_IMAGE_MAX_SIZE ||
        (image->Optional.SizeOfImage & (PE_PAGE_SIZE - 1)) ||
        !image->Optional.SizeOfHeaders || (image->Optional.SizeOfHeaders & 511) ||
        image->Optional.SizeOfHeaders > length ||
        image->Optional.SizeOfHeaders > image->Optional.SizeOfImage ||
        (image->Optional.BaseAddress & (PE_IMAGE_ALIGNMENT - 1)) ||
        image->Optional.BaseAddress > UINT64_MAX - image->Optional.SizeOfImage) {
        return RpiBootInvalidPayload;
    }
    
    offset += sizeof(image->Optional);
    if (!__PeContains(offset, (uint64_t)image->Header.NumSections * sizeof(PeSectionHeader_t),
            image->Optional.SizeOfHeaders)) {
        return RpiBootInvalidPayload;
    }
    
    image->File = file;
    image->Sections = (const PeSectionHeader_t*)(file + offset);
    previousEnd = image->Optional.SizeOfHeaders;

    // LLD emits sections in RVA order. Requiring that layout lets one running
    // end reject overlapping destinations before any physical RAM is written.
    for (unsigned int i = 0; i < image->Header.NumSections; i++) {
        const PeSectionHeader_t* section = &image->Sections[i];
        uint32_t extent = section->VirtualSize;

        if (section->RawSize > extent) {
            extent = section->RawSize;
        }
        
        if (!extent || section->VirtualAddress < previousEnd ||
            (section->VirtualAddress & (PE_PAGE_SIZE - 1)) ||
            !__PeContains(section->VirtualAddress, extent, image->Optional.SizeOfImage) ||
            (section->RawSize && (section->RawAddress < image->Optional.SizeOfHeaders ||
                (section->RawAddress & 511) || (section->RawSize & 511) ||
                !__PeContains(section->RawAddress, section->RawSize, length)))) {
            return RpiBootInvalidPayload;
        }
        previousEnd = (uint64_t)section->VirtualAddress + extent;
    }
    
    entry = __PeFindSection(image, image->Optional.Base.EntryPointRVA, 4, 1);
    if (!entry || !(entry->Flags & PE_SECTION_EXECUTE) ||
        (image->Optional.Base.EntryPointRVA & 3)) {
        return RpiBootInvalidPayload;
    }

    // This is a statically linked kernel, not a process. Passive metadata may
    // remain in the image, but directories needing imports, TLS callbacks or
    // another runtime must fail rather than produce a half-initialized kernel.
    image->Relocations = NULL;
    for (unsigned int i = 0; i < PE_NUM_DIRECTORIES; i++) {
        const PeDataDirectory_t* directory = &image->Optional.Directories[i];
        const PeSectionHeader_t* section;

        if (!directory->AddressRVA && !directory->Size) {
            continue;
        }
        
        if (!directory->AddressRVA || !directory->Size) {
            return RpiBootInvalidPayload;
        }
        
        if (i != PE_SECTION_EXPORT && i != PE_SECTION_RESOURCE &&
            i != PE_SECTION_EXCEPTION && i != PE_SECTION_DEBUG &&
            i != PE_SECTION_BASE_RELOCATION) {
            return RpiBootUnsupported;
        }
        
        section = __PeFindSection(image, directory->AddressRVA, directory->Size, 1);
        if (!section) {
            return RpiBootInvalidPayload;
        }
        
        if (i == PE_SECTION_BASE_RELOCATION) {
            if (image->Header.Attributes & PE_ATTRIBUTE_NORELOCATION) {
                return RpiBootInvalidPayload;
            }
            image->Relocations = file + section->RawAddress +
                (directory->AddressRVA - section->VirtualAddress);
        }
    }
    return RpiBootOk;
}

static enum RpiBootStatus
__PeRelocateImage(
    const struct __PeImage* image,
    unsigned char*         destination)
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

        if (length - offset < 8) {
            return RpiBootInvalidPayload;
        }
        
        memcpy(&page, image->Relocations + offset, sizeof(page));
        memcpy(&blockSize, image->Relocations + offset + 4, sizeof(blockSize));
        if ((page & (PE_PAGE_SIZE - 1)) || page >= image->Optional.SizeOfImage ||
            blockSize < 8 || (blockSize & 3) || blockSize > length - offset) {
            return RpiBootInvalidPayload;
        }
        
        for (uint32_t i = 8; i < blockSize; i += 2) {
            uint16_t fixup;
            uint64_t target;
            uint64_t value;

            memcpy(&fixup, image->Relocations + offset + i, sizeof(fixup));
            if ((fixup >> 12) == PE_RELOCATION_ALIGN) {
                continue;
            }
            
            if ((fixup >> 12) != PE_RELOCATION_RELATIVE64) {
                return RpiBootUnsupported;
            }
            
            target = (uint64_t)page + (fixup & 0xfff);
            if (!__PeFindSection(image, target, sizeof(value), 0)) {
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
    const struct RpiBootContext* context,
    const struct __PeImage*      image)
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

        if (entry->Type != VBootMemoryType_Available ||
            entry->Length > UINTPTR_MAX - base) {
            continue;
        }
        
        end = base + entry->Length;
        if (base < sourceEnd) {
            base = sourceEnd;
        }
        
        if (preferred >= base && preferred < end && length <= end - preferred) {
            return preferred;
        }
        
        if (base > UINTPTR_MAX - (PE_IMAGE_ALIGNMENT - 1)) {
            continue;
        }
        
        candidate = (base + PE_IMAGE_ALIGNMENT - 1) & ~(uint64_t)(PE_IMAGE_ALIGNMENT - 1);
        if (!fallback && candidate && candidate < end && length <= end - candidate) {
            fallback = candidate;
        }
    }
    // Without a relocation directory there is no proof that absolute pointers
    // can move. A fixed image is usable only at its preferred physical address.
    return image->Relocations ? fallback : 0;
}

enum RpiBootStatus
RpiLoadKernel(
    struct RpiBootContext* context)
{
    struct __PeImage image;
    enum RpiBootStatus status;
    uint64_t base;
    unsigned char* destination;
    oserr_t oserr;

    if (!context) {
        return RpiBootInvalidPayload;
    }
    
    context->BootInformation.Kernel = (struct VBootModule){0};
    if (!context->Kernel.Length || context->Kernel.Length > RPI_PAYLOAD_MAX_SIZE ||
        !__PeContains(context->Kernel.Offset, context->Kernel.Length, context->Kernel.ImageLength) ||
        context->Kernel.ImageLength > UINTPTR_MAX - (uintptr_t)__rpi_loader_start) {
        return RpiBootInvalidPayload;
    }
    
    if (!context->MemoryMapCount || context->MemoryMapCount > RPI_MEMORY_MAP_CAPACITY) {
        return RpiBootInvalidPlatform;
    }
    
    status = __PeReadImage(__rpi_loader_start + context->Kernel.Offset, context->Kernel.Length, &image);
    if (status != RpiBootOk) {
        return status;
    }
    
    status = __PeRelocateImage(&image, NULL);
    if (status != RpiBootOk) {
        return status;
    }
    
    base = __PeFindDestination(context, &image);
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
    memset(destination, 0, image.Optional.SizeOfImage);
    memcpy(destination, image.File, image.Optional.SizeOfHeaders);
    for (unsigned int i = 0; i < image.Header.NumSections; i++) {
        const PeSectionHeader_t* section = &image.Sections[i];

        if (section->RawSize) {
            memcpy(destination + section->VirtualAddress,
                image.File + section->RawAddress, section->RawSize);
        }
    }
    
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
