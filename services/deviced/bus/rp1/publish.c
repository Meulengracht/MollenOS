/**
 * Copyright 2026, Philip Meulengracht
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
 * Copies RP1 firmware descriptions into records owned by the device manager.
 * 
 */

#include "rp1.h"
#include <bus/pci/bus.h>
#include <devices.h>
#include <ddk/busdevice.h>
#include <ddk/interrupt.h>
#include <ddk/platformdevice.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

static oserr_t
__Rp1PublishChild(
    _In_    struct Rp1Bus*   bus,
    _InOut_ struct Rp1Child* child)
{
    struct DmDeviceRegistration registration = { .Kind = DmDeviceDescriptionPlatform };
    PlatformDevice_t* device;
    const struct FdtRp1Device* firmware = &child->Firmware;
    unsigned int i;
    oserr_t status;

    // The destination has fixed-size arrays. Reject descriptions that cannot
    // fit rather than silently truncating compatibility data or resource lists.
    if (firmware->CompatibleLength > PLATFORM_DEVICE_MAX_COMPATIBLES ||
        firmware->RegisterCount > PLATFORM_DEVICE_MAX_REGISTERS ||
        firmware->InterruptCount > PLATFORM_DEVICE_MAX_INTERRUPTS) {
        return OS_ENOTSUPPORTED;
    }
    
    device = calloc(1, sizeof(*device));
    if (device == NULL) {
        return OS_EOOM;
    }
    device->Base.Length = sizeof(*device);
    device->Base.ParentId = bus->DeviceId;
    
    // The source strings point into firmware data. Make an owned copy so this
    // device description does not depend on that memory remaining mapped.
    device->Base.Identification.Description = strdup(firmware->Name);
    if (device->Base.Identification.Description == NULL) {
        free(device);
        return OS_EOOM;
    }
    
    device->Version = PLATFORM_DEVICE_VERSION;
    
    // This record describes resources but does not set up DMA or interrupts.
    // Keep the driver from initializing hardware until those services are ready.
    device->Pending = PLATFORM_DEVICE_PENDING_INTERRUPTS | PLATFORM_DEVICE_PENDING_DMA;
    device->FirmwareNode = firmware->NodeOffset;
    device->CompatibleLength = firmware->CompatibleLength;
    memcpy(device->Compatibles, firmware->Compatible, firmware->CompatibleLength);
    device->RegisterCount = firmware->RegisterCount;
    
    for (i = 0; i < firmware->RegisterCount; i++) {
        device->Registers[i].Base = firmware->Registers[i].Base;
        device->Registers[i].Length = firmware->Registers[i].Length;
    }
    
    device->InterruptCount = firmware->InterruptCount;
    for (i = 0; i < firmware->InterruptCount; i++) {
        device->Interrupts[i].Controller = firmware->Interrupts[i].Controller;
        device->Interrupts[i].Number = firmware->Interrupts[i].Number;
        device->Interrupts[i].Type = firmware->Interrupts[i].Type;
    }
    
    // Add the record without asking for a driver yet. PCI first creates the full
    // device tree; if a later addition fails, it can remove this partial set
    // without any peripheral driver having started.
    registration.Description = &device->Base;
    
    status = DmDeviceCreateWithProvider(&registration, 0, &child->DeviceId);
    if (status != OS_EOK) {
        // Registration did not take ownership, so release both allocations here.
        free(device->Base.Identification.Description);
        free(device);
    }
    return status;
}

oserr_t
Rp1BusUnpublish(
    _InOut_ struct Rp1Bus* bus)
{
    struct Rp1Child* child;
    oserr_t          status;

    // The PCI function owns the parent entry. Remove only RP1's children, and
    // clear each saved ID only after removal succeeds so failures can be retried.
    for (child = bus->Children; child != NULL; child = child->Next) {
        if (child->DeviceId != UUID_INVALID) {
            status = DmDeviceDestroy(child->DeviceId);
            if (status != OS_EOK) {
                return status;
            }
            child->DeviceId = UUID_INVALID;
            child->BindingEnabled = 0;
        }
    }

    // Clear the parent ID only after every child has been removed successfully.
    bus->DeviceId = UUID_INVALID;
    return OS_EOK;
}

oserr_t
Rp1BusPublish(
    _InOut_ struct Rp1Bus*          bus,
    _In_    const struct PciDevice* endpoint)
{
    struct Rp1Child* child;
    oserr_t          status;

    // Every child needs a published parent ID before the device manager can add it.
    if (endpoint->DeviceId == UUID_INVALID) {
        return OS_EINVALPARAMS;
    }
    
    bus->DeviceId = endpoint->DeviceId;
    for (child = bus->Children; child != NULL; child = child->Next) {
        // Keep successful entries when retried; PCI can clean them up if a later
        // child fails, or continue publication without creating duplicates.
        if (child->DeviceId != UUID_INVALID) {
            continue;
        }

        status = __Rp1PublishChild(bus, child);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

oserr_t
Rp1BusEnableBinding(
    _InOut_ struct Rp1Bus* bus)
{
    struct Rp1Child* child;
    oserr_t          status;

    // Make children eligible for driver matching only after PCI has created the
    // complete tree. Remember each success so a later failure can be retried.
    for (child = bus->Children; child != NULL; child = child->Next) {
        if (child->BindingEnabled) {
            continue;
        }
        
        status = DmDeviceEnableDriverBinding(child->DeviceId);
        if (status != OS_EOK) {
            return status;
        }
        child->BindingEnabled = 1;
    }
    return OS_EOK;
}
