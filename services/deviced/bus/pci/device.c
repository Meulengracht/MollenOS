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

#include "private.h"
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

    // Reject requests for devices that are no longer in the PCI inventory.
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
    oserr_t      status;
    unsigned int i;

    device = malloc(sizeof(BusDevice_t));
    if (device == NULL) {
        return OS_EOOM;
    }

    memset(device, 0, sizeof(BusDevice_t));
    device->Base.Id     = UUID_INVALID;
    device->Base.ParentId = pciDevice->Parent->DeviceId;
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
    device->Segment  = (unsigned int)pciDevice->Host->Identification.Segment;
    device->Bus      = pciDevice->Bus;
    device->Slot     = pciDevice->Slot;
    device->Function = pciDevice->Function;

    device->InterruptLine        = pciDevice->InterruptLine;
    device->InterruptPin         = (int)pciDevice->Header->InterruptPin;
    device->InterruptAcpiConform = pciDevice->AcpiConform;

    if (device->Base.Identification.Description == NULL) {
        free(device);
        return OS_EOOM;
    }
    if (!pciDevice->IsBridge && pciDevice->Handler == NULL && !pciDevice->Host->DriversBlocked) {
        // Only endpoints that may use a generic driver expose their BAR
        // (base address register) mappings.
        PciRegisterBars(pciDevice->Host, device, pciDevice->Resources.Bars);

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
    }
    
    status = DmDeviceCreate(&device->Base, 0, &pciDevice->DeviceId);
    if (status != OS_EOK) {
        for (i = 0; i < 6; i++) {
            if (device->IoSpaces[i].Type != DeviceIoInvalid) {
                DestroyDeviceIo(&device->IoSpaces[i]);
            }
        }
        free(device->Base.Identification.Description);
        free(device);
        return status;
    }
    
    memcpy(pciDevice->PublishedIo, device->IoSpaces, sizeof(pciDevice->PublishedIo));
    return OS_EOK;
}

static oserr_t
__PublishPciRoot(
    _In_ PciDevice_t* root)
{
    Device_t* description;
    oserr_t   status;

    description = calloc(1, sizeof(*description));
    if (description == NULL) {
        return OS_EOOM;
    }

    description->Length = sizeof(*description);
    description->Identification.Description = strdup("PCI host");
    if (description->Identification.Description == NULL) {
        free(description);
        return OS_EOOM;
    }

    status = DmDeviceCreate(description, 0, &root->DeviceId);
    if (status != OS_EOK) {
        free(description->Identification.Description);
        free(description);
    }
    return status;
}

static oserr_t
__PciStagePublication(
    _In_ PciDevice_t* device)
{
    oserr_t status;

    if (device->DeviceId == UUID_INVALID) {
        if (device->Parent != NULL && device->Parent->DeviceId == UUID_INVALID) {
            return OS_EINVALPARAMS;
        }
        status = device->Parent == NULL ? __PublishPciRoot(device) : __PublishPciDevice(device);
        if (status != OS_EOK) {
            return status;
        }
    }
    
    if (device->Attachment != NULL && device->Handler->Publish != NULL) {
        status = device->Handler->Publish(device->Attachment, device);
        if (status != OS_EOK) {
            return status;
        }
    }
    
    foreach (element, &device->children) {
        status = __PciStagePublication(element->value);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

static oserr_t
__PciEnableBinding(
    _In_ PciDevice_t* device)
{
    oserr_t status;

    if (!device->BindingEnabled) {
        if (device->Attachment != NULL && device->Handler->EnableBinding != NULL) {
            status = device->Handler->EnableBinding(device->Attachment);
            if (status != OS_EOK) {
                return status;
            }
        } else if (device->Parent != NULL && !device->IsBridge &&
            device->Handler == NULL && !device->Host->DriversBlocked) {
            status = DmDeviceEnableDriverBinding(device->DeviceId);
            if (status != OS_EOK) {
                return status;
            }
        }
        device->BindingEnabled = 1;
    }
    
    foreach (element, &device->children) {
        status = __PciEnableBinding(element->value);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

oserr_t
PciUnpublishDevice(
    _In_ PciDevice_t* device)
{
    oserr_t status;

    foreach (element, &device->children) {
        status = PciUnpublishDevice(element->value);
        if (status != OS_EOK) {
            return status;
        }
    }
    
    if (device->Attachment != NULL && device->Handler->Unpublish != NULL) {
        status = device->Handler->Unpublish(device->Attachment);
        if (status != OS_EOK) {
            return status;
        }
    }
    
    if (device->DeviceId != UUID_INVALID) {
        status = DmDeviceDestroy(device->DeviceId);
        if (status != OS_EOK) {
            return status;
        }
        device->DeviceId = UUID_INVALID;
    }
    
    for (unsigned int i = 0; i < 6; i++) {
        if (device->PublishedIo[i].Type != DeviceIoInvalid) {
            DestroyDeviceIo(&device->PublishedIo[i]);
            memset(&device->PublishedIo[i], 0, sizeof(DeviceIo_t));
        }
    }
    device->BindingEnabled = 0;
    return OS_EOK;
}

oserr_t
PciPublishDevice(
    _In_ PciDevice_t* device)
{
    oserr_t status;
    oserr_t cleanup;

    // Published subtrees may already have active clients. A repeated call only
    // retries binding, never rolls back descriptions beneath those clients.
    if (device->BindingEnabled) {
        return __PciEnableBinding(device);
    }
    
    status = __PciStagePublication(device);
    if (status != OS_EOK) {
        cleanup = PciUnpublishDevice(device);
        return cleanup == OS_EOK ? status : cleanup;
    }
    
    return __PciEnableBinding(device);
}
