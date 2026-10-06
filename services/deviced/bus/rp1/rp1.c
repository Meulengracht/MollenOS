/** RP1 enumeration is separate from the generic PCI scanner. */
#include "rp1.h"
#include <bus/pci/bus.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

/** Carries allocation failures across the firmware walk's callback boundary. */
struct __Rp1Enumeration {
    struct Rp1Bus* Bus;
    struct Rp1Child** Tail;
    oserr_t Status;
};

static void
__Rp1AddChild(
    _In_ const struct FdtRp1Device* firmware,
    _InOut_ void* context)
{
    struct __Rp1Enumeration* enumeration = context;
    struct Rp1Child* child;

    if (enumeration->Status != OS_EOK) {
        return;
    }
    child = calloc(1, sizeof(*child));
    if (child == NULL) {
        enumeration->Status = OS_EOOM;
        return;
    }
    child->Firmware = *firmware;
    *enumeration->Tail = child;
    enumeration->Tail = &child->Next;
    enumeration->Bus->ChildCount++;
}

oserr_t
Rp1BusCreate(
    _In_ const struct FdtPciHost* host,
    _In_ const struct PciBar* bars,
    _Out_ struct Rp1Bus** busOut)
{
    struct __Rp1Enumeration enumeration = { 0 };
    struct Rp1Bus* bus;
    oserr_t status;

    if (host == NULL || bars == NULL || busOut == NULL) {
        return OS_EINVALPARAMS;
    }
    *busOut = NULL;
    if (bars[1].State != PciBarAssigned ||
        (bars[1].Space != 2 && bars[1].Space != 3) || bars[1].Size < RP1_PCIE_APBS_OFFSET + RP1_PCIE_APBS_LENGTH) {
        return OS_EINVALPARAMS;
    }
    bus = calloc(1, sizeof(*bus));
    if (bus == NULL) {
        return OS_EOOM;
    }
    bus->Host = host;
    memcpy(bus->Bars, bars, sizeof(bus->Bars));
    enumeration.Bus = bus;
    enumeration.Tail = &bus->Children;
    enumeration.Status = OS_EOK;
    status = FdtEnumerateRp1Children(host, bars, __Rp1AddChild, &enumeration);
    if (status == OS_EOK) {
        status = enumeration.Status;
    }
    if (status != OS_EOK) {
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
    Rp1BusUnpublish(bus);
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
    return !device->IsBridge && device->Header->VendorId == RP1_VENDOR_ID &&
        device->Header->DeviceId == RP1_DEVICE_ID;
}

static oserr_t
__Rp1Attach(
    _In_ const struct PciDevice* device,
    _In_ const struct PciFunctionResources* resources,
    _Out_ void** attachmentOut)
{
    struct Rp1Bus* bus;
    struct Rp1Child* child;
    oserr_t status;

    *attachmentOut = NULL;
    if (device->Header->Revision != RP1_REVISION_C0) {
        WARNING("RP1 revision %u is unsupported", device->Header->Revision);
        return OS_ENOTSUPPORTED;
    }
    if (resources->Firmware == NULL) {
        return OS_ENOENT;
    }
    // The firmware wrapper has no PCI reg identity. Bind it only to the
    // directly attached function, never to an unrelated downstream endpoint.
    if (device->Bus != (unsigned int)device->Host->Identification.BusStart + 1 ||
        device->Slot != 0 || device->Function != 0) {
        return OS_ENOENT;
    }
    status = Rp1BusCreate(resources->Firmware, resources->Bars, &bus);
    if (status != OS_EOK) {
        WARNING("RP1 child enumeration failed (%u); assigned BARs and matching firmware required", status);
        return status;
    }
    status = Rp1BusPublish(bus, device);
    if (status != OS_EOK) {
        WARNING("RP1 publication failed (%u)", status);
        Rp1BusDestroy(bus);
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

const struct PciFunctionHandler g_rp1PciHandler = {
    .Match = __Rp1Match,
    .BlockActivation = 1,
    .Attach = __Rp1Attach,
    .Destroy = __Rp1DestroyAttachment
};
