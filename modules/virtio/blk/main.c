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
#include "private.h"

#include <ddk/convert.h>
#include <ddk/utils.h>
#include <io.h>
#include <ioset.h>
#include <os/types/device.h>
#include <stdlib.h>
#include <os/usched/mutex.h>
#include <os/usched/job.h>
#include <time.h>
#include <ctt_driver_service_server.h>
#include <ctt_storage_service_server.h>

extern gracht_server_t* __crt_get_module_server(void);

static list_t g_devices = LIST_INIT;
static list_t g_cleanupDevices = LIST_INIT;
static struct usched_mtx g_lock;
static uuid_t g_cleanupJob;
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

    // MSI-X uses one vector for queue and configuration changes, so we must
    // check both on every event. For MSI/INTx, reading the ISR byte also
    // acknowledges the interrupt.
    if (resource->MsixEnabled) {
        status = VIRTIO_ISR_QUEUE_INTERRUPT | VIRTIO_ISR_CONFIG_INTERRUPT;
    } else {
        status = (uint8_t)interruptTable->ReadIoSpace(
            ioSpace,
            resource->IsrOffset,
            sizeof(uint8_t)
        );
    }

    if (status == 0) {
        return IRQSTATUS_NOT_HANDLED;
    }
    // Only the handler's code pages are mapped into kernel space. We cannot
    // access string literals in module rodata here, so log the saved status
    // from OnEvent.
    atomic_fetch_or(&resource->PendingStatus, status);
    interruptTable->EventSignal(resourceTable->HandleResource);
    return IRQSTATUS_HANDLED;
}

static VirtioBlkDevice_t*
__FindStorageDevice(
    _In_ uuid_t deviceId)
{
    // The module stores devices by PCI ID. Storage requests use the logical
    // device ID assigned by this driver, so we must check each descriptor.
    foreach (element, &g_devices) {
        VirtioBlkDevice_t* device = element->value;
        if (device->Descriptor.DeviceID == deviceId) {
            return device;
        }
    }
    return NULL;
}

void
VirtioBlkDeferDestroy(
    _In_ VirtioBlkDevice_t* device)
{
    // The caller must remove the device from public lookup first. Keep it on
    // the cleanup list so the monitor or shutdown can retry the release.
    list_append(&g_cleanupDevices, &device->Header);
}

static void
__RetryCleanup(void)
{
    size_t             count = g_cleanupDevices.count;
    VirtioBlkDevice_t* device;

    // Cleanup can yield. Remove each device before trying to destroy it, and
    // try each device once per pass. The module mutex prevents concurrent
    // cleanup. List operations release their spinlock before DMA calls can yield.
    while (count-- != 0 && g_cleanupDevices.head != NULL) {
        device = g_cleanupDevices.head->value;
        list_remove(&g_cleanupDevices, &device->Header);
        if (VirtioBlkDeviceDestroy(device) != OS_EOK) {
            VirtioBlkDeferDestroy(device);
        }
    }
}

static void
__CleanupMonitor(
    _In_ void* argument,
    _In_ void* cancellationToken)
{
    struct timespec    wakeUp;
    element_t*         element;
    VirtioBlkDevice_t* device;
    _CRT_UNUSED(argument);

    // Once the interrupt has been handled, a deferred response may have no
    // further event to trigger cleanup. Use one service job to retry pending
    // cleanup, leaving active requests in the queue.
    while (!usched_is_cancelled(cancellationToken)) {
        timespec_get(&wakeUp, TIME_UTC);
        wakeUp.tv_sec++;
        usched_job_sleep(&wakeUp);
        usched_mtx_lock(&g_lock);
        _foreach(element, &g_devices) {
            device = element->value;
            if (device->CleanupRequests != NULL) {
                (void)VirtioBlkRetryRequests(device);
            }
        }
        __RetryCleanup();
        usched_mtx_unlock(&g_lock);
    }
}

