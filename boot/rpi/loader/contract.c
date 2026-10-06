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

extern unsigned char __rpi_loader_start[];
extern unsigned char __rpi_loader_end[];
extern unsigned char __rpi_boot_stack_bottom[];
extern unsigned char __rpi_boot_stack_top[];

static int
__ContractRangeReserved(
    const struct RpiBootContext* context,
    uint64_t                     base,
    uint64_t                     length)
{
    uint64_t end;

    if (!length || length > UINT64_MAX - base) {
        return 0;
    }
    end = base + length;

    // A range that is not listed as free RAM is not necessarily ours to use.
    // It might be missing from the memory map, or its addresses might refer to
    // device registers rather than RAM. Every byte in the requested range must
    // therefore be covered by entries marked reserved, with no gaps between
    // them. This tells the kernel that the memory is already in use and must
    // not be handed out for other purposes when it starts allocating pages.
    for (uint32_t i = 0; i < context->MemoryMapCount; i++) {
        const struct VBootMemoryEntry* entry;
        uint64_t                       entryEnd;


        entry = &context->MemoryMap[i];
        entryEnd = entry->PhysicalBase + entry->Length;
        if (entryEnd <= base) {
            continue;
        }
        
        if (entry->PhysicalBase > base || entry->Type != VBootMemoryType_Reserved ||
            (entry->Attributes & VBOOT_MEMORY_NO_MAP)) {
            return 0;
        }
        
        if (entryEnd >= end) {
            return 1;
        }
        base = entryEnd;
    }
    return 0;
}

static int
__ContractMemoryValid(
    const struct RpiBootContext* context)
{
    const struct VBootMemory* memory = &context->BootInformation.Memory;
    uint64_t                  previousEnd = 0;

    // The fixed array must contain at least one entry and stay within capacity.
    if (!context->MemoryMapCount || context->MemoryMapCount > RPI_MEMORY_MAP_CAPACITY) {
        return 0;
    }

    // The published descriptor must describe this array, not stale storage.
    if (memory->Entries != (uintptr_t)context->MemoryMap ||
        memory->EntrySize != sizeof(struct VBootMemoryEntry) ||
        memory->NumberOfEntries != context->MemoryMapCount) {
        return 0;
    }

    // Platform preparation puts the memory ranges in address order, with no
    // overlaps. Check that this is still true before using the map to verify
    // reserved memory. Otherwise, the same bytes could be listed as both
    // reserved and free, allowing the kernel to reuse memory already in use.
    for (uint32_t i = 0; i < context->MemoryMapCount; i++) {
        const struct VBootMemoryEntry* entry = &context->MemoryMap[i];

        // Each range needs a nonempty end address that cannot wrap back to zero.
        if (!entry->Length || entry->Length > UINT64_MAX - entry->PhysicalBase) {
            return 0;
        }

        // Ordered, nonoverlapping ranges and known types make later walks reliable.
        if (entry->PhysicalBase < previousEnd ||
            (unsigned int)entry->Type > VBootMemoryType_Reclaim) {
            return 0;
        }
        
        // The kernel allocates free RAM in whole 4 KiB pages. Both the start
        // address and length must be multiples of the page size, so allocating
        // a page cannot also claim bytes outside this free range. The mask
        // checks the low address bits of both values at once.
        if (entry->Type == VBootMemoryType_Available &&
            ((entry->PhysicalBase | entry->Length) & RPI_PAGE_MASK)) {
            return 0;
        }
        previousEnd = entry->PhysicalBase + entry->Length;
    }
    return 1;
}

static int
__ContractRangesOverlap(
    uint64_t base,
    uint64_t length,
    uint64_t otherBase,
    uint64_t otherLength)
{
    // Both intervals have already passed the nonempty, nonwrapping ownership
    // check. Sharing reserved storage would still corrupt independent resources.
    return base < otherBase + otherLength && otherBase < base + length;
}

static int
__ContractPlatformValid(
    _In_ const struct RpiBootContext* context)
{
    uint64_t wrapperBase = (uintptr_t)__rpi_loader_start;
    uint64_t wrapperEnd = (uintptr_t)__rpi_loader_end;
    uint64_t stackBase = (uintptr_t)__rpi_boot_stack_bottom;
    uint64_t stackTop = (uintptr_t)__rpi_boot_stack_top;

    // Validate the map before using it to prove ownership of any live storage.
    if (!__ContractMemoryValid(context)) {
        return 0;
    }

    // Boot descriptors and the map itself must survive kernel allocation.
    if (!__ContractRangeReserved(context, (uintptr_t)context, sizeof(*context))) {
        return 0;
    }

    // The packaged image must include the complete wrapper before subtracting bounds.
    if (wrapperEnd <= wrapperBase || context->Kernel.ImageLength < wrapperEnd - wrapperBase) {
        return 0;
    }

    // Keep both loader storage and the appended source PE reserved.
    if (!__ContractRangeReserved(context, wrapperBase, context->Kernel.ImageLength)) {
        return 0;
    }

    // The active stack must be a nonempty interval inside the wrapper.
    if (stackBase < wrapperBase || stackTop > wrapperEnd || stackTop <= stackBase) {
        return 0;
    }

    // Stack bounds are page-aligned and its length must fit the VBoot field.
    if (((stackBase | stackTop) & RPI_PAGE_MASK) || stackTop - stackBase > UINT32_MAX) {
        return 0;
    }

    // The stack remains live until the final assembly handoff resets SP.
    if (!__ContractRangeReserved(context, stackBase, stackTop - stackBase)) {
        return 0;
    }
    return 1;
}

