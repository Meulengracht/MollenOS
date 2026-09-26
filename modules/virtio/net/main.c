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
 * Virtio network device main module.
 * 
 * Handles PCI attachment, protocol advertisement, and interrupt dispatch for the Virtio network device.
 * 
 */

#include <ddk/convert.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <io.h>
#include <internal/_utils.h>
#include <ioset.h>
#include <os/usched/mutex.h>
#include <stddef.h>
#include <stdlib.h>

#include <ctt_driver_service_server.h>
#include <sys_device_service_client.h>

#include "virtio-net.h"

extern gracht_server_t*
__crt_get_module_server(void);
extern int
__crt_get_server_iod(void);
extern void
__crt_module_set_send_timeout(uint32_t milliseconds);

static list_t            g_devices = LIST_INIT;
static struct usched_mtx g_lock;

/**
 * @brief Helper functions
 */

void VirtioNetLock(void) {
    usched_mtx_lock(&g_lock);
}

void VirtioNetUnlock(void) {
    usched_mtx_unlock(&g_lock);
}

gracht_server_t* VirtioNetServer(void) {
    return __crt_get_module_server();
}

uint64_t VirtioNetGeneration(void) {
    return GetNativeHandle(__crt_get_server_iod());
}

VirtioNetDevice_t*
VirtioNetFindDevice(
    _In_ uuid_t deviceId)
{
    return list_find_value(&g_devices, (void*)(uintptr_t)deviceId);
}

VirtioNetDevice_t*
VirtioNetFindSession(
    _In_ const struct gracht_message*         message,
    _In_ const struct ctt_netadapter_session* identity)
{
    foreach (element, &g_devices) {
        VirtioNetDevice_t*  device = element->value;
        VirtioNetSession_t* session = &device->Session;
        
        if (session->Active && session->Owner == message->client &&
            identity->id == session->Identity.id &&
            identity->generation == session->Identity.generation) {
            return device;
        }
    }
    return NULL;
}

bool
VirtioNetWasClosed(
    _In_ const struct gracht_message*         message,
    _In_ const struct ctt_netadapter_session* identity)
{
    foreach (element, &g_devices) {
        VirtioNetDevice_t* device = element->value;
        for (uint32_t i = 0; i < device->ClosedCount; ++i) {
            VirtioNetClosedSession_t* closed = &device->Closed[i];
            if (closed->Owner == message->client && identity->id == closed->Identity.id &&
                identity->generation == closed->Identity.generation) {
                return true;
            }
        }
    }
    return false;
}

irqstatus_t
OnFastInterrupt(
    _In_ InterruptFunctionTable_t* interruptTable,
    _In_ InterruptResourceTable_t* resourceTable)
{
    VirtioNetInterruptResource_t* resource =
            (VirtioNetInterruptResource_t*)INTERRUPT_RESOURCE(resourceTable, 0);
    DeviceIo_t* ioSpace = INTERRUPT_IOSPACE(resourceTable, 0);
    uint8_t     status;
            
    
    status = (uint8_t)interruptTable->ReadIoSpace(
        ioSpace,
        resource->IsrOffset,
        sizeof(uint8_t)
    );
    if (!status) {
        return IRQSTATUS_NOT_HANDLED;
    }
    
    // Kernel-remapped interrupt code may only use the explicitly registered
    // resources.
    atomic_fetch_or(&resource->PendingStatus, status);
    interruptTable->EventSignal(resourceTable->HandleResource);
    return IRQSTATUS_HANDLED;
}

