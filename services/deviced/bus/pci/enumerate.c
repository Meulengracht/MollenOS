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

#include <bus/pci/enumerate.h>
#include <bus/pci/host-private.h>
#include <bus/pci/device.h>
#include <bus/pci/function.h>
#include <bus/pci/config.h>
#include <bus/pci/interrupts.h>
#include <bus/pci/strings.h>
#include <ddk/busdevice.h>
#include <ddk/utils.h>
#include <stdlib.h>

#define DEVICE_IS_PCI_BRIDGE(device) ((device)->Header->Class == PCI_CLASS_BRIDGE && (device)->Header->Subclass == PCI_BRIDGE_SUBCLASS_PCI)

unsigned int PciToDevClass(uint32_t Class, uint32_t SubClass) {
    return ((Class & 0xFFFF) << 16 | (SubClass & 0xFFFF));
}

unsigned int PciToDevSubClass(uint32_t Interface) {
    return ((Interface & 0xFFFF) << 16 | 0);
}

static oserr_t __GetPciDeviceNativeHeader(
    _In_  PciDevice_t*        parent,
    _In_  int                 bus,
    _In_  int                 slot,
    _In_  int                 function,
    _Out_ PciNativeHeader_t** headerOut)
{
    PciNativeHeader_t* nativeHeader;

    nativeHeader = (PciNativeHeader_t*)malloc(sizeof(PciNativeHeader_t));
    if (!nativeHeader) {
        return OS_EOOM;
    }

    // Read entire function information
    PciReadFunction(nativeHeader, parent->Host, (unsigned int)bus, (unsigned int)slot, (unsigned int)function);

    *headerOut = nativeHeader;
    return OS_EOK;
}

static oserr_t
PciCheckFunction(
    _In_ PciDevice_t* parent,
    _In_ int          bus,
    _In_ int          slot,
    _In_ int          function)
{
    oserr_t                     oserr;
    PciDevice_t*                device;
    int                         secondBus;
    uint16_t                    settings;
    BusDevice_t                 resources = { 0 };

    device = calloc(1, sizeof(PciDevice_t));
    if (!device) {
        return OS_EOOM;
    }

    oserr = __GetPciDeviceNativeHeader(parent, bus, slot, function, &device->Header);
    if (oserr != OS_EOK) {
        free(device);
        return oserr;
    }

    device->Handler = NULL;
    device->Attachment = NULL;
    device->Parent      = parent;
    device->Host       = parent->Host;
    device->Bus         = bus;
    device->Slot        = slot;
    device->Function    = function;
    device->AcpiConform = 0;
    device->InterruptLine = device->Header->InterruptLine;
    device->IsBridge    = DEVICE_IS_PCI_BRIDGE(device) ? 1 : 0; // this relies on device->Header
    ELEMENT_INIT(&device->list_header, 0, device);
    ELEMENT_INIT(&device->child_header, (uintptr_t)device->IsBridge, device);
    list_construct(&device->children);

    // Trace Information about device 
    // Ignore the spam of device_id 0x7a0 in VMWare
    // This is VIRTIO devices
    if (device->Header->DeviceId != 0x7a0) {
        TRACE(" - [%x:%x:%x] %s", bus, slot, function,
              PciToString(device->Header->Class, device->Header->Subclass, device->Header->Interface));
    }

    // Do some disabling, but NOT on the video or bridge
    if ((device->Header->Class != PCI_CLASS_BRIDGE)
        && (device->Header->Class != PCI_CLASS_VIDEO)) {
        uint16_t pciSettings = PciRead16(device->Host, bus, slot, function, 0x04);
        PciWrite16(device->Host, bus, slot, function, 0x04, pciSettings | PCI_COMMAND_INTDISABLE);
    }
    
    device->Handler = PciFunctionHandlerFind(device);
    if ((device->Host->DriversBlocked || device->Handler != NULL) && !device->IsBridge) {
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
        oserr = device->Handler->Attach(device, &device->Resources, &device->Attachment);
        if (oserr != OS_EOK) {
            WARNING("PCI %u:%u:%u.%u function attachment failed (%u)",
                device->Host->Identification.Segment, device->Bus, device->Slot, device->Function, oserr);
        }
    }

    // add device to lists
    list_append(&g_pciDevices, &device->list_header);
    list_append(&parent->children, &device->child_header);

    if (DEVICE_IS_PCI_BRIDGE(device)) {
        // Extract secondary bus
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
    _In_ int          bus,
    _In_ int          slot)
{
    uint16_t vendorId;
    uint8_t  headerType;
    int      function = 0;

    // Validate the vendor id, it's invalid only
    // if there is no device on that location
    vendorId = PciReadVendorId(
        parent->Host,
        (unsigned int)bus,
        (unsigned int)slot,
        (unsigned int)function
    );

    // Sanitize if device is present
    if (vendorId == 0xFFFF) {
        return;
    }

    // Check base function
    PciCheckFunction(parent, bus, slot, function);

    // Multi-function or single? 
    // If it is a multi-function device, check remaining functions
    headerType = PciReadHeaderType(
        parent->Host,
        (unsigned int)bus,
        (unsigned int)slot,
        (unsigned int)function
    );
    if (headerType & 0x80) {
        for (function = 1; function < 8; function++) {
            if (PciReadVendorId(parent->Host, bus, slot, function) != 0xFFFF) {
                PciCheckFunction(parent, bus, slot, function);
            }
        }
    }
}

void
PciCheckBus(
    _In_ PciDevice_t* parent,
    _In_ int          bus)
{
    int device;

    if (parent == NULL) {
        return;
    }

    if (bus < parent->Host->Identification.BusStart || bus > parent->Host->Identification.BusEnd) {
        return;
    }

    // Each bus belongs to one path through this host. Broken bridge routing
    // and legacy root aliases must not create cycles or duplicate functions.
    if (parent->Host->ScannedBuses[bus / 8] & (1U << (bus % 8))) {
        return;
    }
    parent->Host->ScannedBuses[bus / 8] |= 1U << (bus % 8);

    // Iterate all possible 32 devices on the pci-bus
    for (device = 0; device < 32; device++) {
        PciCheckDevice(parent, bus, device);
    }
}

#ifdef __OSCONFIG_HAS_LEGACY_PCI
void
PciScanLegacyRoots(
    _In_ PciHost_t* host)
{
    int function;

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
