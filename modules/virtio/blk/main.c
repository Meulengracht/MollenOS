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
 *
 * MollenOS MCore - Virtio Driver
 * - Contains the implementation of a shared virtio driver
 *   for all the virtio devices
 * Follows the specification here:
 *   https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html
 */

#define __TRACE
#include "virtio-blk.h"

#include <ddk/convert.h>
#include <ddk/utils.h>
#include <io.h>
#include <ioset.h>
#include <os/types/device.h>
#include <stdlib.h>
#include <ctt_driver_service_server.h>
#include <ctt_storage_service_server.h>

extern gracht_server_t* __crt_get_module_server(void);

static list_t g_devices = LIST_INIT;
static uuid_t g_nextDeviceId = 1;

irqstatus_t
OnFastInterrupt(
    _In_ InterruptFunctionTable_t* interruptTable,
    _In_ InterruptResourceTable_t* resourceTable)
{
    VirtioBlkInterruptResource_t* resource =
            (VirtioBlkInterruptResource_t*)INTERRUPT_RESOURCE(resourceTable, 0);
    DeviceIo_t* ioSpace = INTERRUPT_IOSPACE(resourceTable, 0);
    uint8_t status;

    // Reading the Virtio ISR byte acknowledges legacy INTx. The fast handler
    // only preserves the cause and wakes the module loop; queue processing and
    // protocol responses are not safe in interrupt context.
    status = (uint8_t)interruptTable->ReadIoSpace(
        ioSpace,
        resource->IsrOffset,
        sizeof(uint8_t)
    );

    if (status == 0) {
        return IRQSTATUS_NOT_HANDLED;
    }
    // Only the handler's code pages are remapped into kernel space. String
    // literals in module rodata are not accessible through that mapping;
    // log the saved status from OnEvent instead.
    atomic_fetch_or(&resource->PendingStatus, status);
    interruptTable->EventSignal(resourceTable->HandleResource);
    return IRQSTATUS_HANDLED;
}

static VirtioBlkDevice_t*
__FindStorageDevice(
    _In_ uuid_t deviceId)
{
    // Storage IDs are driver-local logical-device IDs. The list itself is
    // owned by the module lifecycle and stores devices by their PCI ID.
    foreach (element, &g_devices) {
        VirtioBlkDevice_t* device = element->value;
        if (device->Descriptor.DeviceID == deviceId) {
            return device;
        }
    }
    return NULL;
}

static void
__DestroyDevice(
    _In_ element_t* element,
    _In_ void*      context)
{
    (void)context;
    VirtioBlkDeviceDestroy(element->value);
}

oserr_t
OnLoad(void)
{
    TRACE("OnLoad()");

    // The driver protocol is used by deviced for PCI attachment and control;
    // the storage contract is the data path consumed by filed.
    gracht_server_register_protocol(
        __crt_get_module_server(),
        &ctt_driver_server_protocol
    );
    gracht_server_register_protocol(
        __crt_get_module_server(),
        &ctt_storage_server_protocol
    );
    return OS_EOK;
}

void
OnUnload(void)
{
    TRACE("OnUnload()");
    list_clear(&g_devices, __DestroyDevice, NULL);
}

oserr_t
OnEvent(
    _In_ struct ioset_event* event)
{
    VirtioBlkDevice_t* device;
    unsigned int signal;
    uint8_t status;
    oserr_t queueStatus = OS_EOK;
    oserr_t configStatus = OS_EOK;

    if (event == NULL || !(event->events & IOSETSYN)) {
        return OS_ENOENT;
    }
    device = event->data.context;

    // Consume the event counter before taking the accumulated ISR bits. New
    // interrupts can safely OR additional causes while this event is handled.
    if (read(device->EventDescriptor, &signal, sizeof(signal)) != sizeof(signal)) {
        ERROR("OnEvent failed to consume the Virtio interrupt event");
        return OS_EUNKNOWN;
    }

    status = (uint8_t)atomic_exchange(
        &device->InterruptResource.PendingStatus,
        0
    );
    TRACE("OnEvent interrupt status=0x%x", status);

    // Handle queue completions before a simultaneous configuration change. If
    // the device requests reset, recovery then cancels only work still pending.
    if (status & VIRTIO_ISR_QUEUE_INTERRUPT) {
        queueStatus = VirtioBlkDeviceHandleInterrupt(device);
    }
    if (status & VIRTIO_ISR_CONFIG_INTERRUPT) {
        configStatus = VirtioBlkDeviceHandleConfigurationChange(device);
    }
    return configStatus != OS_EOK ? configStatus : queueStatus;
}

