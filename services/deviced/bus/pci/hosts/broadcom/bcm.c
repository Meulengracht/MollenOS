/**
 * MollenOS
 *
 * Copyright (C) Philip Meulengracht
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

//#define __TRACE

#include <bus/pci/host-private.h>
#include <bus/pci/dma.h>
#include <bus/pci/registers.h>
#include <bus/pci/hosts/broadcom/bcm.h>
#include <ddk/utils.h>
#include <string.h>
#include <time.h>

#define BCM_OUTBOUND_TARGET      0x400C
#define BCM_LINK_STATUS          0x4068
#define BCM_OUTBOUND_BASE_LIMIT  0x4070
#define BCM_OUTBOUND_BASE_HIGH   0x4080
#define BCM_LINK_UP              0x30
#define BCM_ROOT_PORT            0x80
#define BCM_MEGABYTE             0x100000ULL

void
BcmPciDelay(
    _In_ long milliseconds)
{
    // Hardware status changes take time, so callers pause between checks
    // instead of repeatedly reading the same register in a busy loop.
    struct timespec interval = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (milliseconds % 1000) * 1000000
    };
    int status;

    status = thrd_sleep(&interval, NULL);
    if (status != 0) {
        WARNING("BcmPciDelay sleep of %li ms did not complete: %i", milliseconds, status);
    }
}

oserr_t
BcmPciMapRegisters(
    _Out_ DeviceIo_t* io,
    _In_ uint64_t     base,
    _In_ uint64_t     length)
{
    oserr_t status;
    oserr_t cleanup;

    TRACE("BcmPciMapRegisters(base=0x%llx, length=0x%llx)",
        (unsigned long long)base, (unsigned long long)length);

    // Check the range before converting it to the smaller types used by I/O.
    if (length == 0) {
        ERROR("BcmPciMapRegisters empty register range");
        return OS_ENOTSUPPORTED;
    }
    if (base > UINTPTR_MAX || length > SIZE_MAX) {
        ERROR("BcmPciMapRegisters range exceeds pointer width");
        return OS_ENOTSUPPORTED;
    }
    if (length - 1 > UINTPTR_MAX - base) {
        ERROR("BcmPciMapRegisters register range wraps the CPU address space");
        return OS_ENOTSUPPORTED;
    }
    
    status = CreateDeviceMemoryIo(io, (uintptr_t)base, (size_t)length);
    if (status != OS_EOK) {
        ERROR("BcmPciMapRegisters CreateDeviceMemoryIo failed: %u", status);
        return status;
    }
   
    status = AcquireDeviceIo(io);
    if (status != OS_EOK) {
        ERROR("BcmPciMapRegisters AcquireDeviceIo failed: %u", status);
        cleanup = DestroyDeviceIo(io);
        if (cleanup != OS_EOK) {
            ERROR("BcmPciMapRegisters DestroyDeviceIo failed during cleanup: %u", cleanup);
        }
        return status;
    }
    return OS_EOK;
}

oserr_t
BcmPciRescal(
    _In_ const struct FdtPciDependencies* dependencies,
    _In_ int                              reuseCompleted)
{
    DeviceIo_t   reset;
    oserr_t      status;
    oserr_t      cleanupStatus;
    uint32_t     value;
    unsigned int attempt;

    TRACE("BcmPciRescal(base=0x%llx, reuse=%i)",
        (unsigned long long)dependencies->ResetBase, reuseCompleted);

    // No reset range means firmware has not provided the optional calibration block.
    if (dependencies->ResetLength == 0) {
        return OS_EOK;
    }

    status = BcmPciMapRegisters(&reset, dependencies->ResetBase, dependencies->ResetLength);
    if (status != OS_EOK) {
        ERROR("BcmPciRescal BcmPciMapRegisters failed: %u", status);
        return status;
    }

    // Pi 5 ports share this calibration block. Reuse its completed result so
    // starting another port does not interrupt a port that is already working.
    value = (uint32_t)ReadDeviceIo(&reset, 8, 4);
    if (reuseCompleted && value != UINT32_MAX && (value & 1)) {
        TRACE("BcmPciRescal reusing completed shared calibration");
        goto cleanup;
    }

    // Read the enable bit back because a successful write does not prove that
    // the hardware accepted the request.
    value = (uint32_t)ReadDeviceIo(&reset, 0, 4);
    status = WriteDeviceIo(&reset, 0, value | 1, 4);
    if (status != OS_EOK) {
        ERROR("BcmPciRescal failed to start calibration: %u", status);
    } else {
        value = (uint32_t)ReadDeviceIo(&reset, 0, 4);
        if (!(value & 1)) {
            ERROR("BcmPciRescal start bit did not latch, read back 0x%x", value);
            status = OS_EUNKNOWN;
        }
    }

    // The start write may have reached hardware even if it failed. Bound the
    // wait and clear the request before releasing the mapping.
    for (attempt = 0; attempt < 10; attempt++) {
        value = (uint32_t)ReadDeviceIo(&reset, 8, 4);
        if (value != UINT32_MAX && (value & 1)) {
            break;
        }
        BcmPciDelay(1);
    }

    if (attempt == 10) {
        ERROR("BcmPciRescal timed out waiting for calibration, status=0x%x", value);
        status = OS_EUNKNOWN;
    }

    // Clear the request even after a timeout, and keep the earlier error if
    // clearing it also fails.
    value = (uint32_t)ReadDeviceIo(&reset, 0, 4);
    cleanupStatus = WriteDeviceIo(&reset, 0, value & ~1U, 4);
    if (cleanupStatus != OS_EOK) {
        ERROR("BcmPciRescal failed to clear calibration request: %u", cleanupStatus);
        if (status == OS_EOK) {
            status = OS_EUNKNOWN;
        }
    }

cleanup:
    cleanupStatus = ReleaseDeviceIo(&reset);
    if (cleanupStatus != OS_EOK) {
        ERROR("BcmPciRescal failed to release calibration mapping: %u", cleanupStatus);
    }
    cleanupStatus = DestroyDeviceIo(&reset);
    if (cleanupStatus != OS_EOK) {
        ERROR("BcmPciRescal failed to destroy calibration mapping: %u", cleanupStatus);
    }
    return status;
}

oserr_t
BcmPciUpdate(
    _In_ PciHost_t* bus,
    _In_ size_t     reg,
    _In_ uint32_t   mask,
    _In_ uint32_t   value)
{
    // Change only the requested bits so unrelated controller settings survive.
    uint32_t previous = (uint32_t)ReadDeviceIo(&bus->IoSpace, reg, 4);
    oserr_t  status;

    status = WriteDeviceIo(&bus->IoSpace, reg, (previous & ~mask) | value, 4);
    if (status != OS_EOK) {
        ERROR("BcmPciUpdate register 0x%x mask 0x%x value 0x%x failed: %u",
            (unsigned)reg, mask, value, status);
        return status;
    }
    return OS_EOK;
}

int
BcmPciLinkUp(
    _In_ PciHost_t* bus)
{
    // An all-ones value means the register could not be read, not that the link is up.
    uint32_t status = (uint32_t)ReadDeviceIo(&bus->IoSpace, BCM_LINK_STATUS, 4);
    return status != UINT32_MAX &&
            (status & (BCM_LINK_UP | BCM_ROOT_PORT)) == (BCM_LINK_UP | BCM_ROOT_PORT);
}

static void
__BcmDestroy(
    _InOut_ PciHost_t* bus)
{
    // The PCI core calls this to release the controller context it was given.
    BcmPciDestroy(bus, bus->OpContext);
}

static int
__BcmConfigOffset(
    _In_ PciHost_t*   bus,
    _In_ unsigned int number,
    _In_ unsigned int slot,
    _In_ unsigned int function,
    _In_ size_t       reg,
    _In_ size_t       width,
    _Out_ size_t*     offset)
{
    struct BcmPciHost* controller = bus->OpContext;
    oserr_t            status;

    // Device settings are reached through shared registers; do not use them
    // until controller setup has finished.
    if (!controller->Ready) {
        return 0;
    }
    // Firmware assigns which bus numbers this controller can access.
    if (number < (unsigned int)bus->Identification.BusStart ||
        number > (unsigned int)bus->Identification.BusEnd) {
        return 0;
    }
    // Each bus has 32 device slots, and each device has up to eight functions.
    if (slot >= 32 || function >= 8) {
        return 0;
    }
    // This controller accepts byte, two-byte, and four-byte reads and writes.
    if (width != 1 && width != 2 && width != 4) {
        return 0;
    }
    // Keep the complete access inside the 4 KiB area that holds device settings.
    if (reg >= 4096 || width > 4096 - reg) {
        return 0;
    }
    // The requested address must match the size of the access.
    if ((reg & (width - 1)) != 0) {
        return 0;
    }
    
    if (number == (unsigned int)bus->Identification.BusStart) {
        // The first bus is the controller itself, not a bus of attached devices.
        if (slot != 0 || function != 0) {
            return 0;
        }
        *offset = reg;
        return 1;
    }
    
    // The first downstream bus has only the directly attached device.
    if (number == (unsigned int)bus->Identification.BusStart + 1 && slot != 0) {
        return 0;
    }

    // Avoid a config-space access while the cable or device link is unavailable.
    if (!BcmPciLinkUp(bus)) {
        return 0;
    }
    
    status = WriteDeviceIo(
        &bus->IoSpace,
        controller->Variant->ConfigIndex,
        (number << 20) | (slot << 15) | (function << 12),
        4
    );
    if (status != OS_EOK) {
        ERROR("__BcmConfigOffset WriteDeviceIo register 0x%x failed: %u",
            (unsigned)(controller->Variant->ConfigIndex), status);
        return 0;
    }
    
    // Finish selecting the address before reading or writing the data window.
    (void)ReadDeviceIo(&bus->IoSpace, controller->Variant->ConfigIndex, 4);

    if (!BcmPciLinkUp(bus)) {
        return 0;
    }
    
    *offset = controller->Variant->ConfigData + reg;
    return 1;
}

static size_t
__BcmRead(
    _In_ PciHost_t*   bus,
    _In_ unsigned int number,
    _In_ unsigned int slot,
    _In_ unsigned int function,
    _In_ size_t       reg,
    _In_ size_t       width)
{
    struct BcmPciHost* controller = bus->OpContext;
    size_t             offset;
    size_t             value = (size_t)-1;
    int                status;

    // The controller uses shared registers to select a device and read its
    // settings, so keep the selection and read together under one lock.
    status = mtx_lock(&controller->ConfigLock);
    if (status != thrd_success) {
        ERROR("__BcmRead failed to lock configuration: %i", status);
        return value;
    }

    if (__BcmConfigOffset(bus, number, slot, function, reg, width, &offset)) {
        value = ReadDeviceIo(&bus->IoSpace, offset, width);
    }

    status = mtx_unlock(&controller->ConfigLock);
    if (status != thrd_success) {
        ERROR("__BcmRead failed to unlock configuration: %i", status);
    }
    return value;
}

static void
__BcmWrite(
    _In_ PciHost_t*   bus,
    _In_ unsigned int number,
    _In_ unsigned int slot,
    _In_ unsigned int function,
    _In_ size_t       reg,
    _In_ size_t       value,
    _In_ size_t       width)
{
    struct BcmPciHost* controller = bus->OpContext;
    size_t             offset;
    int                lockStatus;
    oserr_t            status;

    // Keep selecting the device and writing its settings together so another
    // caller cannot change the selected device midway through the operation.
    lockStatus = mtx_lock(&controller->ConfigLock);
    if (lockStatus != thrd_success) {
        ERROR("__BcmWrite failed to lock configuration: %i", lockStatus);
        return;
    }
    
    if (__BcmConfigOffset(bus, number, slot, function, reg, width, &offset)) {
        status = WriteDeviceIo(&bus->IoSpace, offset, value, width);
        if (status != OS_EOK) {
            ERROR("__BcmWrite %u:%u.%u register 0x%x failed: %u",
                number, slot, function, (unsigned)reg, status);
        }
    }
    lockStatus = mtx_unlock(&controller->ConfigLock);
    if (lockStatus != thrd_success) {
        ERROR("__BcmWrite failed to unlock configuration: %i", lockStatus);
    }
}

static oserr_t
__BcmTranslate(
    _In_  PciHost_t* bus,
    _In_  uint32_t   space,
    _In_  uint64_t   address,
    _In_  uint64_t   length,
    _Out_ uint64_t*  physical)
{
    // Firmware describes how device addresses map to machine addresses, so use
    // the common firmware translator rather than duplicating those rules here.
    struct BcmPciHost* controller = bus->OpContext;
    return FdtTranslatePciAddress(
        &controller->Firmware,
        (enum FdtPciSpace)space,
        address,
        length,
        physical
    );
}

static oserr_t
__BcmInterrupt(
    _In_  PciHost_t*    bus,
    _In_  unsigned int  number,
    _In_  unsigned int  slot,
    _In_  unsigned int  function,
    _In_  unsigned int  pin,
    _Out_ int*          line,
    _Out_ unsigned int* flags)
{
    // Firmware may route each device interrupt differently; ask its interrupt map.
    struct BcmPciHost* controller = bus->OpContext;
    return FdtResolvePciInterrupt(
        &controller->Firmware,
        number,
        slot,
        function,
        pin,
        line,
        flags
    );
}

static oserr_t
__BcmGetDmaDescription(
    _In_  PciHost_t*                bus,
    _Out_ struct PciDmaDescription* description)
{
    const struct BcmPciHost* controller = bus->OpContext;
    oserr_t                  status;

    // Configuration access alone is not evidence of a usable inbound mapping.
    // Stop and failed setup clear this separate record before removing hardware.
    if (!controller->Ready || !controller->InboundConfigured) {
        WARNING("__BcmGetDmaDescription controller is not ready for DMA");
        return OS_EBUSY;
    }

    // Setup used these retained firmware windows. Reuse the shared RAM filter
    // and keep their exact lengths, including when hardware rounded a size up.
    status = FdtPciDmaMap(&controller->Firmware, &description->Map);
    if (status != OS_EOK) {
        ERROR("__BcmGetDmaDescription FdtPciDmaMap failed: %u", status);
        return status;
    }
    
    // The initial policy requires software to arrange visibility. Neither a
    // programmed address window nor a firmware flag proves that caches are shared.
    description->CachePolicy = DmDmaCacheNonCoherent;
    return OS_EOK;
}

const struct PciHostOperations g_pciBcmOperations = {
    .GetDmaDescription = __BcmGetDmaDescription,
    .Read = __BcmRead,
    .Write = __BcmWrite,
    .Translate = __BcmTranslate,
    .ResolveInterrupt = __BcmInterrupt,
    .Destroy = __BcmDestroy
};

static oserr_t
__BcmValidateOutbound(
    _In_ const struct FdtPciHost* firmware)
{
    const struct FdtPciWindow* window;
    uint32_t                   index;
    uint32_t                   previous;
    uint64_t                   length;
    uint64_t                   previousLength;

    TRACE("__BcmValidateOutbound(segment=%u, windows=%u)",
        firmware->Segment, firmware->WindowCount);

    // The controller can handle at most four address ranges, and it needs at
    // least one bus number after its own number for attached devices.
    if (firmware->WindowCount == 0 || firmware->WindowCount > 4) {
        WARNING("__BcmValidateOutbound requires one to four outbound windows");
        return OS_ENOTSUPPORTED;
    }
    if (firmware->BusStart >= firmware->BusEnd) {
        WARNING("__BcmValidateOutbound bus range has no downstream bus");
        return OS_ENOTSUPPORTED;
    }
    
    for (index = 0; index < firmware->WindowCount; index++) {
        window = &firmware->Windows[index];
        
        // Only memory ranges are supported; an empty range has no addresses to map.
        if (window->Space != FdtPciSpaceMemory32 && window->Space != FdtPciSpaceMemory64) {
            WARNING("__BcmValidateOutbound outbound window is not memory space");
            return OS_ENOTSUPPORTED;
        }
        if (window->Length == 0) {
            WARNING("__BcmValidateOutbound outbound window is empty");
            return OS_ENOTSUPPORTED;
        }
        
        // Hardware maps whole 1 MiB blocks, so both starting addresses must
        // begin at a block boundary.
        if ((window->PhysicalBase | window->BusBase) & (BCM_MEGABYTE - 1)) {
            WARNING("__BcmValidateOutbound outbound addresses must be MiB-aligned");
            return OS_ENOTSUPPORTED;
        }
        
        // The controller cannot reach machine addresses above its 40-bit limit.
        if (window->PhysicalBase >= (1ULL << 40)) {
            WARNING("__BcmValidateOutbound outbound CPU base exceeds 40 bits");
            return OS_ENOTSUPPORTED;
        }
        if (window->Length > (1ULL << 40) - window->PhysicalBase) {
            WARNING("__BcmValidateOutbound outbound CPU extent exceeds 40 bits");
            return OS_ENOTSUPPORTED;
        }
        
        // Make sure the last device-visible address does not wrap around.
        if (window->Length - 1 > UINT64_MAX - window->BusBase) {
            WARNING("__BcmValidateOutbound outbound PCI extent wraps");
            return OS_ENOTSUPPORTED;
        }
        
        // Hardware maps whole 1 MiB blocks, but firmware may reserve bytes at
        // the end (Pi 5 uses a length of 0xfffffffc). Round up only when checking
        // for overlap and address limits. Callers may use only the bytes in
        // the original firmware description.
        length = (window->Length + BCM_MEGABYTE - 1) & ~(BCM_MEGABYTE - 1);
        if (length - 1 > UINT64_MAX - window->BusBase) {
            WARNING("__BcmValidateOutbound rounded outbound PCI extent wraps");
            return OS_ENOTSUPPORTED;
        }

        // Regular memory ranges can use only 32-bit device addresses here.
        // Ranges marked as safe to read ahead have extra registers for larger addresses.
        if (!(window->Attributes & FDT_PCI_PREFETCHABLE) &&
            (window->BusBase >= (1ULL << 32) || length > (1ULL << 32) - window->BusBase)) {
            WARNING("__BcmValidateOutbound non-prefetchable window exceeds 32-bit PCI addressing");
            return OS_ENOTSUPPORTED;
        }
        
        for (previous = 0; previous < index; previous++) {
            previousLength = (firmware->Windows[previous].Length + BCM_MEGABYTE - 1) &
                    ~(BCM_MEGABYTE - 1);
            // Rounded ranges must not overlap in machine memory or in the
            // addresses seen by devices; otherwise two mappings would conflict.
            if (window->PhysicalBase <= firmware->Windows[previous].PhysicalBase + previousLength - 1 &&
                firmware->Windows[previous].PhysicalBase <= window->PhysicalBase + length - 1) {
                WARNING("__BcmValidateOutbound rounded outbound CPU windows overlap");
                return OS_ENOTSUPPORTED;
            }
            
            if (window->BusBase <= firmware->Windows[previous].BusBase + previousLength - 1 &&
                firmware->Windows[previous].BusBase <= window->BusBase + length - 1) {
                WARNING("__BcmValidateOutbound rounded outbound PCI windows overlap");
                return OS_ENOTSUPPORTED;
            }
        }
    }

    return OS_EOK;
}

static oserr_t
__BcmBridgeWindow(
    _In_ PciHost_t*               bus,
    _In_ const struct FdtPciHost* firmware)
{
    uint64_t     base[2] = { UINT64_MAX, UINT64_MAX };
    uint64_t     limit[2] = { 0, 0 };
    uint64_t     end;
    uint32_t     index;
    unsigned int kind;
    uint32_t     memory = 0x0000FFF0;
    uint32_t     prefetch = 0x0000FFF0;
    uint32_t     command;
    oserr_t      status;

    TRACE("__BcmBridgeWindow(segment=%u)", firmware->Segment);

    // The bridge has one range for regular memory and one for memory that a
    // device says may be read ahead. Combine matching startup ranges for each.
    for (index = 0; index < firmware->WindowCount; index++) {
        kind = (firmware->Windows[index].Attributes & FDT_PCI_PREFETCHABLE) != 0;
        if (firmware->Windows[index].BusBase < base[kind]) {
            base[kind] = firmware->Windows[index].BusBase;
        }
        
        end = firmware->Windows[index].BusBase + firmware->Windows[index].Length - 1;
        if (end > limit[kind]) {
            limit[kind] = end;
        }
    }

    if (base[0] != UINT64_MAX) {
        memory = ((uint32_t)(base[0] >> 16) & 0xFFF0) | ((uint32_t)limit[0] & 0xFFF00000);
    }

    if (base[1] != UINT64_MAX) {
        // Mark this range as 64-bit and prefetchable so the bridge uses its
        // separate high-address registers for the full device address.
        prefetch = ((uint32_t)(base[1] >> 16) & 0xFFF0) |
                ((uint32_t)limit[1] & 0xFFF00000) | 0x00010001;
    }

    // Set the address ranges before allowing the bridge to forward memory
    // requests, so devices cannot use an old or incomplete range.
    status = WriteDeviceIo(&bus->IoSpace, 0x20, memory, 4);
    if (status != OS_EOK) {
        ERROR("__BcmBridgeWindow WriteDeviceIo register 0x%x failed: %u",
            (unsigned)(0x20), status);
        return status;
    }
    
    status = WriteDeviceIo(&bus->IoSpace, 0x24, prefetch, 4);
    if (status != OS_EOK) {
        ERROR("__BcmBridgeWindow WriteDeviceIo register 0x%x failed: %u",
            (unsigned)(0x24), status);
        return status;
    }

    status = WriteDeviceIo(&bus->IoSpace, 0x28, base[1] == UINT64_MAX ? 0 : base[1] >> 32, 4);
    if (status != OS_EOK) {
        ERROR("__BcmBridgeWindow WriteDeviceIo register 0x%x failed: %u",
            (unsigned)(0x28), status);
        return status;
    }

    status = WriteDeviceIo(&bus->IoSpace, 0x2C, limit[1] >> 32, 4);
    if (status != OS_EOK) {
        ERROR("__BcmBridgeWindow WriteDeviceIo register 0x%x failed: %u",
            (unsigned)(0x2C), status);
        return status;
    }

    status = WriteDeviceIo(&bus->IoSpace, 0x1C, 0x00F0, 2);
    if (status != OS_EOK) {
        ERROR("__BcmBridgeWindow WriteDeviceIo register 0x%x failed: %u",
            (unsigned)(0x1C), status);
        return status;
    }

    command = (uint32_t)ReadDeviceIo(&bus->IoSpace, 0x04, 2);
    command = (command & ~PCI_COMMAND_PORTIO) | PCI_COMMAND_MMIO | PCI_COMMAND_BUSMASTER;
    
    status = WriteDeviceIo(&bus->IoSpace, 0x04, command, 2);
    if (status != OS_EOK) {
        ERROR("__BcmBridgeWindow WriteDeviceIo register 0x%x failed: %u",
            (unsigned)(0x04), status);
        return status;
    }
    return OS_EOK;
}

static oserr_t
__BcmWindows(
    _In_ PciHost_t*               bus,
    _In_ const struct FdtPciHost* firmware)
{
    const struct FdtPciWindow* window;
    uint64_t                   base;
    uint64_t                   limit;
    uint32_t                   index;
    oserr_t                    status;

    TRACE("__BcmWindows(segment=%u, windows=%u)", firmware->Segment, firmware->WindowCount);

    // Clear all four hardware ranges first so settings left by earlier firmware
    // cannot conflict with the ranges this driver is about to install.
    for (index = 0; index < 4; index++) {
        status = WriteDeviceIo(&bus->IoSpace, BCM_OUTBOUND_BASE_LIMIT + index * 4, 0xFFF0, 4);
        if (status != OS_EOK) {
            ERROR("__BcmWindows WriteDeviceIo register 0x%x failed: %u",
                (unsigned)(BCM_OUTBOUND_BASE_LIMIT + index * 4), status);
            return status;
        }

        status = WriteDeviceIo(&bus->IoSpace, BCM_OUTBOUND_BASE_HIGH + index * 8, 0, 4);
        if (status != OS_EOK) {
            ERROR("__BcmWindows WriteDeviceIo register 0x%x failed: %u",
                (unsigned)(BCM_OUTBOUND_BASE_HIGH + index * 8), status);
            return status;
        }

        status = WriteDeviceIo(&bus->IoSpace, BCM_OUTBOUND_BASE_HIGH + index * 8 + 4, 0, 4);
        if (status != OS_EOK) {
            ERROR("__BcmWindows WriteDeviceIo register 0x%x failed: %u",
                (unsigned)(BCM_OUTBOUND_BASE_HIGH + index * 8 + 4), status);
            return status;
        }
    }

    // Install each firmware range: device addresses are sent to the matching
    // machine-memory range when a device reads or writes it.
    for (index = 0; index < firmware->WindowCount; index++) {
        window = &firmware->Windows[index];
        base = window->PhysicalBase >> 20;
        limit = (window->PhysicalBase + window->Length - 1) >> 20;

        status = WriteDeviceIo(&bus->IoSpace, BCM_OUTBOUND_TARGET + index * 8,
            (uint32_t)window->BusBase, 4);
        if (status != OS_EOK) {
            ERROR("__BcmWindows WriteDeviceIo register 0x%x failed: %u",
                (unsigned)(BCM_OUTBOUND_TARGET + index * 8), status);
            return status;
        }

        status = WriteDeviceIo(&bus->IoSpace, BCM_OUTBOUND_TARGET + index * 8 + 4,
            (uint32_t)(window->BusBase >> 32), 4);
        if (status != OS_EOK) {
            ERROR("__BcmWindows WriteDeviceIo register 0x%x failed: %u",
                (unsigned)(BCM_OUTBOUND_TARGET + index * 8 + 4), status);
            return status;
        }

        status = WriteDeviceIo(&bus->IoSpace, BCM_OUTBOUND_BASE_HIGH + index * 8,
            (uint32_t)(base >> 12), 4);
        if (status != OS_EOK) {
            ERROR("__BcmWindows WriteDeviceIo register 0x%x failed: %u",
                (unsigned)(BCM_OUTBOUND_BASE_HIGH + index * 8), status);
            return status;
        }

        status = WriteDeviceIo(&bus->IoSpace, BCM_OUTBOUND_BASE_HIGH + index * 8 + 4,
            (uint32_t)(limit >> 12), 4);
        if (status != OS_EOK) {
            ERROR("__BcmWindows WriteDeviceIo register 0x%x failed: %u",
                (unsigned)(BCM_OUTBOUND_BASE_HIGH + index * 8 + 4), status);
            return status;
        }

        status = WriteDeviceIo(&bus->IoSpace, BCM_OUTBOUND_BASE_LIMIT + index * 4,
            ((uint32_t)base & 0xFFF) << 4 | ((uint32_t)limit & 0xFFF) << 20, 4);
        if (status != OS_EOK) {
            ERROR("__BcmWindows WriteDeviceIo register 0x%x failed: %u",
                (unsigned)(BCM_OUTBOUND_BASE_LIMIT + index * 4), status);
            return status;
        }
    }

    return OS_EOK;
}

const struct BcmPciVariant*
BcmPciGetVariant(
        _In_ enum FdtPciHostType type)
{
    // Select the register layout that matches the host type reported by firmware.
    switch (type) {
        case FdtPciHostBcm2711:
            return &g_bcm2711PciVariant;
        case FdtPciHostBcm2712:
            return &g_bcm2712PciVariant;
        default:
            return NULL;
    }
}

oserr_t
BcmPciInitialize(
        _InOut_ PciHost_t* bus,
        _Out_ struct BcmPciHost* controller,
        _In_ const struct FdtPciHost* firmware)
{
    unsigned int                dmaOrder;
    unsigned int                attempt;
    oserr_t                     status;
    const struct BcmPciVariant* variant;
    int                         lockStatus;

    TRACE("BcmPciInitialize(bus=%p, firmware=%p)", bus, firmware);

    // Check inputs and firmware ranges before changing the bus, so a bad
    // description cannot leave the controller partly configured.
    if (bus == NULL || controller == NULL || firmware == NULL) {
        ERROR("BcmPciInitialize missing host, controller or firmware");
        return OS_EINVALPARAMS;
    }

    variant = BcmPciGetVariant(firmware->Type);
    if (variant == NULL) {
        WARNING("BcmPciInitialize unsupported controller type %u", firmware->Type);
        return OS_ENOTSUPPORTED;
    }
    if (bus->OpContext != NULL || bus->Operations != NULL) {
        WARNING("BcmPciInitialize host is already initialized");
        return OS_EINVALPARAMS;
    }
    if (bus->IoSpace.Type != DeviceIoMemoryBased ||
        bus->IoSpace.Access.Memory.Length < variant->RegisterLength) {
        ERROR("BcmPciInitialize missing or truncated register mapping");
        return OS_EINVALPARAMS;
    }

    status = variant->Validate(firmware, &dmaOrder);
    if (status != OS_EOK) {
        ERROR("BcmPciInitialize variant->Validate failed: %u", status);
        return status;
    }

    status = __BcmValidateOutbound(firmware);
    if (status != OS_EOK) {
        ERROR("BcmPciInitialize __BcmValidateOutbound failed: %u", status);
        return status;
    }
    
    memset(controller, 0, sizeof(*controller));
    controller->Firmware = *firmware;
    controller->Variant = variant;
    lockStatus = mtx_init(&controller->ConfigLock, mtx_plain);
    if (lockStatus != thrd_success) {
        ERROR("BcmPciInitialize failed to initialize configuration lock: %i", lockStatus);
        return OS_EOOM;
    }

    bus->OpContext = controller;
    bus->DriversBlocked = 1;
    bus->IoResourcePolicy = PciIoResourceMemory;
    bus->IsExtended = 1;
    bus->Identification.BusStart = firmware->BusStart;
    bus->Identification.BusEnd = firmware->BusEnd;
    bus->Identification.Segment = firmware->Segment;

    // Prepare the host before installing its address ranges and memory access.
    status = variant->Prepare(bus, firmware);
    if (status != OS_EOK) {
        ERROR("BcmPciInitialize variant->Prepare failed: %u", status);
        goto failure;
    }

    status = __BcmWindows(bus, firmware);
    if (status != OS_EOK) {
        ERROR("BcmPciInitialize __BcmWindows failed: %u", status);
        goto failure;
    }

    status = variant->ProgramInbound(bus, firmware, dmaOrder);
    if (status != OS_EOK) {
        ERROR("BcmPciInitialize variant->ProgramInbound failed: %u", status);
        goto failure;
    }
    controller->InboundConfigured = 1;

    status = __BcmBridgeWindow(bus, firmware);
    if (status != OS_EOK) {
        ERROR("BcmPciInitialize __BcmBridgeWindow failed: %u", status);
        goto failure;
    }

    status = variant->Start(bus, firmware);
    if (status != OS_EOK) {
        ERROR("BcmPciInitialize variant->Start failed: %u", status);
        goto failure;
    }

    // The connection takes time to start. Wait for it before making read and
    // write operations available to the rest of the PCI system.
    for (attempt = 0; attempt <= 20; attempt++) {
        if (BcmPciLinkUp(bus)) {
            controller->Ready = 1;
            bus->Operations = &g_pciBcmOperations;
            bus->Firmware = &controller->Firmware;
            TRACE("BcmPciInitialize segment %u link ready after %u polls",
                firmware->Segment, attempt + 1);
            return OS_EOK;
        }
        if (attempt == 20) {
            break;
        }
        BcmPciDelay(5);
    }

    ERROR("BcmPciInitialize segment %u link did not come up", firmware->Segment);
    status = OS_EUNKNOWN;

failure:
    // Remove the hardware setup and published bus state together after failure.
    controller->InboundConfigured = 0;
    variant->Stop(bus);
    bus->OpContext = NULL;
    bus->Firmware = NULL;
    bus->Operations = NULL;
    mtx_destroy(&controller->ConfigLock);
    return status;
}

void
BcmPciDestroy(
    _InOut_ PciHost_t*         bus,
    _InOut_ struct BcmPciHost* controller)
{
    int status;

    TRACE("BcmPciDestroy(bus=%p, controller=%p)", bus, controller);

    // Ignore incomplete or stale requests so they cannot stop another host.
    if (bus == NULL || controller == NULL) {
        return;
    }
    if (bus->OpContext != controller) {
        return;
    }

    // Prevent new register work while stopping the host and removing its callbacks.
    status = mtx_lock(&controller->ConfigLock);
    if (status != thrd_success) {
        ERROR("BcmPciDestroy failed to lock configuration: %i", status);
    }
    controller->Ready = 0;
    controller->InboundConfigured = 0;
    controller->Variant->Stop(bus);
    bus->Operations = NULL;
    bus->OpContext = NULL;
    bus->Firmware = NULL;
    status = mtx_unlock(&controller->ConfigLock);
    if (status != thrd_success) {
        ERROR("BcmPciDestroy failed to unlock configuration: %i", status);
    }
    mtx_destroy(&controller->ConfigLock);
}

oserr_t
BcmPciDmaAddress(
    _In_  const struct BcmPciHost* controller,
    _In_  uint64_t                 physical,
    _In_  uint64_t                 length,
    _Out_ uint64_t*                address)
{
    const struct FdtPciWindow* window;
    uint64_t                   displacement;
    uint32_t                   index;

    // A translation needs a live host, an output location, and a non-empty
    // physical range whose end does not wrap around.
    if (controller == NULL || address == NULL) {
        WARNING("BcmPciDmaAddress missing controller or output");
        return OS_EINVALPARAMS;
    }
    if (!controller->Ready || !controller->InboundConfigured || length == 0) {
        WARNING("BcmPciDmaAddress controller is not ready or length is zero");
        return OS_EINVALPARAMS;
    }
    if (length - 1 > UINT64_MAX - physical) {
        WARNING("BcmPciDmaAddress requested range wraps the physical address space");
        return OS_EINVALPARAMS;
    }

    // Only ranges that describe normal memory can be used for device transfers.
    for (index = 0; index < controller->Firmware.DmaWindowCount; index++) {
        window = &controller->Firmware.DmaWindows[index];
        if (window->Kind != FdtDmaWindowRam) {
            continue;
        }
        if (physical < window->PhysicalBase) {
            continue;
        }
        
        displacement = physical - window->PhysicalBase;
        if (displacement >= window->Length || length > window->Length - displacement) {
            continue;
        }
        if (displacement > UINT64_MAX - window->BusBase) {
            continue;
        }
        if (length - 1 > UINT64_MAX - (window->BusBase + displacement)) {
            continue;
        }
        *address = window->BusBase + displacement;
        return OS_EOK;
    }

    TRACE("BcmPciDmaAddress no RAM window contains 0x%llx+0x%llx",
        (unsigned long long)physical, (unsigned long long)length);
    return OS_ENOENT;
}
