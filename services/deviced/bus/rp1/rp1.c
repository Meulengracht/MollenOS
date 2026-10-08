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

#include <bus/rp1/rp1.h>
#include <bus/pci/device.h>
#include <bus/pci/function.h>
#include <bus/pci/host.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

/** Tracks the list being built, where to append the next child, and any error. */
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
        enumeration->Status = OS_EOOM;
        return;
    }
    
    // Copy the description, including its pointers into firmware data. The
    // PCI host keeps that data mapped for as long as this device list exists.
    child->Firmware = *firmware;
    
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

    // The caller needs a clear failure result even if validation or allocation
    // below fails before a device list has been created.
    if (host == NULL || bars == NULL || busOut == NULL) {
        return OS_EINVALPARAMS;
    }
    
    *busOut = NULL;
    
    // RP1's peripheral-control registers (APBS) are inside the PCI memory range
    // described by BAR1. Check that this range is assigned and large enough
    // before reading the descriptions of RP1's internal devices.
    if (bars[1].State != PciBarAssigned ||
        (bars[1].Space != 2 && bars[1].Space != 3) || bars[1].Size < RP1_PCIE_APBS_OFFSET + RP1_PCIE_APBS_LENGTH) {
        return OS_EINVALPARAMS;
    }
    
    bus = calloc(1, sizeof(struct Rp1Bus));
    if (bus == NULL) {
        return OS_EOOM;
    }
    bus->Host = host;

    // Keep the complete PCI address list. Each child's register addresses must
    // fit within memory assigned to this RP1 device.
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

    // A failed creation may call cleanup before a device list was allocated.
    if (bus == NULL) {
        return;
    }
    
    for (child = bus->Children; child != NULL; child = child->Next) {
        // The registration group still needs this stored ID to remove the
        // device-manager entry. Keep the list until every entry has been removed.
        if (child->DeviceId != UUID_INVALID) {
            return;
        }
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
    // Use this handler for the RP1 device itself; skip devices that connect PCI buses.
    return !device->IsBridge && device->Header->VendorId == RP1_VENDOR_ID &&
        device->Header->DeviceId == RP1_DEVICE_ID;
}

static oserr_t
__Rp1Attach(
    _In_  const struct PciDevice*            device,
    _In_  const struct PciFunctionResources* resources,
    _Out_ void**                             attachmentOut)
{
    const struct PciHostIdentification* identification = PciHostGetIdentification(device->Host);
    struct Rp1Bus*   bus;
    struct Rp1Child* child;
    oserr_t          status;

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
        return OS_ENOENT;
    }
    
    // Firmware does not identify RP1 by its PCI bus, slot, and function numbers.
    // Check RP1's expected location: the next bus after the host, slot 0,
    // function 0. This avoids selecting another device with the same IDs.
    if (device->Bus != (unsigned int)identification->BusStart + 1 ||
        device->Slot != 0 || device->Function != 0) {
        return OS_ENOENT;
    }
    
    // Build and check all child descriptions before returning their list to
    // the PCI code. Adding device-manager entries and finding drivers happen later.
    status = Rp1BusCreate(resources->Firmware, resources->Bars, &bus);
    if (status != OS_EOK) {
        WARNING("RP1 child enumeration failed (%u); assigned BARs and matching firmware required", status);
        return status;
    }
    
    *attachmentOut = bus;
    
    for (child = bus->Children; child != NULL; child = child->Next) {
        WARNING("RP1 %u:%u:%u.%u child %s (%s), %u registers, %u local interrupts; activation pending MSI/DMA",
            identification->Segment, device->Bus, device->Slot, device->Function,
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
    _In_    void*                      attachment,
    _In_    const struct PciDevice*    device,
    _InOut_ struct DmPublicationGroup* group)
{
    return Rp1BusPublish(attachment, device, group);
}

const struct PciFunctionHandler g_rp1PciHandler = {
    .Match = __Rp1Match,
    .BlockActivation = 1,
    .Attach = __Rp1Attach,
    .Destroy = __Rp1DestroyAttachment,
    .Publish = __Rp1PublishAttachment
};
