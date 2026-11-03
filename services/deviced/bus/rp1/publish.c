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
#include <bus/rp1/ethernet.h>
#include <bus/rp1/dma.h>
#include <bus/rp1/runtime-private.h>
#include <bus/pci/device.h>
#include <bus/pci/host-private.h>
#include <core/publication.h>
#include <ddk/devices/bus.h>
#include <ddk/interrupts/interrupt.h>
#include <ddk/types/interrupt-pci.h>
#include <ddk/devices/platform.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

static oserr_t
__Rp1BusAddReference(
    _In_ void* context)
{
    struct Rp1Child* child = context;
    struct Rp1Bus*   bus = child->Bus;
    oserr_t          status;

    // A child must be attached to a PCI device before it can be referenced.
    // Reference that device first to keep its host, attachment and firmware valid.
    if (bus->Parent == NULL) {
        WARNING("__Rp1BusAddReference: child %u has no PCI owner", child->Firmware.NodeOffset);
        return OS_ENOTSUPPORTED;
    }

    status = PciDeviceAddReference(bus->Parent);
    if (status != OS_EOK) {
        ERROR("__Rp1BusAddReference: child %u adding parent reference failed (%u)",
              child->Firmware.NodeOffset, status);
        return status;
    }

    atomic_fetch_add(&bus->HostBusReferences, 1);
    return OS_EOK;
}

static void
__Rp1BusRelease(
    _In_ void* context)
{
    struct Rp1Child*  child = context;
    struct Rp1Bus*    bus = child->Bus;
    struct PciDevice* parent = bus->Parent;

    // Decrease the child count before releasing the parent. Neither object may
    // be accessed afterwards. Leave the child list for attachment cleanup, since
    // the publication group may still need the saved device IDs.
    atomic_fetch_sub(&bus->HostBusReferences, 1);

    PciDeviceRelease(parent);
}

static oserr_t
__Rp1BusPrepareDma(
    _In_  void*                    context,
    _In_  uuid_t                   principal,
    _Out_ struct DmDmaDescription* description)
{
    struct Rp1Child*         child = context;
    struct PciDmaDescription configured;
    oserr_t                  status;

    TRACE("__Rp1BusPrepareDma(child=%u)", child->Firmware.NodeOffset);
    
    // Use this child's PCI device to read the DMA map configured for its hardware.
    status = Rp1GetDmaDescription(
        child->Bus->Parent,
        child->Firmware.NodeOffset,
        &configured
    );
    if (status != OS_EOK) {
        WARNING("__Rp1BusPrepareDma: child %u DMA description unavailable (%u)",
            child->Firmware.NodeOffset, status);
        return status;
    }

    // DMA setup requires a registered host and a known cache policy.
    if (configured.Host.HostId == UUID_INVALID) {
        WARNING("__Rp1BusPrepareDma: child %u host is not registered", child->Firmware.NodeOffset);
        return OS_EBUSY;
    }

    if (configured.CachePolicy == DmDmaCacheUnknown) {
        WARNING("__Rp1BusPrepareDma: child %u cache policy is unknown", child->Firmware.NodeOffset);
        return OS_ENOTSUPPORTED;
    }

    // Hold the bus lock while checking readiness and adding the DMA reference.
    // Stop must not disable the Ethernet clocks between these two operations.
    usched_mtx_lock(&child->Bus->Lock);
    if (!child->Bus->Ready ||
        (child->Bus->Runtime != NULL && child->Bus->Runtime->Quiescing)) {
        status = OS_EBUSY;
    } else {
        status = Rp1EthernetStatus(child, principal);
    }

    if (status == OS_EOK && child->Resources.DmaReferences == UINT32_MAX) {
        status = OS_EOVERFLOW;
    }

    if (status == OS_EOK) {
        child->Resources.DmaReferences++;
    }

    usched_mtx_unlock(&child->Bus->Lock);
    if (status != OS_EOK) {
        return status;
    }

    description->HostId = configured.Host.HostId;
    description->CachePolicy = configured.CachePolicy;
    description->Map = configured.Map;
    return OS_EOK;
}

