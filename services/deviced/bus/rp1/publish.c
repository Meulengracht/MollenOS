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
 * Copies RP1 firmware descriptions into records owned by the device manager.
 * 
 */

#include <bus/rp1/rp1.h>
#include <bus/rp1/dma.h>
#include <bus/pci/device.h>
#include <core/publication.h>
#include <ddk/busdevice.h>
#include <ddk/interrupt.h>
#include <ddk/platformdevice.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

static oserr_t
__Rp1ProviderRetain(
    _In_ void* context)
{
    struct Rp1Child* child = context;
    struct Rp1Bus*   bus = child->Bus;
    oserr_t          status;

    // Retain the PCI owner first so its host, attachment and firmware survive
    // every child reference. Unattached firmware inventories cannot be providers.
    if (bus->Parent == NULL) {
        return OS_ENOTSUPPORTED;
    }

    status = PciDeviceRetain(bus->Parent);
    if (status != OS_EOK) {
        return status;
    }

    atomic_fetch_add(&bus->ProviderReferences, 1);
    return OS_EOK;
}

static void
__Rp1ProviderRelease(
    _In_ void* context)
{
    struct Rp1Child*  child = context;
    struct Rp1Bus*    bus = child->Bus;
    struct PciDevice* parent = bus->Parent;

    // Drop the child count before the parent reference, then touch neither
    // object again. The publication group may still need the child's saved ID;
    // its owner, not this callback, decides when to free the inventory.
    atomic_fetch_sub(&bus->ProviderReferences, 1);

    PciDeviceRelease(parent);
}

static oserr_t
__Rp1ProviderPrepareDma(
    _In_  void*                    context,
    _Out_ struct DmDmaDescription* description)
{
    struct Rp1Child*         child = context;
    struct PciDmaDescription configured;
    oserr_t                  status;

    // Follow this child's retained PCI owner so the result describes configured
    // hardware, rather than firmware's requested map or a different RP1 device.
    status = Rp1GetDmaDescription(
        child->Bus->Parent,
        child->Firmware.NodeOffset,
        &configured
    );
    if (status != OS_EOK) {
        return status;
    }

    // A later resource owner needs a registered host and a known cache policy.
    // Neither an anonymous host nor unknown cache behavior can support that use.
    if (configured.Host.HostId == UUID_INVALID) {
        return OS_EBUSY;
    }
    if (configured.CachePolicy == DmDmaCacheUnknown) {
        return OS_ENOTSUPPORTED;
    }

    description->HostId = configured.Host.HostId;
    description->CachePolicy = configured.CachePolicy;
    description->Map = configured.Map;
    return OS_EOK;
}

// Preparing a description supplies no register access or permission to run DMA.
static const struct DmDeviceProviderOperations g_rp1ChildProviderOperations = {
    .Retain = __Rp1ProviderRetain,
    .Release = __Rp1ProviderRelease,
    .PrepareDma = __Rp1ProviderPrepareDma
};

static oserr_t
__Rp1PublishChild(
    _InOut_ struct DmPublicationGroup* group,
    _In_    uuid_t                     parentId,
    _InOut_ struct Rp1Child*           child)
{
    struct DmDeviceRegistration registration = { .Kind = DmDeviceDescriptionPlatform };
    PlatformDevice_t*           device;
    const struct FdtRp1Device*  firmware = &child->Firmware;
    oserr_t                     status;

    // The destination arrays have fixed limits. Reject descriptions that do
    // not fit, so hardware names, register ranges, and interrupts are not lost.
    if (firmware->CompatibleLength > PLATFORM_DEVICE_MAX_COMPATIBLES ||
        firmware->RegisterCount > PLATFORM_DEVICE_MAX_REGISTERS ||
        firmware->InterruptCount > PLATFORM_DEVICE_MAX_INTERRUPTS) {
        return OS_ENOTSUPPORTED;
    }
    
    // The device manager needs its own record, separate from our list of RP1 devices.
    device = calloc(1, sizeof(*device));
    if (device == NULL) {
        return OS_EOOM;
    }
    device->Base.Length = sizeof(*device);
    device->Base.ParentId = parentId;
    
    // The name points into firmware data. Copy it into separately allocated
    // memory so this name remains available if the firmware mapping is released.
    device->Base.Identification.Description = strdup(firmware->Name);
    if (device->Base.Identification.Description == NULL) {
        free(device);
        return OS_EOOM;
    }
    
    device->Version = PLATFORM_DEVICE_VERSION;
    
    // Registers and interrupt numbers alone are not enough to run the device.
    // Block driver startup until interrupt delivery and direct memory access
    // (DMA), which lets the device access memory itself, have been set up.
    device->Pending = PLATFORM_DEVICE_PENDING_INTERRUPTS | PLATFORM_DEVICE_PENDING_DMA;
    device->FirmwareNode = firmware->NodeOffset;
    device->CompatibleLength = firmware->CompatibleLength;
    memcpy(device->Compatibles, firmware->Compatible, firmware->CompatibleLength);
    device->RegisterCount = firmware->RegisterCount;
    
    for (unsigned int i = 0; i < firmware->RegisterCount; i++) {
        device->Registers[i].Base = firmware->Registers[i].Base;
        device->Registers[i].Length = firmware->Registers[i].Length;
    }
    
    device->InterruptCount = firmware->InterruptCount;
    for (unsigned int i = 0; i < firmware->InterruptCount; i++) {
        device->Interrupts[i].Controller = firmware->Interrupts[i].Controller;
        device->Interrupts[i].Number = firmware->Interrupts[i].Number;
        device->Interrupts[i].Type = firmware->Interrupts[i].Type;
    }
    
    // Add the record without asking for a driver yet. The PCI code first adds
    // all device-manager entries. If a later addition fails, it can remove the
    // entries already added before any device drivers have started.
    registration.Description = &device->Base;
    registration.Provider.Operations = &g_rp1ChildProviderOperations;
    registration.Provider.Context = child;
    
    status = DmPublicationAdd(group, &registration, 1, &child->DeviceId);
    if (status != OS_EOK) {
        // The device manager did not accept the record, so free it and its name.
        free(device->Base.Identification.Description);
        free(device);
    }
    return status;
}

oserr_t
Rp1BusPublish(
    _InOut_ struct Rp1Bus*             bus,
    _In_    const struct PciDevice*    endpoint,
    _InOut_ struct DmPublicationGroup* group)
{
    struct Rp1Child* child;
    oserr_t          status;

    // Register the RP1 PCI device first so each child can refer to its device ID.
    if (endpoint->DeviceId == UUID_INVALID) {
        return OS_EINVALPARAMS;
    }
    
    // Only the endpoint that created this attachment can publish its providers.
    // Reparenting would make retained references protect the wrong PCI device.
    if (bus->Parent != endpoint) {
        return OS_EINVALPARAMS;
    }

    for (child = bus->Children; child != NULL; child = child->Next) {
        status = __Rp1PublishChild(group, endpoint->DeviceId, child);
        // The registration group tracks entries already added, so the PCI code
        // can remove them all on failure before any drivers have started.
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}
