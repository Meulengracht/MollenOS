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

#include "bus.h"
#include <devices.h>
#include <ddk/busdevice.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

static oserr_t
__PciBusControl(
    _In_ BusDevice_t*              device,
    _In_ struct OSIOCtlBusControl* request)
{
    PciDevice_t* pciDevice = NULL;
    uint16_t     settings;

    pciDevice = PciFindDevice(
        device->Segment,
        device->Bus,
        device->Slot,
        device->Function
    );

    // Sanitize
    if (pciDevice == NULL) {
        ERROR(" > failed to locate pci-device for ioctl");
        return OS_ENOENT;
    }

    if (pciDevice->Host->DriversBlocked ||
        (pciDevice->Handler != NULL && pciDevice->Handler->BlockActivation)) {
        return OS_ENOTSUPPORTED;
    }

    // Read value, modify and write back
    settings = PciRead16(pciDevice->Host, device->Bus, device->Slot, device->Function, 0x04);

    // Clear all possible flags first
    settings &= ~(PCI_COMMAND_BUSMASTER | PCI_COMMAND_FASTBTB
                  | PCI_COMMAND_MMIO | PCI_COMMAND_PORTIO | PCI_COMMAND_INTDISABLE);

    // Handle enable
    if (!(request->Flags & __DEVICEMANAGER_IOCTL_ENABLE)) {
        settings |= PCI_COMMAND_INTDISABLE;
    }

    // Handle io/mmio
    if (request->Flags & __DEVICEMANAGER_IOCTL_MMIO_ENABLE) {
        settings |= PCI_COMMAND_MMIO;
    }
    if (request->Flags & __DEVICEMANAGER_IOCTL_IO_ENABLE) {
        settings |= PCI_COMMAND_PORTIO;
    }

    // Handle busmaster
    if (request->Flags & __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE) {
        settings |= PCI_COMMAND_BUSMASTER;
    }

    // Handle fast-b2b
    if (request->Flags & __DEVICEMANAGER_IOCTL_FASTBTB_ENABLE) {
        settings |= PCI_COMMAND_FASTBTB;
    }

    // Handle memory write and invalidate
    if (request->Flags & __DEVICEMANAGER_IOCTL_MEMWRTINVD_ENABLE) {
        settings |= PCI_COMMAND_MEMWRITE;
    }

    // Write back settings
    PciWrite16(pciDevice->Host, device->Bus, device->Slot, device->Function, 0x04, settings);
    return OS_EOK;
}

static oserr_t
__PciIoctlDevice(
	_In_ BusDevice_t* device,
	_In_ int          direction,
	_In_ unsigned int Register,
	_In_ size_t*      value,
	_In_ size_t       width)
{
    PciDevice_t* pciDevice = NULL;

    pciDevice = PciFindDevice(
        device->Segment,
        device->Bus,
        device->Slot,
        device->Function
    );

    if (pciDevice == NULL) {
        ERROR(" > failed to locate pci-device for ioctl");
        return OS_ENOENT;
    }

    if (direction == __DEVICEMANAGER_IOCTL_EXT_READ) {
        if (width == 1) {
            *value = (size_t)PciRead8(pciDevice->Host, device->Bus,
                                      device->Slot, device->Function, Register);
        } else if (width == 2) {
            *value = (size_t)PciRead16(pciDevice->Host, device->Bus,
                                       device->Slot, device->Function, Register);
        } else if (width == 4) {
            *value = (size_t)PciRead32(pciDevice->Host, device->Bus,
                                       device->Slot, device->Function, Register);
        } else {
            return OS_EINVALPARAMS;
        }
    } else {
        if (pciDevice->Host->DriversBlocked ||
            (pciDevice->Handler != NULL && pciDevice->Handler->BlockActivation)) {
            return OS_ENOTSUPPORTED;
        }

        if (width == 1) {
            PciWrite8(pciDevice->Host, device->Bus,
                      device->Slot, device->Function, Register, LOBYTE(*value));
        } else if (width == 2) {
            PciWrite16(pciDevice->Host, device->Bus,
                       device->Slot, device->Function, Register, LOWORD(*value));
        } else if (width == 4) {
            PciWrite32(pciDevice->Host, device->Bus,
                       device->Slot, device->Function, Register, LODWORD(*value));
        } else {
            return OS_EINVALPARAMS;
        }
    }
    return OS_EOK;
}

