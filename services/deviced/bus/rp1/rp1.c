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
 * Finds RP1 as a PCI device and reads firmware descriptions of the hardware inside it.
 * 
 */

//#define __TRACE

#include <bus/rp1/rp1.h>
#include <bus/pci/device.h>
#include <bus/pci/function.h>
#include <bus/pci/host.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

// Track state during enumeration of RP1 children.
struct __Rp1Enumeration {
    struct Rp1Bus*    Bus;
    struct Rp1Child** Tail;
    oserr_t           Status;
};

static void
__Rp1AddChild(
    _In_    const struct FdtRp1Device* firmware,
    _InOut_ void*                      context)
{
    struct __Rp1Enumeration* enumeration = context;
    struct Rp1Child*         child;

    // This callback cannot return an error. Save any memory allocation failure
    // so Rp1BusCreate can report it after reading the firmware descriptions.
    if (enumeration->Status != OS_EOK) {
        return;
    }
    
    child = calloc(1, sizeof(struct Rp1Child));
    if (child == NULL) {
        ERROR("__Rp1AddChild: allocation failed for node %u", firmware->NodeOffset);
        enumeration->Status = OS_EOOM;
        return;
    }
    
    // Copy the description, including its pointers into firmware data. The
    // PCI host keeps that data mapped for as long as this device list exists.
    child->Firmware = *firmware;
    child->Bus = enumeration->Bus;
    
    // Tail points to where the next child belongs. Appending there keeps
    // firmware order without searching for the end of the list each time.
    *enumeration->Tail = child;
    enumeration->Tail = &child->Next;
    enumeration->Bus->ChildCount++;
}

oserr_t
Rp1BusCreate(
    _In_  const struct FdtPciHost* host,
    _In_  const struct PciBar*     bars,
    _Out_ struct Rp1Bus**          busOut)
{
    struct __Rp1Enumeration enumeration = { 0 };
    struct Rp1Bus*          bus;
    oserr_t                 status;
    oserr_t                 cleanupStatus;

    TRACE("Rp1BusCreate(host=%p)", host);
    // The caller needs a clear failure result even if validation or allocation
    // below fails before a device list has been created.
    if (host == NULL || bars == NULL || busOut == NULL) {
        ERROR("Rp1BusCreate: missing host, BARs or output");
        return OS_EINVALPARAMS;
    }
    
    *busOut = NULL;
    
    // RP1's peripheral-control registers (APBS) are inside the PCI memory range
    // described by BAR1. Check that this range is assigned and large enough
    // before reading the descriptions of RP1's internal devices.
    if (bars[1].State != PciBarAssigned ||
        (bars[1].Space != 2 && bars[1].Space != 3) || bars[1].Size < RP1_PCIE_APBS_OFFSET + RP1_PCIE_APBS_LENGTH) {
        ERROR("Rp1BusCreate: BAR1 does not contain assigned APBS memory");
        return OS_EINVALPARAMS;
    }
    
    bus = calloc(1, sizeof(struct Rp1Bus));
    if (bus == NULL) {
        ERROR("Rp1BusCreate: bus allocation failed");
        return OS_EOOM;
    }
    bus->Host = host;
    atomic_init(&bus->ProviderReferences, 0);

    // Keep the complete PCI address list. Each child's register addresses must
    // fit within memory assigned to this RP1 device.
    memcpy(bus->Bars, bars, sizeof(bus->Bars));
    enumeration.Bus = bus;
    enumeration.Tail = &bus->Children;
    enumeration.Status = OS_EOK;
    
    status = FdtEnumerateRp1Children(host, bars, __Rp1AddChild, &enumeration);
    if (status != OS_EOK) {
        WARNING("Rp1BusCreate: firmware enumeration failed (%u)", status);
        goto error;
    }

    status = enumeration.Status;
    if (status != OS_EOK) {
        ERROR("Rp1BusCreate: child inventory construction failed (%u)", status);
        goto error;
    }
    
    *busOut = bus;
    TRACE("Rp1BusCreate: created %u children", bus->ChildCount);
    return OS_EOK;

error:
    // Destroy the partial inventory before returning an error.
    cleanupStatus = Rp1BusDestroy(bus);
    if (cleanupStatus != OS_EOK) {
        ERROR("Rp1BusCreate: partial inventory cleanup failed (%u)", cleanupStatus);
    }
    return status;
}

