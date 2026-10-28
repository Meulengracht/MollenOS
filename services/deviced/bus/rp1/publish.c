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

//#define __TRACE

#include <ddk/types/interrupt-source.h>
#include <bus/rp1/rp1.h>
#include <bus/rp1/dma.h>
#include <bus/rp1/runtime-private.h>
#include <bus/pci/device.h>
#include <bus/pci/host-private.h>
#include <core/publication.h>
#include <ddk/devices/bus.h>
#include <ddk/interrupts/interrupt.h>
#include <ddk/types/pci-interrupt.h>
#include <ddk/devices/platform.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

static oserr_t
__Rp1ProviderAddReference(
    _In_ void* context)
{
    struct Rp1Child* child = context;
    struct Rp1Bus*   bus = child->Bus;
    oserr_t          status;

    // A child must be attached to a PCI device before it can be referenced.
    // Reference that device first to keep its host, attachment and firmware valid.
    if (bus->Parent == NULL) {
        WARNING("__Rp1ProviderAddReference: child %u has no PCI owner", child->Firmware.NodeOffset);
        return OS_ENOTSUPPORTED;
    }

    status = PciDeviceAddReference(bus->Parent);
    if (status != OS_EOK) {
        WARNING("__Rp1ProviderAddReference: child %u adding parent reference failed (%u)",
            child->Firmware.NodeOffset, status);
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

    // Decrease the child count before releasing the parent, then stop accessing
    // both objects. The publication group may still need the child's saved ID,
    // so the attachment owner decides when to free the child list.
    atomic_fetch_sub(&bus->ProviderReferences, 1);

    PciDeviceRelease(parent);
}

static oserr_t
__Rp1ProviderPrepareDma(
    _In_  void*                    context,
    _In_  uuid_t                   principal,
    _Out_ struct DmDmaDescription* description)
{
    struct Rp1Child*         child = context;
    struct PciDmaDescription configured;
    oserr_t                  status;

    (void)principal;
    TRACE("__Rp1ProviderPrepareDma(child=%u)", child->Firmware.NodeOffset);
    // Use this child's PCI device to read the DMA map configured for its hardware.
    status = Rp1GetDmaDescription(
        child->Bus->Parent,
        child->Firmware.NodeOffset,
        &configured
    );
    if (status != OS_EOK) {
        WARNING("__Rp1ProviderPrepareDma: child %u DMA description unavailable (%u)",
            child->Firmware.NodeOffset, status);
        return status;
    }

    // DMA setup requires a registered host and a known cache policy.
    if (configured.Host.HostId == UUID_INVALID) {
        WARNING("__Rp1ProviderPrepareDma: child %u host is not registered", child->Firmware.NodeOffset);
        return OS_EBUSY;
    }
    if (configured.CachePolicy == DmDmaCacheUnknown) {
        WARNING("__Rp1ProviderPrepareDma: child %u cache policy is unknown", child->Firmware.NodeOffset);
        return OS_ENOTSUPPORTED;
    }

    description->HostId = configured.Host.HostId;
    description->CachePolicy = configured.CachePolicy;
    description->Map = configured.Map;
    return OS_EOK;
}

static oserr_t
__Rp1QueryInterruptSupport(
    _In_    struct Rp1Child* child,
    _InOut_ void*            buffer,
    _In_    size_t           length)
{
    struct DeviceInterruptSourceQuery* query = buffer;
    struct DeviceInterruptSourceQuery  result = { 0 };
    oserr_t                            status = OS_EBUSY;

    if (query == NULL || length != sizeof(*query)) {
        return OS_EINVALPARAMS;
    }

    if (query->Kind || query->ControllerID) {
        return OS_EINVALPARAMS;
    }

    if (query->Line.Line || query->Line.Pin || query->Line.AcpiConform) {
        return OS_EINVALPARAMS;
    }

    // Index selects an interrupt from the child's firmware description.
    // libddk uses the PCI host's message controller to configure that source.
    usched_mtx_lock(&child->Bus->Lock);
    if (child->Bus->Ready && query->Index < child->Firmware.InterruptCount) {
        result.Index = query->Index;
        result.Kind = InterruptSourceMessage;
        result.ControllerID = child->Bus->Parent->Host->InterruptControllerId;
        *query = result;
        status = OS_EOK;
    } else if (child->Bus->Ready) {
        status = OS_ENOTSUPPORTED;
    }
    usched_mtx_unlock(&child->Bus->Lock);
    
    TRACE("__Rp1QueryInterruptSupport: device %u source %u query: %u",
          child->DeviceId, query->Index, status);
    return status;
}

static oserr_t
__Rp1ProviderControl(
    _In_    void*               context,
    _In_    uuid_t              principal,
    _In_    enum OSIOCtlRequest request,
    _InOut_ void*               buffer,
    _In_    size_t              length)
{
    struct Rp1Child* child = context;
    oserr_t          status;

    TRACE("__Rp1ProviderControl(device=%u, principal=%u, request=0x%x, length=%zu)",
          child->DeviceId, principal, request, length);
    
    if (request == OSIOCTLREQUEST_DEVICE_RESOURCES || request == OSIOCTLREQUEST_DEVICE_ACTIVATE) {
        usched_mtx_lock(&child->Bus->Lock);
        status = Rp1ChildResourceControl(child, request, buffer, length);
        usched_mtx_unlock(&child->Bus->Lock);
        return status;
    }

    if (request == OSIOCTLREQUEST_INTERRUPT_QUERY) {
        return __Rp1QueryInterruptSupport(context, buffer, length);
    }

    if (request != OSIOCTLREQUEST_INTERRUPT_CONTROL) {
        TRACE("__Rp1ProviderControl: device %u unsupported request 0x%x", child->DeviceId, request);
        return OS_ENOTSUPPORTED;
    }

    if (buffer == NULL || length != sizeof(struct DeviceInterruptSourceRequest)) {
        WARNING("__Rp1ProviderControl: device %u invalid control buffer or length %zu", child->DeviceId, length);
        return OS_EINVALPARAMS;
    }

    usched_mtx_lock(&child->Bus->Lock);
    status = Rp1RuntimeControl(context, principal, buffer);
    usched_mtx_unlock(&child->Bus->Lock);
    if (status != OS_EOK) {
        TRACE("__Rp1ProviderControl: Rp1RuntimeControl device %u, principal %u returned %u",
              child->DeviceId, principal, status);
    }
    return status;
}

// Preparing a description supplies no register access or permission to run DMA.
static const struct DmDeviceProviderOperations g_rp1ChildProviderOperations = {
    .AddReference = __Rp1ProviderAddReference,
    .Release = __Rp1ProviderRelease,
    .PrepareDma = __Rp1ProviderPrepareDma,
    .Control = __Rp1ProviderControl
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
    unsigned int                i;

    TRACE("__Rp1PublishChild(node=%u)", firmware->NodeOffset);
    
    // The destination arrays have fixed limits. Reject descriptions that do
    // not fit, so hardware names, register ranges, and interrupts are not lost.
    if (firmware->CompatibleLength > PLATFORM_DEVICE_MAX_COMPATIBLES ||
        firmware->RegisterCount > PLATFORM_DEVICE_MAX_REGISTERS ||
        firmware->InterruptCount > PLATFORM_DEVICE_MAX_INTERRUPTS) {
        WARNING("__Rp1PublishChild: node %u exceeds platform resource capacity", firmware->NodeOffset);
        return OS_ENOTSUPPORTED;
    }
    
    // Allocate the device manager's record separately from the RP1 child list.
    device = calloc(1, sizeof(PlatformDevice_t));
    if (device == NULL) {
        ERROR("__Rp1PublishChild: node %u device allocation failed", firmware->NodeOffset);
        return OS_EOOM;
    }
    device->Base.Length = sizeof(PlatformDevice_t);
    device->Base.ParentId = parentId;
    
    // The name points into firmware data. Copy it into separately allocated
    // memory so this name remains available if the firmware mapping is released.
    device->Base.Identification.Description = strdup(firmware->Name);
    if (device->Base.Identification.Description == NULL) {
        ERROR("__Rp1PublishChild: node %u description allocation failed", firmware->NodeOffset);
        free(device);
        return OS_EOOM;
    }
    
    device->Version = PLATFORM_DEVICE_VERSION;
    
    // The PCI publication group delays driver binding until hardware setup finishes.
    device->Pending = 0;
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
    
    // Add the record without asking for a driver yet. The PCI code first adds
    // all device-manager entries. If a later addition fails, it can remove the
    // entries already added before any device drivers have started.
    registration.Description = &device->Base;
    registration.Provider.Operations = &g_rp1ChildProviderOperations;
    registration.Provider.Context = child;
    
    status = DmPublicationAdd(group, &registration, 1, &child->DeviceId);
    if (status != OS_EOK) {
        WARNING("__Rp1PublishChild: node %u registration failed (%u)", firmware->NodeOffset, status);
        // The device manager did not accept the record, so free it and its name.
        free(device->Base.Identification.Description);
        free(device);
        return status;
    }
    TRACE("__Rp1PublishChild: node %u described; driver binding deferred", firmware->NodeOffset);
    return OS_EOK;
}

oserr_t
Rp1BusPublish(
    _InOut_ struct Rp1Bus*             bus,
    _In_    const struct PciDevice*    endpoint,
    _InOut_ struct DmPublicationGroup* group)
{
    struct Rp1Child* child;
    oserr_t          status;

    TRACE("Rp1BusPublish(bus=%p)", bus);
    // Register the RP1 PCI device first so each child can refer to its device ID.
    if (endpoint->DeviceId == UUID_INVALID) {
        WARNING("Rp1BusPublish: parent endpoint is not registered");
        return OS_EINVALPARAMS;
    }
    
    // Only the endpoint that created this attachment can publish its children.
    // Changing the parent would make existing references protect the wrong PCI device.
    if (bus->Parent != endpoint) {
        WARNING("Rp1BusPublish: endpoint does not own this RP1 attachment");
        return OS_EINVALPARAMS;
    }

    for (child = bus->Children; child != NULL; child = child->Next) {
        status = __Rp1PublishChild(group, endpoint->DeviceId, child);
        // The registration group tracks entries already added, so the PCI code
        // can remove them all on failure before any drivers have started.
        if (status != OS_EOK) {
            WARNING("Rp1BusPublish: stopped at child %u (%u)", child->Firmware.NodeOffset, status);
            return status;
        }
    }
    return OS_EOK;
}