static void
__Rp1BusReleaseDma(
    _In_ void* context)
{
    struct Rp1Child* child = context;

    // Call once for each successful PrepareDma. Releasing the final reference
    // allows a later Ethernet stop. This call does not change hardware.
    usched_mtx_lock(&child->Bus->Lock);
    child->Resources.DmaReferences--;
    usched_mtx_unlock(&child->Bus->Lock);
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

    // Only Index may be set on entry. Reject nonzero result fields.
    // Until the bus is ready, return OS_EBUSY so the driver can retry the query.
    if (query == NULL || length != sizeof(*query)) {
        ERROR("__Rp1QueryInterruptSupport: device %u invalid query buffer %p or length %zu",
              child->DeviceId, buffer, length);
        return OS_EINVALPARAMS;
    }

    if (query->Kind || query->ControllerID) {
        ERROR("__Rp1QueryInterruptSupport: device %u source %u has kind %u or controller %u set",
              child->DeviceId, query->Index, query->Kind, query->ControllerID);
        return OS_EINVALPARAMS;
    }

    if (query->Line.Line || query->Line.Pin || query->Line.AcpiConform) {
        ERROR("__Rp1QueryInterruptSupport: device %u source %u has line %u, pin %u or ACPI flags %u set",
              child->DeviceId, query->Index, query->Line.Line,
              query->Line.Pin, query->Line.AcpiConform);
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
__Rp1BusControl(
    _In_    void*               context,
    _In_    uuid_t              principal,
    _In_    enum OSIOCtlRequest request,
    _InOut_ void*               buffer,
    _In_    size_t              length)
{
    struct Rp1Child* child = context;
    oserr_t          status;

    TRACE("__Rp1BusControl(device=%u, principal=%u, request=0x%x, length=%zu)",
          child->DeviceId, principal, request, length);

    // Resource, start/stop and Ethernet requests require the bus lock here.
    // Interrupt requests take the same lock in their separate paths below.
    if (request == OSIOCTLREQUEST_DEVICE_RESOURCES || request == OSIOCTLREQUEST_DEVICE_ACTIVATE ||
        request == OSIOCTLREQUEST_ETHERNET_CONFIG || request == OSIOCTLREQUEST_ETHERNET_PHY_RESET) {
        // Resource setup, PHY reset and stop share saved register state. Keep
        // them serialized with DMA and interrupt requests on the same bus.
        usched_mtx_lock(&child->Bus->Lock);
        status = Rp1ChildResourceControl(child, principal, request, buffer, length);
        usched_mtx_unlock(&child->Bus->Lock);
        if (status != OS_EOK) {
            if (status == OS_ENOTSUPPORTED || status == OS_EBUSY) {
                TRACE("__Rp1BusControl: device %u resource request 0x%x unavailable (%u)",
                      child->DeviceId, request, status);
            } else {
                ERROR("__Rp1BusControl: device %u resource request 0x%x failed (%u)",
                      child->DeviceId, request, status);
            }
            return status;
        }
        return OS_EOK;
    }

    if (request == OSIOCTLREQUEST_INTERRUPT_QUERY) {
        status = __Rp1QueryInterruptSupport(context, buffer, length);
        if (status != OS_EOK) {
            return status;
        }
        return OS_EOK;
    }

    if (request != OSIOCTLREQUEST_INTERRUPT_CONTROL) {
        TRACE("__Rp1BusControl: device %u unsupported request 0x%x", child->DeviceId, request);
        return OS_ENOTSUPPORTED;
    }

    if (buffer == NULL || length != sizeof(struct DeviceInterruptSourceRequest)) {
        WARNING("__Rp1BusControl: device %u invalid control buffer or length %zu", child->DeviceId, length);
        return OS_EINVALPARAMS;
    }

    usched_mtx_lock(&child->Bus->Lock);
    status = Rp1RuntimeControl(context, principal, buffer);
    usched_mtx_unlock(&child->Bus->Lock);
    if (status != OS_EOK) {
        TRACE("__Rp1BusControl: Rp1RuntimeControl device %u, principal %u returned %u",
              child->DeviceId, principal, status);
    }
    return status;
}

// Preparing a description supplies no register access or permission to run DMA.
static const DmDeviceBusOperations_t g_rp1ChildBusOperations = {
    .AddReference = __Rp1BusAddReference,
    .Release = __Rp1BusRelease,
    .PrepareDma = __Rp1BusPrepareDma,
    .ReleaseDma = __Rp1BusReleaseDma,
    .Control = __Rp1BusControl
};

static oserr_t
__Rp1PublishChild(
    _InOut_ struct DmPublicationGroup* group,
    _In_    uuid_t                     parentId,
    _InOut_ struct Rp1Child*           child)
{
    DmDeviceRegistration_t registration = { .Kind = DmDeviceDescriptionPlatform };
    PlatformDevice_t*           device;
    const struct FdtRp1Device*  firmware = &child->Firmware;
    oserr_t                     status;
    unsigned int                i;

    TRACE("__Rp1PublishChild(node=%u)", firmware->NodeOffset);
    
    // The full description must fit in the destination arrays. Reject larger
    // descriptions before copying hardware names, register ranges or interrupts.
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
    
    // Copy the name from firmware so the device record remains valid after
    // the firmware mapping is released.
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
    
    // Defer driver binding until the PCI code has added every device record.
    // If an addition fails, the records can be removed before drivers start.
    registration.Description = &device->Base;
    registration.Bus.Operations = &g_rp1ChildBusOperations;
    registration.Bus.Context = child;
    
    status = DmPublicationAdd(group, &registration, 1, &child->DeviceId);
    if (status != OS_EOK) {
        ERROR("__Rp1PublishChild: node %u registration failed (%u)",
              firmware->NodeOffset, status);
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
    
    // The endpoint must match the attachment's parent. Existing references keep
    // that PCI device available while the children are in use.
    if (bus->Parent != endpoint) {
        WARNING("Rp1BusPublish: endpoint does not own this RP1 attachment");
        return OS_EINVALPARAMS;
    }

    for (child = bus->Children; child != NULL; child = child->Next) {
        status = __Rp1PublishChild(group, endpoint->DeviceId, child);
        // The registration group tracks entries already added, so the PCI code
        // can remove them all on failure before any drivers have started.
        if (status != OS_EOK) {
            TRACE("Rp1BusPublish: stopped at child %u (%u)", child->Firmware.NodeOffset, status);
            return status;
        }
    }
    return OS_EOK;
}