static int
__ContractKernelValid(
    _In_ const struct RpiBootContext* context)
{
    const struct VBoot* boot = &context->BootInformation;
    uint64_t            wrapperBase = (uintptr_t)__rpi_loader_start;

    // Loaded kernel storage must consist of complete physical pages.
    if (!boot->Kernel.Base || (boot->Kernel.Base & RPI_PAGE_MASK) ||
        (boot->Kernel.Length & RPI_PAGE_MASK)) {
        return 0;
    }

    // Prove the image has a valid end address before checking entry placement.
    if (!__ContractRangeReserved(context, boot->Kernel.Base, boot->Kernel.Length)) {
        return 0;
    }

    // The entry needs one aligned instruction and cannot precede the image.
    if ((boot->Kernel.EntryPoint & (RPI_ARM64_INSTRUCTION_SIZE - 1)) ||
        boot->Kernel.EntryPoint < boot->Kernel.Base ||
        boot->Kernel.Length < RPI_ARM64_INSTRUCTION_SIZE) {
        return 0;
    }

    // Subtract only after validating the lower bounds; the complete instruction must fit.
    if (boot->Kernel.EntryPoint - boot->Kernel.Base > boot->Kernel.Length - RPI_ARM64_INSTRUCTION_SIZE) {
        return 0;
    }

    // The expanded image must not overwrite the wrapper or its appended source file.
    if (boot->Kernel.Base < wrapperBase + context->Kernel.ImageLength) {
        return 0;
    }

    // The kernel must not overwrite the boot descriptors it is about to consume.
    if (__ContractRangesOverlap(boot->Kernel.Base, boot->Kernel.Length, (uintptr_t)context, sizeof(*context))) {
        return 0;
    }

    return 1;
}

static int
__ContractDeviceTreeValid(
    _In_ const struct RpiBootContext* context)
{
    const struct VBoot* boot = &context->BootInformation;
    uint64_t            wrapperBase = (uintptr_t)__rpi_loader_start;

    // The descriptor must refer to the same aligned DTB that discovery retained.
    if (!context->DtbPhysical || boot->DeviceTree.PhysicalBase != context->DtbPhysical ||
        (context->DtbPhysical & (RPI_DTB_ALIGNMENT - 1))) {
        return 0;
    }

    // Reject an incomplete header or a size beyond the loader's parsing budget.
    if (boot->DeviceTree.Length < RPI_DTB_HEADER_SIZE ||
        boot->DeviceTree.Length > RPI_DTB_MAX_SIZE) {
        return 0;
    }

    // Establish a valid, fully reserved range before comparing its end address.
    if (!__ContractRangeReserved(context, context->DtbPhysical, boot->DeviceTree.Length)) {
        return 0;
    }

    // The kernel and DTB need separate storage so neither can overwrite the other.
    if (__ContractRangesOverlap(boot->Kernel.Base, boot->Kernel.Length,
            context->DtbPhysical, boot->DeviceTree.Length)) {
        return 0;
    }

    // The wrapper still contains live code and bootstrap storage at handoff.
    if (__ContractRangesOverlap(wrapperBase, context->Kernel.ImageLength,
            context->DtbPhysical, boot->DeviceTree.Length)) {
        return 0;
    }

    // The context must remain intact while the kernel reads its boot descriptors.
    if (__ContractRangesOverlap((uintptr_t)context, sizeof(*context),
            context->DtbPhysical, boot->DeviceTree.Length)) {
        return 0;
    }
    return 1;
}

static int
__ContractResourceStorageValid(
    _In_ const struct RpiBootContext* context,
    _In_ uint64_t                     base,
    _In_ uint64_t                     length)
{
    const struct VBoot* boot = &context->BootInformation;
    uint64_t            wrapperBase = (uintptr_t)__rpi_loader_start;

    // Resource bytes must remain reserved and have a valid end before overlap checks.
    if (!__ContractRangeReserved(context, base, length)) {
        return 0;
    }

    // The loader still needs its wrapper and appended source image.
    if (__ContractRangesOverlap(wrapperBase, context->Kernel.ImageLength, base, length)) {
        return 0;
    }

    // Resource storage cannot also hold the loaded kernel.
    if (__ContractRangesOverlap(boot->Kernel.Base, boot->Kernel.Length, base, length)) {
        return 0;
    }

    // The DTB must remain separate from the bundle and expanded Phoenix image.
    if (__ContractRangesOverlap(context->DtbPhysical, boot->DeviceTree.Length, base, length)) {
        return 0;
    }

    // Preserve the context that contains all descriptors and memory-map entries.
    if (__ContractRangesOverlap((uintptr_t)context, sizeof(*context), base, length)) {
        return 0;
    }
    return 1;
}

