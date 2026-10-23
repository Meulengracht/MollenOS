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

#include <bus/pci/discovery.h>
#include <bus/pci/host-private.h>
#include <bus/pci/enumerate.h>
#include <bus/pci/interrupts.h>
#include <bus/pci/publish.h>
#include <bus/pci/hosts/ecam.h>
#include <bus/pci/hosts/legacy.h>
#include <bus/pci/hosts/broadcom/bcm.h>
#include <ddk/acpi.h>
#include <ddk/firmware.h>
#include <ddk/utils.h>
#include <firmware/pci.h>
#include <stdlib.h>

#ifdef __OSCONFIG_HAS_LEGACY_PCI
#include <bus/legacy/fixed.h>
#endif

static int g_acpiAvailable;
static int g_msiDisabled;

int
PciIsMsiAvailable(void)
{
    if (g_msiDisabled) {
        return 0;
    }
    return 1;
}

int
PciIsAcpiAvailable(void)
{
    return g_acpiAvailable;
}

#ifdef __OSCONFIG_HAS_LEGACY_PCI
static void
__EnumerateLegacy(void)
{
    PciHost_t* host;
    oserr_t    status;

    TRACE("__EnumerateLegacy()");

    status = PciLegacyHostCreate(&host);
    if (status != OS_EOK) {
        ERROR("__EnumerateLegacy failed to create host: %u", status);
        return;
    }
    
    status = PciHostAttach(host, NULL);
    if (status != OS_EOK) {
        ERROR("__EnumerateLegacy failed to attach host: %u", status);
        status = PciHostDestroy(host);
        if (status != OS_EOK) {
            ERROR("__EnumerateLegacy failed to destroy unattached host: %u", status);
        }
        return;
    }
    
    PciScanLegacyRoots(host);
}
#endif

// <base> is the ECAM address of <busStart>, each bus decodes 1MB.
static void
__EnumerateEcamWindow(
    _In_ uint32_t                   segment,
    _In_ uint8_t                    busStart,
    _In_ uint8_t                    busEnd,
    _In_ uint64_t                   base,
    _In_ const struct FdtPciHost*   firmwareHost,
    _In_ struct PciFirmwareMapping* mapping)
{
    PciHost_t*                      host;
    oserr_t                         status;
    struct PciEcamHostConfiguration configuration = {
        .Identification = {
            .Segment = segment,
            .BusStart = busStart,
            .BusEnd = busEnd
        },
        .Base = base,
        .IoResourcePolicy = PciIoResourceMemory,
        .Firmware = firmwareHost
    };

    TRACE("__EnumerateEcamWindow(segment=%u, buses=%u-%u, base=0x%llx)",
        segment, busStart, busEnd, (unsigned long long)base);

#ifdef __OSCONFIG_HAS_LEGACY_PCI
    if (firmwareHost == NULL) {
        configuration.IoResourcePolicy = PciIoResourcePorts;
    }
#endif
    
    status = PciEcamHostCreate(&configuration, &host);
    if (status != OS_EOK) {
        ERROR("__EnumerateEcamWindow failed to create segment %u: %u", segment, status);
        return;
    }
    
    status = PciHostAttach(host, mapping);
    if (status != OS_EOK) {
        ERROR("__EnumerateEcamWindow failed to attach segment %u: %u", segment, status);
        status = PciHostDestroy(host);
        if (status != OS_EOK) {
            ERROR("__EnumerateEcamWindow failed to destroy unattached host: %u", status);
        }
        return;
    }
    PciCheckBus(host->RootDevice, busStart);
}