oserr_t
OnLoad(void)
{
    TRACE("OnLoad()");
    usched_mtx_init(&g_lock, USCHED_MUTEX_PLAIN);
    g_cleanupJob = usched_job_queue(__CleanupMonitor, NULL);
    if (g_cleanupJob == UUID_INVALID) {
        return OS_EOOM;
    }

    // deviced uses the driver protocol to attach and control PCI devices.
    // filed uses the storage protocol for disk requests.
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
    VirtioBlkDevice_t* device;
    int exitCode;

    TRACE("OnUnload()");
    if (g_cleanupJob != UUID_INVALID) {
        usched_job_cancel(g_cleanupJob);
        if (usched_job_join(g_cleanupJob, &exitCode) != 0) {
            ERROR("virtio-blk cleanup worker join failed");
            return;
        }
        g_cleanupJob = UUID_INVALID;
    }

    usched_mtx_lock(&g_lock);
    while (g_devices.head != NULL) {
        device = g_devices.head->value;
        list_remove(&g_devices, &device->Header);
        VirtioBlkDeferDestroy(device);
    }
    __RetryCleanup();
    usched_mtx_unlock(&g_lock);
}

static oserr_t
__OnEvent(
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

    // The device may be removed while this event waits for the module lock.
    // Look it up by the increasing storage ID, so queued events do not need
    // a pointer to the device.
    device = __FindStorageDevice((uuid_t)(uintptr_t)event->data.context);
    if (device == NULL) {
        return OS_ENOENT;
    }

    // Read the event counter before taking the pending ISR bits. The interrupt
    // handler can add new bits while we process this event.
    if (read(device->EventDescriptor, &signal, sizeof(signal)) != sizeof(signal)) {
        ERROR("OnEvent failed to consume the Virtio interrupt event");
        return OS_EUNKNOWN;
    }

    status = (uint8_t)atomic_exchange(
        &device->InterruptResource.PendingStatus,
        0
    );
    TRACE("OnEvent interrupt status=0x%x", status);

    // Process completed requests before configuration changes. If the device
    // needs a reset, we then cancel only the requests that are still pending.
    if (status & VIRTIO_ISR_QUEUE_INTERRUPT) {
        queueStatus = VirtioBlkDeviceHandleInterrupt(device);
    }
    if (status & VIRTIO_ISR_CONFIG_INTERRUPT) {
        configStatus = VirtioBlkDeviceHandleConfigurationChange(device);
    }
    return configStatus != OS_EOK ? configStatus : queueStatus;
}

static void
__ctt_driver_register_device_invocation(
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

    __RetryCleanup();

    // If cleanup failed, the old transport still has mappings for this PCI
    // device. We must finish releasing them before initializing it again.
    if (list_find_value(&g_devices, (void*)(uintptr_t)busDevice->Base.Id) != NULL ||
        list_find_value(&g_cleanupDevices, (void*)(uintptr_t)busDevice->Base.Id) != NULL) {
        WARNING("virtio-blk device %u is already registered or awaiting cleanup", busDevice->Base.Id);
        free(busDevice);
        return;
    }

    // After VirtioBlkDeviceCreate, the caller must not free busDevice, even on failure.
    // Assign the storage ID here so filed can use it throughout the device's life.
    blockDevice = VirtioBlkDeviceCreate(busDevice, g_nextDeviceId++);
    if (blockDevice == NULL) {
        ERROR("failed to initialize virtio block device");
        return;
    }

    // filed can call ctt_storage_stat as soon as the device is registered.
    // Add the initialized device to our list first, so we can find its storage ID.
    list_append(&g_devices, &blockDevice->Header);
    if (VirtioBlkDeviceRegisterStorage(blockDevice) != OS_EOK) {
        list_remove(&g_devices, &blockDevice->Header);
        if (VirtioBlkDeviceDestroy(blockDevice) != OS_EOK) {
            VirtioBlkDeferDestroy(blockDevice);
        }
        ERROR("failed to register virtio block device with filed");
    }
}