oserr_t
Rp1BusDestroy(
    _In_ struct Rp1Bus* bus)
{
    struct Rp1Child* child;

    TRACE("Rp1BusDestroy(bus=%p)", bus);
    // A failed creation may call cleanup before a device list was allocated.
    if (bus == NULL) {
        return OS_EOK;
    }
    // A provider may outlive its registry ID. Keep its context and the borrowed
    // firmware pointers until that owner has also released its parent reference.
    if (atomic_load(&bus->ProviderReferences) != 0) {
        WARNING("Rp1BusDestroy: providers still retain bus %p", bus);
        return OS_EBUSY;
    }
    for (child = bus->Children; child != NULL; child = child->Next) {
        // The registration group still needs this stored ID to remove the
        // device-manager entry. Keep the list until every entry has been removed.
        if (child->DeviceId != UUID_INVALID) {
            WARNING("Rp1BusDestroy: child node %u is still published", child->Firmware.NodeOffset);
            return OS_EBUSY;
        }
    }
    
    while (bus->Children != NULL) {
        child = bus->Children;
        bus->Children = child->Next;
        free(child);
    }
    free(bus);
    return OS_EOK;
}

static int
__Rp1Match(
    _In_ const struct PciDevice* device)
{
    // Use this handler for the RP1 device itself; skip devices that connect PCI buses.
    return !device->IsBridge && device->Header->VendorId == RP1_VENDOR_ID &&
        device->Header->DeviceId == RP1_DEVICE_ID;
}

static oserr_t
__Rp1Attach(
    _In_  struct PciDevice*                  device,
    _In_  const struct PciFunctionResources* resources,
    _Out_ void**                             attachmentOut)
{
    const struct PciHostIdentification* identification = PciHostGetIdentification(device->Host);
    struct Rp1Bus*   bus;
    struct Rp1Child* child;
    oserr_t          status;

    TRACE("__Rp1Attach: %u:%u:%u.%u", identification->Segment,
        device->Bus, device->Slot, device->Function);
    // Return no RP1 device list if setup fails.
    *attachmentOut = NULL;

    // Only hardware revision C0 has the register and firmware formats this code supports.
    if (device->Header->Revision != RP1_REVISION_C0) {
        WARNING("RP1 revision %u is unsupported", device->Header->Revision);
        return OS_ENOTSUPPORTED;
    }
    
    // PCI vendor and device IDs identify RP1 but do not describe the hardware
    // inside it. Firmware must supply those devices' registers and interrupts.
    if (resources->Firmware == NULL) {
        WARNING("__Rp1Attach: RP1 has no firmware description");
        return OS_ENOENT;
    }
    
    // Firmware does not identify RP1 by its PCI bus, slot, and function numbers.
    // Check RP1's expected location: the next bus after the host, slot 0,
    // function 0. This avoids selecting another device with the same IDs.
    if (device->Bus != (unsigned int)identification->BusStart + 1 ||
        device->Slot != 0 || device->Function != 0) {
        WARNING("__Rp1Attach: RP1 is not at the supported host-relative PCI location");
        return OS_ENOENT;
    }
    
    // Build and check all child descriptions before returning their list to
    // the PCI code. Adding device-manager entries and finding drivers happen later.
    status = Rp1BusCreate(resources->Firmware, resources->Bars, &bus);
    if (status != OS_EOK) {
        WARNING("RP1 child enumeration failed (%u); assigned BARs and matching firmware required", status);
        return status;
    }
    
    // Record the association without retaining our owner permanently. Actual
    // provider users take references; a permanent one would prevent teardown.
    bus->Parent = device;
    *attachmentOut = bus;
    
    for (child = bus->Children; child != NULL; child = child->Next) {
        TRACE("RP1 %u:%u:%u.%u child %s (%s), %u registers, %u local interrupts; activation pending MSI/DMA",
            identification->Segment, device->Bus, device->Slot, device->Function,
            child->Firmware.Name, child->Firmware.Compatible,
            child->Firmware.RegisterCount, child->Firmware.InterruptCount);
    }
    return OS_EOK;
}

static oserr_t
__Rp1DestroyAttachment(
    _In_ void* attachment)
{
    oserr_t status;

    // Propagate busy to PCI so it cannot free our parent while we remain in use.
    status = Rp1BusDestroy(attachment);
    if (status != OS_EOK) {
        WARNING("__Rp1DestroyAttachment: attachment %p retained (%u)", attachment, status);
        return status;
    }
    return OS_EOK;
}

static oserr_t
__Rp1PublishAttachment(
    _In_    void*                      attachment,
    _In_    const struct PciDevice*    device,
    _InOut_ struct DmPublicationGroup* group)
{
    oserr_t status;

    status = Rp1BusPublish(attachment, device, group);
    if (status != OS_EOK) {
        WARNING("__Rp1PublishAttachment: attachment %p publication failed (%u)", attachment, status);
        return status;
    }
    return OS_EOK;
}

const struct PciFunctionHandler g_rp1PciHandler = {
    .Match = __Rp1Match,
    .BlockActivation = 1,
    .Attach = __Rp1Attach,
    .Destroy = __Rp1DestroyAttachment,
    .Publish = __Rp1PublishAttachment
};
