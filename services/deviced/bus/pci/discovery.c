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

#include "private.h"
#include "hosts/ecam.h"
#include "hosts/legacy.h"
#include "hosts/broadcom/bcm.h"
#include <ddk/acpi.h>
#include <ddk/firmware.h>
#include <ddk/utils.h>
#include <firmware/pci.h>
#include <stdlib.h>

#ifdef __OSCONFIG_HAS_LEGACY_PCI
extern oserr_t __InstallPS2Controller(void);
#endif

static int g_acpiAvailable;

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

    if (PciLegacyHostCreate(&host) != OS_EOK) {
        return;
    }
    
    if (PciHostAttach(host, NULL) != OS_EOK) {
        PciHostDestroy(host);
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

#ifdef __OSCONFIG_HAS_LEGACY_PCI
    if (firmwareHost == NULL) {
        configuration.IoResourcePolicy = PciIoResourcePorts;
    }
#endif
    
    if (PciEcamHostCreate(&configuration, &host) != OS_EOK) {
        return;
    }
    
    if (PciHostAttach(host, mapping) != OS_EOK) {
        PciHostDestroy(host);
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

    if (AcpiQueryTable(ACPI_SIG_MCFG, &header) != OS_EOK) {
        WARNING("BusEnumerate ACPI system without MCFG");
        return;
    }

    if (header->Length < sizeof(ACPI_TABLE_MCFG)) {
        free(header);
        return;
    }

    entry = (ACPI_MCFG_ALLOCATION*)((uint8_t*)header + sizeof(ACPI_TABLE_MCFG));
    count = (header->Length - sizeof(ACPI_TABLE_MCFG)) / sizeof(ACPI_MCFG_ALLOCATION);
    for (size_t i = 0; i < count; i++, entry++) {
        if (entry->EndBusNumber < entry->StartBusNumber) {
            continue;
        }
        if (entry->Address > UINT64_MAX - ((uint64_t)entry->StartBusNumber << 20)) {
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
        return;
    }
    
    bus = calloc(1, sizeof(PciHost_t) + sizeof(struct BcmPciHost));
    if (bus == NULL) {
        return;
    }
    
    controller = (struct BcmPciHost*)(bus + 1);
    status = CreateDeviceMemoryIo(
        &bus->IoSpace,
        (uintptr_t)host->EcamBase,
        (size_t)host->EcamLength
    );
    if (status != OS_EOK) {
        free(bus);
        return;
    }
    
    status = AcquireDeviceIo(&bus->IoSpace);
    if (status != OS_EOK) {
        DestroyDeviceIo(&bus->IoSpace);
        free(bus);
        return;
    }
    
    status = BcmPciInitialize(bus, controller, host);
    if (status != OS_EOK) {
        ERROR("Broadcom PCIe segment %u initialization failed: %u", host->Segment, status);
        ReleaseDeviceIo(&bus->IoSpace);
        DestroyDeviceIo(&bus->IoSpace);
        free(bus);
        return;
    }
    if (PciHostAttach(bus, context) != OS_EOK) {
        PciHostDestroy(bus);
        return;
    }
    
    PciCheckBus(bus->RootDevice, host->BusStart);
    WARNING("Broadcom PCIe enumerated; device drivers remain blocked pending DMA/INTx acceptance");
}

static void __EnumerateDeviceTree(void)
{
    OSFirmwareTableKey_t       key = { .Source = OSFIRMWARE_DEVICETREE };
    const void*                blob;
    size_t                     length;
    struct PciFirmwareMapping* mapping;
    oserr_t                    oserr;

    oserr = FirmwareTableMap(&key, &blob, &length);
    if (oserr != OS_EOK) {
        ERROR("BusEnumerate failed to map the device tree: %u", oserr);
        return;
    }

    mapping = calloc(1, sizeof(struct PciFirmwareMapping));
    if (mapping == NULL) {
        FirmwareTableUnmap(blob, length);
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

void BusEnumerate(void)
{
    AcpiDescriptor_t acpi = { 0 };
    OSFirmwareInfo_t firmware = { 0 };
    element_t*       element;
    oserr_t          status;

    PciInitialize();
    
    // This entry point discovers at startup; it is not a hot-plug rescan.
    // In particular, do not reinitialize a live Broadcom controller.
    if (g_pciRoots.head != NULL) {
        return;
    }
    g_acpiAvailable = 0;

    if (AcpiQueryStatus(&acpi) == OS_EOK) {
        TRACE("ACPI-Version: 0x%x (BootFlags 0x%x)",
              acpi.Version, acpi.BootFlags);
        g_acpiAvailable = 1;
    }

#ifdef __OSCONFIG_HAS_LEGACY_PCI
    // Without ACPI we assume all 8042 devices are present
    if (!g_acpiAvailable || (acpi.BootFlags & ACPI_IA_8042) || acpi.BootFlags == 0) {
        __InstallPS2Controller();
    }
#endif

    (void)FirmwareQuery(&firmware);
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