static oserr_t
__OnUnregister(
    _In_ Device_t* device)
{
    VirtioBlkDevice_t* blockDevice;
    oserr_t status;
    TRACE("OnUnregister(device=%u)", device->Id);

    blockDevice = list_find_value(
        &g_devices,
        (void*)(uintptr_t)device->Id
    );

    if (blockDevice == NULL) {
        blockDevice = list_find_value(&g_cleanupDevices, (void*)(uintptr_t)device->Id);
        if (blockDevice == NULL) {
            return OS_ENOENT;
        }
        list_remove(&g_cleanupDevices, &blockDevice->Header);
    } else {
        list_remove(&g_devices, &blockDevice->Header);
    }
    
    status = VirtioBlkDeviceDestroy(blockDevice);
    if (status != OS_EOK) {
        VirtioBlkDeferDestroy(blockDevice);
    }
    return status;
}

static void
__ctt_driver_ioctl_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           deviceId,
    _In_ const unsigned int     request,
    _In_ const uint8_t*         out,
    _In_ const uint32_t         outCount)
{
    // I/O requirements, transfer and stat requests use the logical storage ID.
    // The device list is indexed by PCI ID, so use the storage ID lookup.
    VirtioBlkDevice_t* device = __FindStorageDevice(deviceId);

    (void)out;
    (void)outCount;
    if (device == NULL) {
        ctt_driver_ioctl_response(message, NULL, 0, OS_ENOENT);
        return;
    }
    if ((enum OSIOCtlRequest)request == OSIOCTLREQUEST_IO_REQUIREMENTS) {
        // The device DMA domain handles mapping and staging. Filesystems can
        // supply committed SHM without restricting its physical addresses.
        struct OSIOCtlRequestRequirements requirements = {
            .BufferAlignment = 0,
            .Conformity = OSMEMORYCONFORMITY_NONE
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

static void
__ctt_storage_transfer_invocation(
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

    // If the request is queued, we send the response after it completes.
    // Respond here only if validation or setup fails.
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

static void
__ctt_storage_stat_invocation(
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

oserr_t
OnEvent(
    _In_ struct ioset_event* event)
{
    oserr_t status;

    // Service calls can yield. Keep the device and DMA state locked until done.
    usched_mtx_lock(&g_lock);
    status = __OnEvent(event);
    usched_mtx_unlock(&g_lock);
    return status;
}

oserr_t
OnUnregister(
    _In_ Device_t* device)
{
    oserr_t status;

    // Service calls can yield. Keep the device and DMA state locked until done.
    usched_mtx_lock(&g_lock);
    status = __OnUnregister(device);
    usched_mtx_unlock(&g_lock);
    return status;
}

void
ctt_driver_register_device_invocation(
    _In_ struct gracht_message* message,
    _In_ const struct sys_device* device)
{
    // Service calls can yield. Keep the device and DMA state locked until done.
    usched_mtx_lock(&g_lock);
    __ctt_driver_register_device_invocation(message, device);
    usched_mtx_unlock(&g_lock);
}

void
ctt_driver_ioctl_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           deviceId,
    _In_ const unsigned int     request,
    _In_ const uint8_t*         out,
    _In_ const uint32_t         outCount)
{
    // Service calls can yield. Keep the device and DMA state locked until done.
    usched_mtx_lock(&g_lock);
    __ctt_driver_ioctl_invocation(message, deviceId, request, out, outCount);
    usched_mtx_unlock(&g_lock);
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
    // Service calls can yield. Keep the device and DMA state locked until done.
    usched_mtx_lock(&g_lock);
    __ctt_storage_transfer_invocation(message, deviceId, direction, sectorLow, sectorHigh, bufferId, offset, sectorCount);
    usched_mtx_unlock(&g_lock);
}

void
ctt_storage_stat_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           deviceId)
{
    // Service calls can yield. Keep the device and DMA state locked until done.
    usched_mtx_lock(&g_lock);
    __ctt_storage_stat_invocation(message, deviceId);
    usched_mtx_unlock(&g_lock);
}
