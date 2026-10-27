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

#include <bus/pci/enumerate.h>
#include <bus/pci/host-private.h>
#include <bus/pci/device.h>
#include <bus/pci/function.h>
#include <bus/pci/config.h>
#include <bus/pci/interrupts.h>
#include <bus/pci/strings.h>
#include <ddk/devices/bus.h>
#include <ddk/utils.h>
#include <stdlib.h>

#define DEVICE_IS_PCI_BRIDGE(device) ((device)->Header->Class == PCI_CLASS_BRIDGE && (device)->Header->Subclass == PCI_BRIDGE_SUBCLASS_PCI)

unsigned int
PciToDevClass(
    _In_ uint32_t Class,
    _In_ uint32_t SubClass)
{
    return ((Class & 0xFFFF) << 16 | (SubClass & 0xFFFF));
}

unsigned int
PciToDevSubClass(
    _In_ uint32_t Interface)
{
    return ((Interface & 0xFFFF) << 16 | 0);
}

static oserr_t
__GetPciDeviceNativeHeader(
    _In_  PciDevice_t*        parent,
    _In_  int                bus,
    _In_  int                slot,
    _In_  int                function,
    _Out_ PciNativeHeader_t** headerOut)
{
    PciNativeHeader_t* nativeHeader;

    nativeHeader = (PciNativeHeader_t*)malloc(sizeof(PciNativeHeader_t));
    if (!nativeHeader) {
        ERROR("__GetPciDeviceNativeHeader allocation failed for %i:%i.%i", bus, slot, function);
        return OS_EOOM;
    }

    // The device owns this header after the function returns successfully.
    PciReadFunction(
        nativeHeader,
        parent->Host,
        (unsigned int)bus,
        (unsigned int)slot,
        (unsigned int)function
    );

    *headerOut = nativeHeader;
    return OS_EOK;
}

static oserr_t
__PciCheckFunction(
    _In_ PciDevice_t* parent,
    _In_ int         bus,
    _In_ int         slot,
    _In_ int         function)
{
    oserr_t      oserr;
    PciDevice_t* device;
    int         secondBus;
    uint16_t    settings;
    uint16_t    pciSettings;
    BusDevice_t resources = { 0 };

    // A failed scan retries missing functions without probing live BARs again.
    device = PciFindDevice(parent->Host->Identification.Segment, bus, slot, function);
    if (device != NULL) {
        if (device->Parent == parent && device->IsBridge) {
            secondBus = PciReadSecondaryBusNumber(device->Host, bus, slot, function);
            PciCheckBus(device, secondBus);
        }
        return OS_EOK;
    }

    device = calloc(1, sizeof(PciDevice_t));
    if (!device) {
        ERROR("PciCheckFunction allocation failed for %i:%i.%i", bus, slot, function);
        return OS_EOOM;
    }

    oserr = __GetPciDeviceNativeHeader(parent, bus, slot, function, &device->Header);
    if (oserr != OS_EOK) {
        free(device);
        return oserr;
    }

    device->Handler       = NULL;
    device->Attachment    = NULL;
    device->Parent        = parent;
    device->Host          = parent->Host;
    device->Bus           = bus;
    device->Slot          = slot;
    device->Function      = function;
    device->AcpiConform   = 0;
    device->InterruptLine = device->Header->InterruptLine;
    device->IsBridge      = DEVICE_IS_PCI_BRIDGE(device) ? 1 : 0;
    ELEMENT_INIT(&device->ListHeader, 0, device);
    ELEMENT_INIT(&device->ChildHeader, (uintptr_t)device->IsBridge, device);
    list_construct(&device->Children);

    // Suppress repeated VMware device traces for device ID 0x7a0.
    if (device->Header->DeviceId != 0x7a0) {
        TRACE(" - [%x:%x:%x] %s", bus, slot, function,
              PciToString(device->Header->Class, device->Header->Subclass, device->Header->Interface));
    }

    // Disable INTx interrupts for devices outside the bridge and video classes.
    if ((device->Header->Class != PCI_CLASS_BRIDGE)
        && (device->Header->Class != PCI_CLASS_VIDEO)) {
        pciSettings = PciRead16(device->Host, bus, slot, function, 0x04);
        PciWrite16(device->Host, bus, slot, function, 0x04, pciSettings | PCI_COMMAND_INTDISABLE);
    }
    
    device->Handler = PciFunctionHandlerFind(device);
    if (device->Handler != NULL && !device->IsBridge) {
        settings = PciRead16(device->Host, bus, slot, function, 0x04);
        settings &= ~PCI_COMMAND_BUSMASTER;
        PciWrite16(device->Host, bus, slot, function, 0x04, settings | PCI_COMMAND_INTDISABLE);
    }

    // Keep one probe result for every user of this function. Functions whose
    // drivers are blocked still appear in device listings and can be scanned for
    // child buses, but their I/O resources are not given to drivers.
    memset(&device->Resources, 0, sizeof(device->Resources));
    device->Resources.Firmware = device->Host->Firmware;
    resources.Bus = bus;
    resources.Slot = slot;
    resources.Function = function;
    
    if (!device->IsBridge) {
        PciProbeBars(device->Host, &resources, device->Header->HeaderType, device->Resources.Bars);
        PciDiagnoseBars(device->Host, &resources, device->Resources.Bars);
    }
    
    if (device->Handler != NULL && !device->IsBridge) {
        TRACE("PciCheckFunction attaching handler to %u:%u:%u.%u",
            device->Host->Identification.Segment, device->Bus, device->Slot, device->Function);
        oserr = device->Handler->Attach(device, &device->Resources, &device->Attachment);
        device->AttachmentStatus = oserr;
        if (oserr != OS_EOK) {
            WARNING("PCI %u:%u:%u.%u function attachment failed (%u)",
                device->Host->Identification.Segment, device->Bus, device->Slot, device->Function, oserr);
        }
    }

    // Add the device to the global list and its parent's child list.
    list_append(&g_pciDevices, &device->ListHeader);
    list_append(&parent->Children, &device->ChildHeader);

    if (DEVICE_IS_PCI_BRIDGE(device)) {
        // The bridge's secondary bus contains its child devices.
        secondBus = PciReadSecondaryBusNumber(device->Host, bus, slot, function);
        PciCheckBus(device, secondBus);
    } else {
        PciResolveInterruptLineAndPin(parent, bus, slot, function, device);
    }
    return OS_EOK;
}