static void
__EnumerateMcfg(void)
{
    ACPI_TABLE_HEADER*    header;
    ACPI_MCFG_ALLOCATION* entry;
    size_t                count;
    oserr_t               status;

    TRACE("__EnumerateMcfg()");

    status = AcpiQueryTable(ACPI_SIG_MCFG, &header);
    if (status != OS_EOK) {
        TRACE("__EnumerateMcfg MCFG unavailable: %u; legacy fallback may be used", status);
        return;
    }

    if (header->Length < sizeof(ACPI_TABLE_MCFG)) {
        ERROR("__EnumerateMcfg truncated MCFG, length %u", header->Length);
        free(header);
        return;
    }

    entry = (ACPI_MCFG_ALLOCATION*)((uint8_t*)header + sizeof(ACPI_TABLE_MCFG));
    count = (header->Length - sizeof(ACPI_TABLE_MCFG)) / sizeof(ACPI_MCFG_ALLOCATION);
    for (size_t i = 0; i < count; i++, entry++) {
        if (entry->EndBusNumber < entry->StartBusNumber) {
            WARNING("__EnumerateMcfg ignoring segment %u with inverted bus range %u-%u",
                entry->PciSegment, entry->StartBusNumber, entry->EndBusNumber);
            continue;
        }
        if (entry->Address > UINT64_MAX - ((uint64_t)entry->StartBusNumber << 20)) {
            WARNING("__EnumerateMcfg ECAM base overflows for segment %u", entry->PciSegment);
            continue;
        }

        // MCFG addresses are relative to bus 0
        __EnumerateEcamWindow(
            entry->PciSegment, entry->StartBusNumber, entry->EndBusNumber,
            entry->Address + ((uint64_t)entry->StartBusNumber << 20), NULL, NULL
        );
    }
    free(header);
}

static void
__OnFdtPciHost(
    _In_ const struct FdtPciHost* host,
    _In_ void*                    context)
{
    PciHost_t*                  bus;
    struct BcmPciHost*          controller;
    const struct BcmPciVariant* variant;
    oserr_t                     status;
    oserr_t                     cleanup;

    TRACE("__OnFdtPciHost(segment=%u, buses=%u-%u, type=%u)",
        host->Segment, host->BusStart, host->BusEnd, host->Type);

    if (host->Type == FdtPciHostEcam) {
        __EnumerateEcamWindow(
            host->Segment,
            host->BusStart,
            host->BusEnd,
            host->EcamBase,
            host,
            context
        );
        return;
    }
    
    variant = BcmPciGetVariant(host->Type);
    if (variant == NULL) {
        WARNING("PCI host segment %u requires its own controller variant (type %u)",
            host->Segment, host->Type);
        return;
    }
    
    if (host->EcamBase > UINTPTR_MAX || host->EcamLength < variant->RegisterLength ||
        host->EcamLength > SIZE_MAX || host->EcamLength - 1 > UINTPTR_MAX - host->EcamBase) {
        ERROR("__OnFdtPciHost segment %u has an unusable register window 0x%llx+0x%llx",
            host->Segment, (unsigned long long)host->EcamBase,
            (unsigned long long)host->EcamLength);
        return;
    }
    
    bus = calloc(1, sizeof(PciHost_t) + sizeof(struct BcmPciHost));
    if (bus == NULL) {
        ERROR("__OnFdtPciHost failed to allocate segment %u", host->Segment);
        return;
    }
    
    controller = (struct BcmPciHost*)(bus + 1);
    status = CreateDeviceMemoryIo(
        &bus->IoSpace,
        (uintptr_t)host->EcamBase,
        (size_t)host->EcamLength
    );
    if (status != OS_EOK) {
        ERROR("__OnFdtPciHost failed to create register I/O: %u", status);
        free(bus);
        return;
    }
    
    status = AcquireDeviceIo(&bus->IoSpace);
    if (status != OS_EOK) {
        ERROR("__OnFdtPciHost failed to acquire register I/O: %u", status);
        cleanup = DestroyDeviceIo(&bus->IoSpace);
        if (cleanup != OS_EOK) {
            ERROR("__OnFdtPciHost failed to destroy register I/O: %u", cleanup);
        }
        free(bus);
        return;
    }
    
    status = BcmPciInitialize(bus, controller, host);
    if (status != OS_EOK) {
        ERROR("Broadcom PCIe segment %u initialization failed: %u", host->Segment, status);
        cleanup = ReleaseDeviceIo(&bus->IoSpace);
        if (cleanup != OS_EOK) {
            ERROR("__OnFdtPciHost failed to release register I/O: %u", cleanup);
        }
        cleanup = DestroyDeviceIo(&bus->IoSpace);
        if (cleanup != OS_EOK) {
            ERROR("__OnFdtPciHost failed to destroy register I/O: %u", cleanup);
        }
        free(bus);
        return;
    }
    status = PciHostAttach(bus, context);
    if (status != OS_EOK) {
        ERROR("__OnFdtPciHost failed to attach segment %u: %u", host->Segment, status);
        status = PciHostDestroy(bus);
        if (status != OS_EOK) {
            ERROR("__OnFdtPciHost failed to destroy unattached host: %u", status);
        }
        return;
    }
    
    PciCheckBus(bus->RootDevice, host->BusStart);
    TRACE("Broadcom PCIe host initialized and enumerated");
}

