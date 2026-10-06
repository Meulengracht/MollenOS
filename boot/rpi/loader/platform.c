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
#include <fdt/reader.h>

extern unsigned char __rpi_loader_start[];
extern unsigned char __rpi_loader_end[];

static enum RpiBootStatus
__PlatformError(
    _In_ oserr_t status)
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
    _In_ struct RpiBootContext* context)
{
    uint32_t output = 0;
    int      available = 0;

    // The initial kernel allocator consumes 4 KiB pages. Round available RAM
    // inward so a sub-page DTB reservation cannot share an allocated page.
    // Omitted fragments remain unavailable; reserved ranges keep exact bounds
    // for later ownership diagnostics rather than silently expanding into MMIO.
    for (uint32_t i = 0; i < context->MemoryMapCount; i++) {
        struct VBootMemoryEntry entry = context->MemoryMap[i];
        if (entry.Type == VBootMemoryType_Available) {
            uint64_t end = (entry.PhysicalBase + entry.Length) & ~RPI_PAGE_MASK;
            if (entry.PhysicalBase > UINT64_MAX - RPI_PAGE_MASK) {
                continue;
            }
            
            entry.PhysicalBase = (entry.PhysicalBase + RPI_PAGE_MASK) & ~RPI_PAGE_MASK;
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
    _In_ uint64_t first,
    _In_ uint64_t firstLength,
    _In_ uint64_t second,
    _In_ uint64_t secondLength)
{
    // Callers establish non-wrapping, nonempty ranges before comparing them.
    return first < second + secondLength && second < first + firstLength;
}

static int
__PlatformValidateInput(
    _In_ const struct RpiBootContext* context,
    _In_ uint64_t                     wrapperBase,
    _In_ uint64_t                     wrapperLength)
{
    // Board-specific DT bindings are defined only for Pi 4 and Pi 5.
    if (context->Board != 4 && context->Board != 5) {
        return 0;
    }
    
    // The initial firmware header read needs an aligned, nonwrapping address.
    if (!context->DtbPhysical ||
        (context->DtbPhysical & (RPI_DTB_ALIGNMENT - 1)) ||
        context->DtbPhysical > UINTPTR_MAX - sizeof(struct FDTHeader)) {
        return 0;
    }
    
    // Bound the appended file before computing its exclusive physical end.
    if (context->Kernel.ImageLength < wrapperLength ||
        context->Kernel.ImageLength - wrapperLength > RPI_PAYLOAD_MAX_SIZE ||
        context->Kernel.ImageLength > UINT64_MAX - wrapperBase) {
        return 0;
    }
    return 1;
}

static int
__PlatformReadDtbLength(
    _In_  const struct RpiBootContext* context,
    _In_  uint64_t                     wrapperBase,
    _Out_ uint32_t*                    dtbLength)
{
    const uint8_t* dtb = (const uint8_t*)context->DtbPhysical;
    uint32_t       magic;
    uint32_t       length;

    // Firmware supplies no independently measured length. Trust only the initial
    // header read, then cap and bound its extent before the shared parser sees it.
    magic = FdtReadBe32(dtb);
    if (magic != RPI_DTB_MAGIC) {
        return 0;
    }
    
    length = FdtReadBe32(dtb + offsetof(struct FDTHeader, TotalSize));
    // The bounded blob must include the header and have a representable end.
    if (length < sizeof(struct FDTHeader) || length > RPI_DTB_MAX_SIZE ||
        length > UINTPTR_MAX - context->DtbPhysical) {
        return 0;
    }
    
    // Discovery must not read a DTB that aliases loader storage or its payload.
    if (__PlatformOverlap(context->DtbPhysical, length,
            wrapperBase, context->Kernel.ImageLength)) {
        return 0;
    }

    *dtbLength = length;
    return 1;
}

enum RpiBootStatus
RpiPlatformPrepare(
    struct RpiBootContext* context)
{ 
    const uint8_t* dtb;
    uint64_t       wrapperBase = (uintptr_t)__rpi_loader_start;
    uint64_t       wrapperLength = (uintptr_t)__rpi_loader_end - wrapperBase;
    uint64_t       imageEnd;
    uint32_t       dtbLength;
    int            dtbValid;
    oserr_t        status;

    context->MemoryMapCount = 0;
    context->BootInformation.Memory = (struct VBootMemory){0};
    context->BootInformation.DeviceTree = (struct VBootDeviceTree){0};
    context->ExternalPayloadBase = 0;
    context->ExternalPayloadLength = 0;
    
    if (!__PlatformValidateInput(context, wrapperBase, wrapperLength)) {
        return RpiBootInvalidPlatform;
    }
    
    imageEnd = wrapperBase + context->Kernel.ImageLength;
    dtb = (const uint8_t*)context->DtbPhysical;

    // Read and validate the firmware extent before the shared parser uses it.
    dtbValid = __PlatformReadDtbLength(context, wrapperBase, &dtbLength);
    if (!dtbValid) {
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
        // The retained bundle must not overwrite low firmware, the wrapper or DTB.
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

    status = DeviceTreeResolveReservations(context, dtbLength);
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
    
    // config.txt enables firmware serial output. Preserve that setup: changing
    // UART divisors/pinmux without clock discovery could destroy the only early
    // diagnostics, especially on Pi 5 where RP1 is not the debug UART.
    return RpiBootOk;

failed:
    context->BootInformation.DeviceTree = (struct VBootDeviceTree){0};
    context->MemoryMapCount = 0;
    context->ExternalPayloadBase = 0;
    context->ExternalPayloadLength = 0;
    return __PlatformError(status);
}