void
ctt_driver_register_device_invocation(
    _In_ struct gracht_message* message,
    _In_ const struct sys_device* device)
{
    BusDevice_t* busDevice = (BusDevice_t*)from_sys_device(device);
    VirtioBlkDevice_t* blockDevice;
    TRACE("ctt_driver_register_device_invocation()");

    (void)message;
    if (busDevice == NULL) {
        ERROR("Failed to convert the registered Virtio PCI device");
        return;
    }

    // VirtioBlkDeviceCreate consumes busDevice on both success and failure.
    // Allocate the storage ID here so filed sees a stable driver-local ID.
    blockDevice = VirtioBlkDeviceCreate(busDevice, g_nextDeviceId++);
    if (blockDevice == NULL) {
        ERROR("failed to initialize virtio block device");
        return;
    }

    // filed can issue ctt_storage_stat immediately after registration. Publish
    // the fully initialized device locally first so that callback can resolve
    // the new driver-local storage ID.
    list_append(&g_devices, &blockDevice->Header);
    if (VirtioBlkDeviceRegisterStorage(blockDevice) != OS_EOK) {
        list_remove(&g_devices, &blockDevice->Header);
        VirtioBlkDeviceDestroy(blockDevice);
        ERROR("failed to register virtio block device with filed");
    }
}

oserr_t
OnUnregister(
    _In_ Device_t* device)
{
    VirtioBlkDevice_t* blockDevice;
    TRACE("OnUnregister(device=%u)", device->Id);

    blockDevice = list_find_value(
        &g_devices,
        (void*)(uintptr_t)device->Id
    );

    if (blockDevice == NULL) {
        return OS_ENOENT;
    }
    list_remove(&g_devices, &blockDevice->Header);
    VirtioBlkDeviceDestroy(blockDevice);
    return OS_EOK;
}

void
ctt_driver_ioctl_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           deviceId,
    _In_ const unsigned int     request,
    _In_ const uint8_t*         out,
    _In_ const uint32_t         outCount)
{
    VirtioBlkDevice_t* device;

    device = list_find_value(
        &g_devices,
        (void*)(uintptr_t)deviceId
    );

    (void)out;
    (void)outCount;
    if (device == NULL) {
        ctt_driver_ioctl_response(message, NULL, 0, OS_ENOENT);
        return;
    }
    if ((enum OSIOCtlRequest)request == OSIOCTLREQUEST_IO_REQUIREMENTS) {
        // The block data path submits the caller's physical SG entries directly.
        // Keep buffers below 4 GiB to match the transport's queue DMA policy.
        struct OSIOCtlRequestRequirements requirements = {
            .BufferAlignment = 0,
            .Conformity = OSMEMORYCONFORMITY_BITS32
        };
        ctt_driver_ioctl_response(
            message,
            (uint8_t*)&requirements,
            sizeof(requirements),
            OS_EOK
        );
        return;
    }
    ctt_driver_ioctl_response(message, NULL, 0, OS_ENOTSUPPORTED);
}

void
ctt_storage_transfer_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           deviceId,
    _In_ const enum sys_transfer_direction direction,
    _In_ const unsigned int     sectorLow,
    _In_ const unsigned int     sectorHigh,
    _In_ const uuid_t           bufferId,
    _In_ const size_t           offset,
    _In_ const size_t           sectorCount)
{
    VirtioBlkDevice_t* device = __FindStorageDevice(deviceId);
    UInteger64_t sector;
    oserr_t oserr;

    if (device == NULL) {
        ctt_storage_transfer_response(message, OS_ENOENT, 0);
        return;
    }
    if (direction != SYS_TRANSFER_DIRECTION_READ &&
        direction != SYS_TRANSFER_DIRECTION_WRITE) {
        ctt_storage_transfer_response(message, OS_EINVALPARAMS, 0);
        return;
    }
    sector.u.LowPart = sectorLow;
    sector.u.HighPart = sectorHigh;

        // A successful queue operation defers the response. Only synchronous
        // validation/setup failures are answered from this invocation.
    oserr = VirtioBlkDeviceTransfer(
        device,
        message,
        direction == SYS_TRANSFER_DIRECTION_READ ?
            __STORAGE_OPERATION_READ : __STORAGE_OPERATION_WRITE,
        sector.QuadPart,
        bufferId,
        offset,
        sectorCount
    );
    if (oserr != OS_EOK) {
        ctt_storage_transfer_response(message, oserr, 0);
    }
}

void
ctt_storage_stat_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           deviceId)
{
    struct sys_disk_descriptor descriptor = { 0 };
    VirtioBlkDevice_t* device = __FindStorageDevice(deviceId);
    oserr_t oserr = OS_ENOENT;

    if (device != NULL) {
        to_sys_disk_descriptor_dkk(&device->Descriptor, &descriptor);
        oserr = OS_EOK;
    }
    ctt_storage_stat_response(message, oserr, &descriptor);
}

void ctt_driver_get_device_protocols_invocation(
    struct gracht_message* message,
    const uuid_t           deviceId)
{
    (void)message;
    (void)deviceId;
}

void sys_device_event_protocol_device_invocation(void) { }
void sys_device_event_device_update_invocation(void) { }
