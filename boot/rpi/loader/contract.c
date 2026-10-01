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
    uint64_t                    base,
    uint64_t                    length)
{
    uint64_t end;

    if (!length || length > UINT64_MAX - base) {
        return 0;
    }
    end = base + length;

    // Absence from available RAM is not ownership: a missing map interval
    // could be MMIO or undiscovered memory. Require continuous reserved storage
    // so every byte stays protected when the kernel starts allocating pages.
    for (uint32_t i = 0; i < context->MemoryMapCount; i++) {
        const struct VBootMemoryEntry* entry = &context->MemoryMap[i];
        uint64_t entryEnd = entry->PhysicalBase + entry->Length;

        if (entryEnd <= base) {
            continue;
        }
        if (entry->PhysicalBase > base || entry->Type != VBootMemoryType_Reserved) {
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
    uint64_t previousEnd = 0;

    if (!context->MemoryMapCount || context->MemoryMapCount > RPI_MEMORY_MAP_CAPACITY ||
        memory->Entries != (uintptr_t)context->MemoryMap ||
        memory->EntrySize != sizeof(struct VBootMemoryEntry) ||
        memory->NumberOfEntries != context->MemoryMapCount) {
        return 0;
    }

    // The ownership walk relies on the normalized ordering from platform
    // preparation. Verify that invariant before interpreting any reservations;
    // otherwise an overlapping available entry could undo their protection.
    for (uint32_t i = 0; i < context->MemoryMapCount; i++) {
        const struct VBootMemoryEntry* entry = &context->MemoryMap[i];

        if (!entry->Length || entry->Length > UINT64_MAX - entry->PhysicalBase ||
            entry->PhysicalBase < previousEnd ||
            (unsigned int)entry->Type > VBootMemoryType_Reclaim) {
            return 0;
        }
        if (entry->Type == VBootMemoryType_Available &&
            ((entry->PhysicalBase | entry->Length) & 4095)) {
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

enum RpiBootStatus
RpiBuildContract(
    struct RpiBootContext* context)
{
    struct VBoot* boot;
    uint64_t wrapperBase = (uintptr_t)__rpi_loader_start;
    uint64_t wrapperEnd = (uintptr_t)__rpi_loader_end;
    uint64_t stackBase = (uintptr_t)__rpi_boot_stack_bottom;
    uint64_t stackTop = (uintptr_t)__rpi_boot_stack_top;

    if (!context) {
        return RpiBootInvalidPlatform;
    }
    boot = &context->BootInformation;
    boot->Magic = 0;
    boot->Version = 0;

    if (!__ContractMemoryValid(context) ||
        !__ContractRangeReserved(context, (uintptr_t)context, sizeof(*context)) ||
        wrapperEnd <= wrapperBase || context->Kernel.ImageLength < wrapperEnd - wrapperBase ||
        !__ContractRangeReserved(context, wrapperBase, context->Kernel.ImageLength) ||
        stackBase < wrapperBase || stackTop > wrapperEnd || stackTop <= stackBase ||
        ((stackBase | stackTop) & 4095) || stackTop - stackBase > UINT32_MAX ||
        !__ContractRangeReserved(context, stackBase, stackTop - stackBase)) {
        return RpiBootInvalidPlatform;
    }
    if (!boot->Kernel.Base || (boot->Kernel.Base & 4095) ||
        (boot->Kernel.Length & 4095) ||
        !__ContractRangeReserved(context, boot->Kernel.Base, boot->Kernel.Length) ||
        (boot->Kernel.EntryPoint & 3) || boot->Kernel.EntryPoint < boot->Kernel.Base ||
        boot->Kernel.Length < 4 ||
        boot->Kernel.EntryPoint - boot->Kernel.Base > boot->Kernel.Length - 4 ||
        boot->Kernel.Base < wrapperBase + context->Kernel.ImageLength ||
        __ContractRangesOverlap(boot->Kernel.Base, boot->Kernel.Length, (uintptr_t)context, sizeof(*context))) {
        return RpiBootInvalidPayload;
    }
    if (!context->DtbPhysical || boot->DeviceTree.PhysicalBase != context->DtbPhysical ||
        (context->DtbPhysical & 7) || boot->DeviceTree.Length < 40 ||
        boot->DeviceTree.Length > RPI_DTB_MAX_SIZE ||
        !__ContractRangeReserved(context, context->DtbPhysical, boot->DeviceTree.Length) ||
        __ContractRangesOverlap(boot->Kernel.Base, boot->Kernel.Length,
            context->DtbPhysical, boot->DeviceTree.Length) ||
        __ContractRangesOverlap(wrapperBase, context->Kernel.ImageLength,
            context->DtbPhysical, boot->DeviceTree.Length) ||
        __ContractRangesOverlap((uintptr_t)context, sizeof(*context),
            context->DtbPhysical, boot->DeviceTree.Length)) {
        return RpiBootInvalidPlatform;
    }

    // A firmware initrd interval is only a transport for our future external
    // payload format. Retain its ownership, but do not present its manifest or
    // an unexpanded Phoenix PE as a usable ramdisk/module to the kernel.
    if (context->ExternalPayloadBase || context->ExternalPayloadLength) {
        if (!context->ExternalPayloadBase ||
            !__ContractRangeReserved(context, context->ExternalPayloadBase, context->ExternalPayloadLength) ||
            __ContractRangesOverlap(wrapperBase, context->Kernel.ImageLength,
                context->ExternalPayloadBase, context->ExternalPayloadLength) ||
            __ContractRangesOverlap(boot->Kernel.Base, boot->Kernel.Length,
                context->ExternalPayloadBase, context->ExternalPayloadLength) ||
            __ContractRangesOverlap(context->DtbPhysical, boot->DeviceTree.Length,
                context->ExternalPayloadBase, context->ExternalPayloadLength) ||
            __ContractRangesOverlap((uintptr_t)context, sizeof(*context),
                context->ExternalPayloadBase, context->ExternalPayloadLength)) {
            return RpiBootInvalidPayload;
        }
    }
    if (boot->Phoenix.Base || boot->Phoenix.EntryPoint || boot->Phoenix.Length ||
        boot->Ramdisk.Data || boot->Ramdisk.Length) {
        return RpiBootUnsupported;
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
    boot->Phoenix = (struct VBootModule){0};
    boot->Ramdisk = (struct VBootRamdisk){0};

    // These markers describe a complete descriptor, not a CPU entry state.
    // Cache visibility and EL normalization remain the transfer stage's job.
    boot->Version = VBOOT_VERSION;
    boot->Magic = VBOOT_MAGIC;
    return RpiBootOk;
}