static void
__EnumerateDeviceTree(void)
{
    OSFirmwareTableKey_t       key = { .Source = OSFIRMWARE_DEVICETREE };
    const void*                blob;
    size_t                     length;
    struct PciFirmwareMapping* mapping;
    oserr_t                    oserr;

    TRACE("__EnumerateDeviceTree()");

    oserr = FirmwareTableMap(&key, &blob, &length);
    if (oserr != OS_EOK) {
        ERROR("BusEnumerate failed to map the device tree: %u", oserr);
        return;
    }

    mapping = calloc(1, sizeof(struct PciFirmwareMapping));
    if (mapping == NULL) {
        ERROR("__EnumerateDeviceTree failed to allocate firmware mapping");
        oserr = FirmwareTableUnmap(blob, length);
        if (oserr != OS_EOK) {
            ERROR("__EnumerateDeviceTree failed to unmap firmware: %u", oserr);
        }
        return;
    }
    
    mapping->Blob = blob;
    mapping->Length = length;
    mapping->References = 1;
    
    oserr = FdtEnumeratePciHosts(blob, length, __OnFdtPciHost, mapping);
    if (oserr != OS_EOK) {
        ERROR("BusEnumerate malformed device tree: %u", oserr);
    } else if (mapping->References == 1) {
        WARNING("BusEnumerate could not initialize any device tree PCI hosts");
    }
    
    PciFirmwareRelease(mapping);
}

void
BusEnumerate(void)
{
    AcpiDescriptor_t acpi = { 0 };
    OSFirmwareInfo_t firmware = { 0 };
    element_t*       element;
    oserr_t          status;

    TRACE("BusEnumerate()");

    PciInitialize();
    
    // This entry point discovers at startup; it is not a hot-plug rescan.
    // In particular, do not reinitialize a live Broadcom controller.
    if (g_pciRoots.head != NULL) {
        TRACE("BusEnumerate hosts already enumerated");
        return;
    }
    g_acpiAvailable = 0;
    g_msiDisabled = 0;

    status = AcpiQueryStatus(&acpi);
    if (status != OS_EOK) {
        TRACE("BusEnumerate ACPI unavailable: %u", status);
    } else {
        TRACE("ACPI-Version: 0x%x (BootFlags 0x%x)",
              acpi.Version, acpi.BootFlags);
        g_acpiAvailable = 1;
        g_msiDisabled = (acpi.BootFlags & ACPI_IA_NO_MSI) != 0;
    }

#ifdef __OSCONFIG_HAS_LEGACY_PCI
    // Without ACPI we assume all 8042 devices are present
    if (!g_acpiAvailable || (acpi.BootFlags & ACPI_IA_8042) || acpi.BootFlags == 0) {
        __InstallPS2Controller();
    }
#endif

    status = FirmwareQuery(&firmware);
    if (status != OS_EOK) {
        WARNING("BusEnumerate firmware query failed: %u; continuing bus fallback", status);
    }
    switch (firmware.Primary) {
        case OSFIRMWARE_ACPI:
            __EnumerateMcfg();
            break;
        case OSFIRMWARE_DEVICETREE:
            __EnumerateDeviceTree();
            break;
        default:
            break;
    }

#ifdef __OSCONFIG_HAS_LEGACY_PCI
    if (g_pciRoots.head == NULL) {
        __EnumerateLegacy();
    }
#endif

    if (g_pciRoots.head == NULL) {
        WARNING("BusEnumerate no usable PCI configuration hosts");
    }

    // we do not need to take a lock here
    _foreach(element, &g_pciRoots) {
        status = PciPublishDevice(element->value);
        if (status != OS_EOK) {
            ERROR("PCI publication failed: %u", status);
        }
    }
}
