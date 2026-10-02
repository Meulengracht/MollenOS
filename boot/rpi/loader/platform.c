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
#include "private.h"

extern unsigned char __rpi_loader_start[];
extern unsigned char __rpi_loader_end[];

static enum RpiBootStatus
__PlatformError(
    oserr_t status)
{
    if (status == OS_ENOTSUPPORTED) {
        return RpiBootUnsupported;
    }
    if (status == OS_EBUFFER || status == OS_EOOM) {
        return RpiBootNoMemory;
    }
    return RpiBootInvalidPlatform;
}

static oserr_t
__PlatformFinishMemoryMap(
    struct RpiBootContext* context)
{
    uint32_t output = 0;
    int available = 0;

    // The initial kernel allocator consumes 4 KiB pages. Round available RAM
    // inward so a sub-page DTB reservation cannot share an allocated page.
    // Omitted fragments remain unavailable; reserved ranges keep exact bounds
    // for later ownership diagnostics rather than silently expanding into MMIO.
    for (uint32_t i = 0; i < context->MemoryMapCount; i++) {
        struct VBootMemoryEntry entry = context->MemoryMap[i];
        if (entry.Type == VBootMemoryType_Available) {
            uint64_t end = (entry.PhysicalBase + entry.Length) & ~4095ULL;
            if (entry.PhysicalBase > UINT64_MAX - 4095) {
                continue;
            }
            entry.PhysicalBase = (entry.PhysicalBase + 4095) & ~4095ULL;
            if (end <= entry.PhysicalBase) {
                continue;
            }
            entry.Length = end - entry.PhysicalBase;
            available = 1;
        }
        context->MemoryMap[output++] = entry;
    }
    context->MemoryMapCount = output;
    return available ? OS_EOK : OS_EOOM;
}

static int
__PlatformOverlap(
    uint64_t first,
    uint64_t firstLength,
    uint64_t second,
    uint64_t secondLength)
{
    // Callers establish non-wrapping, nonempty ranges before comparing them.
    return first < second + secondLength && second < first + firstLength;
}

enum RpiBootStatus
RpiPlatformPrepare(
    struct RpiBootContext* context)
{ 
    const uint8_t* dtb;
    uint64_t wrapperBase = (uintptr_t)__rpi_loader_start;
    uint64_t wrapperLength = (uintptr_t)__rpi_loader_end - wrapperBase;
    uint64_t imageEnd;
    uint32_t dtbLength;
    oserr_t status;

    if (!context) {
        return RpiBootInvalidPlatform;
    }
    
    context->MemoryMapCount = 0;
    context->BootInformation.Memory = (struct VBootMemory){0};
    context->BootInformation.DeviceTree = (struct VBootDeviceTree){0};
    context->ExternalPayloadBase = 0;
    context->ExternalPayloadLength = 0;
    
    if ((context->Board != 4 && context->Board != 5) || !context->DtbPhysical ||
        (context->DtbPhysical & 7) || context->DtbPhysical > UINTPTR_MAX - 40 ||
        context->Kernel.ImageLength < wrapperLength ||
        context->Kernel.ImageLength - wrapperLength > RPI_PAYLOAD_MAX_SIZE ||
        context->Kernel.ImageLength > UINT64_MAX - wrapperBase) {
        return RpiBootInvalidPlatform;
    }
    
    imageEnd = wrapperBase + context->Kernel.ImageLength;
    dtb = (const uint8_t*)context->DtbPhysical;

    // Firmware supplies no independently measured length. Trust only the initial
    // header read, then cap and bound its extent before the shared parser sees
    // it. Reading this header does not require a second structure traversal.
    if (__ReadBe32(dtb) != 0xd00dfeed) {
        return RpiBootInvalidPlatform;
    }
    dtbLength = __ReadBe32(dtb + 4);
    if (dtbLength < 40 || dtbLength > RPI_DTB_MAX_SIZE ||
        dtbLength > UINTPTR_MAX - context->DtbPhysical ||
        __PlatformOverlap(context->DtbPhysical, dtbLength, wrapperBase, context->Kernel.ImageLength)) {
        return RpiBootInvalidPlatform;
    }

    status = DeviceTreeParseEarlyPlatform(
        dtb,
        dtbLength,
        context
    );
    if (status != OS_EOK) {
        goto failed;
    }
    
    if (context->ExternalPayloadLength) {
        if (__PlatformOverlap(context->ExternalPayloadBase, context->ExternalPayloadLength, 0, imageEnd) ||
            __PlatformOverlap(context->ExternalPayloadBase, context->ExternalPayloadLength,
                context->DtbPhysical, dtbLength)) {
            status = OS_EINVALPARAMS;
            goto failed;
        }
        status = DeviceTreeReserveMemory(context, context->ExternalPayloadBase, context->ExternalPayloadLength);
        if (status != OS_EOK) {
            goto failed;
        }
    }

    // Keep low firmware/stub storage, the entire file, and wrapper BSS/stack.
    // The linker includes bootstrap storage in the file span. Firmware's
    // release code remains live while secondary CPUs wait outside our wrapper.
    status = DeviceTreeReserveMemory(context, 0, imageEnd);
    if (status != OS_EOK) {
        goto failed;
    }
    status = DeviceTreeReserveMemory(context, context->DtbPhysical, dtbLength);
    if (status != OS_EOK) {
        goto failed;
    }

    status = __PlatformFinishMemoryMap(context);
    if (status != OS_EOK) {
        goto failed;
    }

    // Only complete discovery publishes descriptors. Their backing array is in
    // the reserved context, not this stack. These describe physical storage;
    // the eventual kernel handoff must establish its own mappings and lifetime.
    context->BootInformation.Memory.Entries = (uintptr_t)context->MemoryMap;
    context->BootInformation.Memory.EntrySize = sizeof(struct VBootMemoryEntry);
    context->BootInformation.Memory.NumberOfEntries = context->MemoryMapCount;
    context->BootInformation.DeviceTree.PhysicalBase = context->DtbPhysical;
    context->BootInformation.DeviceTree.Length = dtbLength;
    
    // config.txt enables firmware serial output. Preserve that setup: changing
    // UART divisors/pinmux without clock discovery could destroy the only early
    // diagnostics, especially on Pi 5 where RP1 is not the debug UART.
    return RpiBootOk;

failed:
    context->MemoryMapCount = 0;
    context->ExternalPayloadBase = 0;
    context->ExternalPayloadLength = 0;
    return __PlatformError(status);
}
