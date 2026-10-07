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
 * RP1 PCI function and its firmware-described internal bus.
 * 
 */

#include "rp1.h"
#include <bus/pci/bus.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

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

    // The firmware walk callback cannot return an error. Save allocation failure
    // here so Rp1BusCreate can report it after the walk finishes.
    if (enumeration->Status != OS_EOK) {
        return;
    }
    
    child = calloc(1, sizeof(struct Rp1Child));
    if (child == NULL) {
        enumeration->Status = OS_EOOM;
        return;
    }
    
    // Keep the decoded description, including its pointers into firmware. The
    // PCI host keeps that firmware mapped for as long as this inventory exists.
    child->Firmware = *firmware;
    
    // Append through the saved tail pointer so children stay in firmware order
    // without searching the list for its last entry each time.
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

    // The caller needs a clear failure result even if validation or allocation
    // below fails before an inventory has been created.
    if (host == NULL || bars == NULL || busOut == NULL) {
        return OS_EINVALPARAMS;
    }
    
    *busOut = NULL;
    
    // APBS registers are inside BAR1. Require an assigned memory BAR large enough
    // to contain them before using its resources to find firmware-described children.
    if (bars[1].State != PciBarAssigned ||
        (bars[1].Space != 2 && bars[1].Space != 3) || bars[1].Size < RP1_PCIE_APBS_OFFSET + RP1_PCIE_APBS_LENGTH) {
        return OS_EINVALPARAMS;
    }
    
    bus = calloc(1, sizeof(struct Rp1Bus));
    if (bus == NULL) {
        return OS_EOOM;
    }
    bus->Host = host;

    // Keep the complete PCI resource list because child addresses are checked
    // against the RP1 function's assigned memory ranges during firmware decoding.
    memcpy(bus->Bars, bars, sizeof(bus->Bars));
    enumeration.Bus = bus;
    enumeration.Tail = &bus->Children;
    enumeration.Status = OS_EOK;
    
    status = FdtEnumerateRp1Children(host, bars, __Rp1AddChild, &enumeration);
    if (status == OS_EOK) {
        status = enumeration.Status;
    }
    
    if (status != OS_EOK) {
        // Do not return a partial child list. Free every record created before
        // the bad description or allocation failure was found.
        Rp1BusDestroy(bus);
        return status;
    }
    
    *busOut = bus;
    return OS_EOK;
}

void
Rp1BusDestroy(
    _In_ struct Rp1Bus* bus)
{
    struct Rp1Child* child;

    if (bus == NULL) {
        return;
    }
    
    // Remove device-manager entries before freeing the inventory they refer to.
    // If removal fails, keep this state intact so the caller can retry later.
    if (Rp1BusUnpublish(bus) != OS_EOK) {
        return;
    }
    
    while (bus->Children != NULL) {
        child = bus->Children;
        bus->Children = child->Next;
        free(child);
    }
    free(bus);
}

static int
__Rp1Match(
    _In_ const struct PciDevice* device)
{
    // The handler belongs to the RP1 endpoint, not PCI bridges or other devices.
    return !device->IsBridge && device->Header->VendorId == RP1_VENDOR_ID &&
        device->Header->DeviceId == RP1_DEVICE_ID;
}

static oserr_t
__Rp1Attach(
    _In_  const struct PciDevice*            device,
    _In_  const struct PciFunctionResources* resources,
    _Out_ void**                             attachmentOut)
{
    struct Rp1Bus*   bus;
    struct Rp1Child* child;
    oserr_t          status;

    // Leave no attachment behind on any failure;
    *attachmentOut = NULL;

    // This driver understands the C0 register and firmware layout only.
    if (device->Header->Revision != RP1_REVISION_C0) {
        WARNING("RP1 revision %u is unsupported", device->Header->Revision);
        return OS_ENOTSUPPORTED;
    }
    
    // RP1's internal devices are described by firmware, so PCI identity alone
    // is not enough to build their resource list.
    if (resources->Firmware == NULL) {
        return OS_ENOENT;
    }
    
    // Firmware does not identify RP1 by its PCI bus, slot, and function numbers.
    // Accept only the expected function directly below this host, not another
    // device that happens to have the same vendor and device IDs.
    if (device->Bus != (unsigned int)device->Host->Identification.BusStart + 1 ||
        device->Slot != 0 || device->Function != 0) {
        return OS_ENOENT;
    }
    
    // Build and validate all child descriptions before handing the attachment
    // to PCI. Publication and driver matching happen later in separate steps.
    status = Rp1BusCreate(resources->Firmware, resources->Bars, &bus);
    if (status != OS_EOK) {
        WARNING("RP1 child enumeration failed (%u); assigned BARs and matching firmware required", status);
        return status;
    }
    
    *attachmentOut = bus;
    
    for (child = bus->Children; child != NULL; child = child->Next) {
        WARNING("RP1 %u:%u:%u.%u child %s (%s), %u registers, %u local interrupts; activation pending MSI/DMA",
            device->Host->Identification.Segment, device->Bus, device->Slot, device->Function,
            child->Firmware.Name, child->Firmware.Compatible,
            child->Firmware.RegisterCount, child->Firmware.InterruptCount);
    }
    return OS_EOK;
}

static void
__Rp1DestroyAttachment(
    _In_ void* attachment)
{
    Rp1BusDestroy(attachment);
}

static oserr_t
__Rp1PublishAttachment(
    _In_ void* attachment,
    _In_ const struct PciDevice* device)
{
    return Rp1BusPublish(attachment, device);
}

static oserr_t
__Rp1UnpublishAttachment(
    _In_ void* attachment)
{
    return Rp1BusUnpublish(attachment);
}

static oserr_t
__Rp1EnableBinding(
    _In_ void* attachment)
{
    return Rp1BusEnableBinding(attachment);
}

const struct PciFunctionHandler g_rp1PciHandler = {
    .Match = __Rp1Match,
    .BlockActivation = 1,
    .Attach = __Rp1Attach,
    .Destroy = __Rp1DestroyAttachment,
    .Publish = __Rp1PublishAttachment,
    .Unpublish = __Rp1UnpublishAttachment,
    .EnableBinding = __Rp1EnableBinding
};