void
PciCheckDevice(
    _In_ PciDevice_t* parent,
    _In_ int         bus,
    _In_ int         slot)
{
    uint16_t vendorId;
    uint8_t  headerType;
    int      function = 0;
    oserr_t  status;

    // Probe function zero before checking the slot's other functions.
    vendorId = PciReadVendorId(
        parent->Host,
        (unsigned int)bus,
        (unsigned int)slot,
        (unsigned int)function
    );

    // A vendor ID of 0xFFFF indicates that the function is absent.
    if (vendorId == 0xFFFF) {
        return;
    }

    status = __PciCheckFunction(parent, bus, slot, function);
    if (status != OS_EOK) {
        ERROR("PciCheckDevice failed to enumerate %i:%i.%i: %u", bus, slot, function, status);
        parent->Host->ScanStatus = status;
    }

    // Header bit 7 must be set before probing functions 1 through 7.
    headerType = PciReadHeaderType(
        parent->Host,
        (unsigned int)bus,
        (unsigned int)slot,
        (unsigned int)function
    );
    if (headerType & 0x80) {
        for (function = 1; function < 8; function++) {
            if (PciReadVendorId(parent->Host, bus, slot, function) != 0xFFFF) {
                status = __PciCheckFunction(parent, bus, slot, function);
                if (status != OS_EOK) {
                    parent->Host->ScanStatus = status;
                    ERROR("PciCheckDevice failed to enumerate %i:%i.%i: %u",
                        bus, slot, function, status);
                }
            }
        }
    }
}

void
PciCheckBus(
    _In_ PciDevice_t* parent,
    _In_ int         bus)
{
    int device;

    if (parent == NULL) {
        WARNING("PciCheckBus missing parent for bus %i", bus);
        return;
    }

    if (bus < parent->Host->Identification.BusStart || bus > parent->Host->Identification.BusEnd) {
        WARNING("PciCheckBus bus %i is outside segment %u range %u-%u", bus,
            parent->Host->Identification.Segment, parent->Host->Identification.BusStart,
            parent->Host->Identification.BusEnd);
        return;
    }

    // Each bus belongs to one path through this host. Broken bridge routing
    // and legacy root aliases must not create cycles or duplicate functions.
    if (parent->Host->ScannedBuses[bus / 8] & (1U << (bus % 8))) {
        TRACE("PciCheckBus skipping previously scanned segment %u bus %i",
            parent->Host->Identification.Segment, bus);
        return;
    }
    TRACE("PciCheckBus(segment=%u, bus=%i)", parent->Host->Identification.Segment, bus);
    parent->Host->ScannedBuses[bus / 8] |= 1U << (bus % 8);

    // A PCI bus has 32 possible device slots.
    for (device = 0; device < 32; device++) {
        PciCheckDevice(parent, bus, device);
    }
}

oserr_t
PciRecoverScan(
    _In_ PciHost_t* host)
{
    if (host->Removing) {
        return OS_EBUSY;
    }
    if (host->ScanStatus == OS_EOK) {
        return OS_EOK;
    }

    memset(host->ScannedBuses, 0, sizeof(host->ScannedBuses));
    host->ScanStatus = OS_EOK;

#ifdef __OSCONFIG_HAS_LEGACY_PCI
    if (host->LegacyScan) {
        PciScanLegacyRoots(host);
    } else {
        PciCheckBus(host->RootDevice, host->Identification.BusStart);
    }
#else
    PciCheckBus(host->RootDevice, host->Identification.BusStart);
#endif
    return host->ScanStatus;
}

#ifdef __OSCONFIG_HAS_LEGACY_PCI
void
PciScanLegacyRoots(
    _In_ PciHost_t* host)
{
    int function;

    TRACE("PciScanLegacyRoots(host=%u)", host->Identification.HostId);

    if (!(PciReadHeaderType(host, 0, 0, 0) & 0x80)) {
        PciCheckBus(host->RootDevice, 0);
        return;
    }
    
    for (function = 0; function < 8; function++) {
        if (PciReadVendorId(host, 0, 0, function) == 0xFFFF) {
            continue;
        }
        PciCheckBus(host->RootDevice, function);
    }
}
#endif
