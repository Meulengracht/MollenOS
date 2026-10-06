/** Publication transfers owned descriptions to deviced, never borrowed DT pointers. */
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
    _In_ struct Rp1Bus* bus,
    _InOut_ struct Rp1Child* child)
{
    PlatformDevice_t* device;
    const struct FdtRp1Device* firmware = &child->Firmware;
    unsigned int i;
    oserr_t status;

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
    device->Base.Identification.Description = strdup(firmware->Name);
    if (device->Base.Identification.Description == NULL) {
        free(device);
        return OS_EOOM;
    }
    device->Version = PLATFORM_DEVICE_VERSION;
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
    // Keep matching off until the entire parent/child set exists. An allocation
    // failure can then roll back without having started any peripheral driver.
    status = DmDeviceCreate(&device->Base, 0, &child->DeviceId);
    if (status != OS_EOK) {
        free(device->Base.Identification.Description);
        free(device);
    }
    return status;
}

void
Rp1BusUnpublish(
    _InOut_ struct Rp1Bus* bus)
{
    struct Rp1Child* child;

    // The PCI teardown caller has already stopped this host's clients.
    for (child = bus->Children; child != NULL; child = child->Next) {
        if (child->DeviceId != UUID_INVALID) {
            DmDeviceDestroy(child->DeviceId);
            child->DeviceId = UUID_INVALID;
        }
    }
    if (bus->DeviceId != UUID_INVALID) {
        DmDeviceDestroy(bus->DeviceId);
        bus->DeviceId = UUID_INVALID;
    }
}

oserr_t
Rp1BusPublish(
    _InOut_ struct Rp1Bus* bus,
    _In_ const struct PciDevice* endpoint)
{
    BusDevice_t* parent;
    struct Rp1Child* child;
    oserr_t status;

    if (bus->DeviceId != UUID_INVALID) {
        return OS_EOK;
    }
    parent = calloc(1, sizeof(*parent));
    if (parent == NULL) {
        return OS_EOOM;
    }
    parent->Base.Length = sizeof(*parent);
    parent->Base.VendorId = RP1_VENDOR_ID;
    parent->Base.ProductId = RP1_DEVICE_ID;
    parent->Base.Identification.Description = strdup("RP1 internal bus");
    if (parent->Base.Identification.Description == NULL) {
        free(parent);
        return OS_EOOM;
    }
    parent->IsPci = 1;
    parent->Segment = endpoint->Host->Identification.Segment;
    parent->Bus = endpoint->Bus;
    parent->Slot = endpoint->Slot;
    parent->Function = endpoint->Function;
    parent->InterruptLine = INTERRUPT_NONE;
    parent->InterruptPin = INTERRUPT_NONE;
    status = DmDeviceCreate(&parent->Base, 0, &bus->DeviceId);
    if (status != OS_EOK) {
        free(parent->Base.Identification.Description);
        free(parent);
        return status;
    }
    for (child = bus->Children; child != NULL; child = child->Next) {
        status = __Rp1PublishChild(bus, child);
        if (status != OS_EOK) {
            Rp1BusUnpublish(bus);
            return status;
        }
    }
    for (child = bus->Children; child != NULL; child = child->Next) {
        DmDeviceEnableDriverBinding(child->DeviceId);
    }
    return OS_EOK;
}
