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
    struct DmDeviceRegistration registration = { .Kind = DmDeviceDescriptionPlatform };
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
    registration.Description = &device->Base;
    status = DmDeviceCreateWithProvider(&registration, 0, &child->DeviceId);
    if (status != OS_EOK) {
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
    oserr_t status;

    // PCI owns the endpoint parent. Remove only this attachment's children.
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
    bus->DeviceId = UUID_INVALID;
    return OS_EOK;
}

oserr_t
Rp1BusPublish(
    _InOut_ struct Rp1Bus* bus,
    _In_ const struct PciDevice* endpoint)
{
    struct Rp1Child* child;
    oserr_t status;

    if (endpoint->DeviceId == UUID_INVALID) {
        return OS_EINVALPARAMS;
    }
    bus->DeviceId = endpoint->DeviceId;
    for (child = bus->Children; child != NULL; child = child->Next) {
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
    oserr_t status;

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