oserr_t
OnLoad(void)
{
    usched_mtx_init(&g_lock, USCHED_MUTEX_PLAIN);
    
    // Configure before registering clients: event delivery must never block
    // queue completion processing on a full netd inbox indefinitely.
    __crt_module_set_send_timeout(1);
    
    if (gracht_server_register_protocol(VirtioNetServer(), &ctt_driver_server_protocol) ||
        gracht_server_register_protocol(VirtioNetServer(), &ctt_netadapter_server_protocol)) {
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

static void
__DestroyDevice(
    _In_ element_t* element,
    _In_ void*      context)
{
    (void)context;
    oserr_t status = VirtioNetDeviceDestroy(element->value);
    if (status != OS_EOK) {
        ERROR("virtio-net teardown retains DMA storage: %u", status);
    }
}

void
OnUnload(void)
{
    VirtioNetLock();
    list_clear(&g_devices, __DestroyDevice, NULL);
    VirtioNetUnlock();
}

oserr_t
OnEvent(
    _In_ struct ioset_event* event)
{
    VirtioNetDevice_t* device;
    unsigned int       signal;

    if (event == NULL || !(event->events & IOSETSYN)) {
        return OS_ENOENT;
    }
    
    device = event->data.context;
    
    VirtioNetLock();
    if (read(device->EventDescriptor, &signal, sizeof(signal)) == sizeof(signal)) {
        uint8_t pending = (uint8_t)atomic_exchange(&device->InterruptResource.PendingStatus, 0);
        if (pending & VIRTIO_ISR_QUEUE_INTERRUPT) {
            VirtioNetPoll(device);
        }
        if (pending & VIRTIO_ISR_CONFIG_INTERRUPT) {
            uint64_t state;
            oserr_t  status;
            
            status = VirtioPciRegionRead(
                &device->Transport.Regions[VIRTIO_PCI_CAP_COMMON_CFG - 1],
                offsetof(VirtioPciCommonConfiguration_t, DeviceStatus),
                sizeof(uint8_t),
                &state
            );
            if (status == OS_EOK &&
                (state & (VIRTIO_STATUS_DEVICE_NEEDS_RESET | VIRTIO_STATUS_FAILED))) {
                status = OS_EDEVFAULT;
            } else if (status == OS_EOK) {
                status = VirtioNetReadConfiguration(device);
            }
            
            // If reading the device status or the configuration fails, 
            // we fault the device.
            if (status != OS_EOK) {
                VirtioNetFault(device, status);
            }
        }
    }
    VirtioNetUnlock();
    
    // Recognized events stay consumed even on a device error; never hand the
    // interrupt descriptor to Gracht as though it were an IPC link.
    return OS_EOK;
}

void
ctt_driver_register_device_invocation(
    _In_ struct gracht_message*   message,
    _In_ const struct sys_device* description)
{
    VirtioNetDevice_t* device;
    BusDevice_t*       busDevice;
    (void)message;

    // Validate the variable wire array before the shared converter copies it
    // into the fixed PCI BAR array. Only PCI bus devices belong to this module.
    if (description->content_type != SYS_DEVICE_CONTENT_BUS ||
        description->content.bus.ios_count > __DEVICEMANAGER_MAX_IOSPACES) {
        return;
    }
    
    busDevice = (BusDevice_t*)from_sys_device(description);
    if (busDevice == NULL) {
        return;
    }
    if (busDevice->Base.Length != sizeof(BusDevice_t)) {
        VirtioNetBusDeviceDestroy(busDevice);
        return;
    }
    
    VirtioNetLock();

    device = VirtioNetFindDevice(busDevice->Base.Id);
    if (device != NULL) {
        // Device with this ID already exists, so we cleanup and return.
        VirtioNetBusDeviceDestroy(busDevice);
        VirtioNetUnlock();
        return;
    }
    
    device = VirtioNetDeviceCreate(busDevice);
    if (device) {
        list_append(&g_devices, &device->Header);
    }
    VirtioNetUnlock();
}

void
ctt_driver_get_device_protocols_invocation(
    _In_ struct gracht_message* message,
    _In_ uuid_t                 deviceId)
{
    VirtioNetDevice_t* device;

    VirtioNetLock();
    device = VirtioNetFindDevice(deviceId);
    if (device == NULL) {
        VirtioNetUnlock();
        return;
    }
    
    ctt_driver_event_device_protocol_single(
        VirtioNetServer(),
        message->client,
        deviceId,
        "netadapter",
        SERVICE_CTT_NETADAPTER_ID
    );
    VirtioNetUnlock();
}

void
ctt_driver_ioctl_invocation(
    struct gracht_message* message,
    uuid_t                 deviceId,
    unsigned int           request,
    const uint8_t*         data,
    uint32_t               count)
{
    (void)deviceId;
    (void)request;
    (void)data;
    (void)count;
    ctt_driver_ioctl_response(message, NULL, 0, OS_ENOTSUPPORTED);
}

oserr_t
OnUnregister(
    _In_ Device_t* descriptor)
{
    VirtioNetDevice_t* device;
    oserr_t            status = OS_ENOENT;

    VirtioNetLock();
    device = VirtioNetFindDevice(descriptor->Id);
    if (device == NULL) {
        VirtioNetUnlock();
        return status;
    }

    // Unlink first, but put the still-live entry back if reset fails. A
    // failed detach must not lose the only reference to DMA-owned storage.
    list_remove(&g_devices, &device->Header);
    status = VirtioNetDeviceDestroy(device);
    if (status != OS_EOK) {
        list_append(&g_devices, &device->Header);
    }
    VirtioNetUnlock();
    return status;
}

/** 
 * The module runtime links the device client for startup notification; this
 * controller does not consume discovery events itself. Keep typed no-op hooks.
 */
void
sys_device_event_protocol_device_invocation(
    gracht_client_t* client,
    uuid_t           deviceId,
    uuid_t           driverId,
    uint8_t          protocolId)
{
    (void)client;
    (void)deviceId;
    (void)driverId;
    (void)protocolId;
}

void
sys_device_event_device_update_invocation(
    gracht_client_t* client,
    uuid_t           deviceId,
    uint8_t          connected)
{
    (void)client;
    (void)deviceId;
    (void)connected;
}