oserr_t
DMBusControl(
    _In_ BusDevice_t* device,
    _In_ struct OSIOCtlBusControl* request)
{
    oserr_t status;

    PciCriticalSectionEnter();
    status = __PciBusControl(device, request);
    PciCriticalSectionLeave();
    return status;
}

oserr_t
DmIoctlDeviceEx(
    _In_ BusDevice_t* device,
    _In_ int direction,
    _In_ unsigned int reg,
    _In_ size_t* value,
    _In_ size_t width)
{
    oserr_t status;

    // Keep lookup and access together so removing another host cannot invalidate
    // the flat list while a request is walking it.
    PciCriticalSectionEnter();
    status = __PciIoctlDevice(device, direction, reg, value, width);
    PciCriticalSectionLeave();
    return status;
}

static oserr_t
__PublishPciDevice(
    _In_ PciDevice_t* pciDevice)
{
    BusDevice_t* device;
    uuid_t       id;

    device = malloc(sizeof(BusDevice_t));
    if (device == NULL) {
        return OS_EOOM;
    }

    memset(device, 0, sizeof(BusDevice_t));
    device->Base.Id     = UUID_INVALID;
    device->Base.ParentId  = UUID_INVALID;
    device->Base.Length = sizeof(BusDevice_t);

    device->Base.VendorId  = pciDevice->Header->VendorId;
    device->Base.ProductId = pciDevice->Header->DeviceId;
    device->Base.Class     = PciToDevClass(pciDevice->Header->Class, pciDevice->Header->Subclass);
    device->Base.Subclass  = PciToDevSubClass(pciDevice->Header->Interface);
    device->Base.Identification.Description = strdup(PciToString(
            pciDevice->Header->Class,
            pciDevice->Header->Subclass,
            pciDevice->Header->Interface));

    device->IsPci = 1;
    device->Segment  = (unsigned int)pciDevice->Host->Segment;
    device->Bus      = pciDevice->Bus;
    device->Slot     = pciDevice->Slot;
    device->Function = pciDevice->Function;

    device->InterruptLine        = pciDevice->InterruptLine;
    device->InterruptPin         = (int)pciDevice->Header->InterruptPin;
    device->InterruptAcpiConform = pciDevice->AcpiConform;

    // Handle bars attached to device
    PciReadBars(pciDevice->Host, device, pciDevice->Header->HeaderType);

#ifdef __OSCONFIG_HAS_LEGACY_PCI
    // PCI - IDE Bar Fixup
    // From experience ide-bars don't always show up (ex: Oracle VM and Bochs)
    // but only the initial 4 bars don't, the BM bar
    // always seem to show up 
    if (pciDevice->Host->IoResourcePolicy == PciIoResourcePorts
        && pciDevice->Header->Class == PCI_CLASS_STORAGE
        && pciDevice->Header->Subclass == PCI_STORAGE_SUBCLASS_IDE) {
        if ((pciDevice->Header->Interface & 0x1) == 0) {
            if (device->IoSpaces[0].Type == DeviceIoInvalid) {
                CreateDevicePortIo(&device->IoSpaces[0], 0x1F0, 8);
            }
            if (device->IoSpaces[1].Type == DeviceIoInvalid) {
                CreateDevicePortIo(&device->IoSpaces[1], 0x3F6, 4);
            }
        }
        if ((pciDevice->Header->Interface & 0x4) == 0) {
            if (device->IoSpaces[2].Type == DeviceIoInvalid) {
                CreateDevicePortIo(&device->IoSpaces[2], 0x170, 8);
            }
            if (device->IoSpaces[3].Type == DeviceIoInvalid) {
                CreateDevicePortIo(&device->IoSpaces[3], 0x376, 4);
            }
        }
    }
#endif
    return DmDeviceCreate(
            &device->Base,
            DEVICE_REGISTER_FLAG_LOADDRIVER,
            &id
    );
}

void
PciPublishDevice(
    _In_ PciDevice_t* pciDevice)
{
    if (pciDevice->Host->DriversBlocked ||
        pciDevice->Handler != NULL) {
        return;
    }

    // Bridge or device? 
    // If a bridge, we keep iterating, device, load driver
    if (pciDevice->IsBridge) {
        foreach(element, &pciDevice->children) {
            PciPublishDevice(element->value);
        }
    } else {
        __PublishPciDevice(pciDevice);
    }
}