static int
__ContractResourcesValid(
    _In_ const struct RpiBootContext* context)
{
    const struct VBoot* boot = &context->BootInformation;
    uint64_t ramdiskOffset;

    // The transport remains reserved while the ramdisk references its bytes.
    // Phoenix uses a separate, expanded PE allocation.
    if (context->ExternalPayloadBase || context->ExternalPayloadLength) {
        // A transport descriptor cannot contain a length without a base address.
        if (!context->ExternalPayloadBase) {
            return 0;
        }
        if (!__ContractResourceStorageValid(context,
                context->ExternalPayloadBase, context->ExternalPayloadLength)) {
            return 0;
        }
    }

    // Kernel-only boot is valid only when both resource descriptors are empty.
    if (!boot->Phoenix.Base && !boot->Phoenix.EntryPoint && !boot->Phoenix.Length) {
        if (!boot->Ramdisk.Data && !boot->Ramdisk.Length) {
            return 1;
        }
    }

    // Any published resource requires a transport and a complete Phoenix descriptor.
    if (!context->ExternalPayloadLength || !boot->Phoenix.Base || !boot->Phoenix.EntryPoint) {
        return 0;
    }

    // Phoenix's entry is an instruction address; its staging allocation uses whole pages.
    if ((boot->Phoenix.EntryPoint & (RPI_ARM64_INSTRUCTION_SIZE - 1)) ||
        ((boot->Phoenix.Base | boot->Phoenix.Length) & RPI_PAGE_MASK)) {
        return 0;
    }
    if (!__ContractResourceStorageValid(context, boot->Phoenix.Base, boot->Phoenix.Length)) {
        return 0;
    }

    // Expanding Phoenix must not overwrite the bundle that backs the ramdisk.
    if (__ContractRangesOverlap(boot->Phoenix.Base, boot->Phoenix.Length,
            context->ExternalPayloadBase, context->ExternalPayloadLength)) {
        return 0;
    }

    // Validate the lower bound before subtracting it to locate ramdisk bytes.
    if (!boot->Ramdisk.Length || boot->Ramdisk.Data < context->ExternalPayloadBase) {
        return 0;
    }
    ramdiskOffset = boot->Ramdisk.Data - context->ExternalPayloadBase;

    // The complete ramdisk must fit inside the retained transport, without wrapping.
    if (ramdiskOffset > context->ExternalPayloadLength ||
        boot->Ramdisk.Length > context->ExternalPayloadLength - ramdiskOffset) {
        return 0;
    }
    return 1;
}

enum RpiBootStatus
RpiBuildContract(
    struct RpiBootContext* context)
{
    struct VBoot* boot;
    uint64_t      stackBase = (uintptr_t)__rpi_boot_stack_bottom;
    uint64_t      stackTop = (uintptr_t)__rpi_boot_stack_top;

    if (!context) {
        return RpiBootInvalidPlatform;
    }
    
    boot = &context->BootInformation;
    boot->Magic = 0;
    boot->Version = 0;

    if (!__ContractPlatformValid(context)) {
        return RpiBootInvalidPlatform;
    }
    if (!__ContractKernelValid(context)) {
        return RpiBootInvalidPayload;
    }
    if (!__ContractDeviceTreeValid(context)) {
        return RpiBootInvalidPlatform;
    }
    if (!__ContractResourcesValid(context)) {
        return RpiBootInvalidPayload;
    }

    // Reuse the live bootstrap stack without clearing it. Only the no-return
    // assembly transfer may reset SP: doing so here would destroy this call's
    // return path. Keep the whole wrapper reserved after the shallow VBoot copy;
    // the map and contract still reside there, and the stack remains active.
    boot->Stack.Base = stackBase;
    boot->Stack.Length = (uint32_t)(stackTop - stackBase);
    boot->Firmware = VBootFirmware_Native;
    boot->ConfigurationTableCount = 0;
    boot->ConfigurationEntrySize = 0;
    boot->ConfigurationTable = 0;
    boot->Video = (struct VBootVideo){0};

    // These markers describe a complete descriptor, not a CPU entry state.
    // Cache visibility and EL normalization remain the transfer stage's job.
    boot->Version = VBOOT_VERSION;
    boot->Magic = VBOOT_MAGIC;
    return RpiBootOk;
}
