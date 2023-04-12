/**
 * MollenOS
 *
 * Copyright 2021, Philip Meulengracht
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
 * Usb Hub Device Driver
 */

//#define __TRACE

#include <ddk/utils.h>
#include <ddk/convert.h>
#include <ds/list.h>
#include <ioset.h>
#include <os/usched/job.h>
#include <os/device.h>
#include "hub.h"

#include <ctt_driver_service_server.h>
#include <ctt_usbhub_service_server.h>
#include <ctt_usbhost_service_client.h>
#include <internal/_utils.h>

extern gracht_server_t* __crt_get_module_server(void);

static list_t g_devices = LIST_INIT;

HubDevice_t*
HubDeviceGet(
        _In_ uuid_t deviceId)
{
    return list_find_value(&g_devices, (void*)(uintptr_t)deviceId);
}

oserr_t OnEvent(struct ioset_event* event)
{
    return OS_ENOTSUPPORTED;
}

oserr_t OnLoad(void)
{
    // Register supported server protocols
    gracht_server_register_protocol(__crt_get_module_server(), &ctt_driver_server_protocol);
    gracht_server_register_protocol(__crt_get_module_server(), &ctt_usbhub_server_protocol);

    // register supported client protocols
    gracht_client_register_protocol(GetGrachtClient(), &ctt_usbhost_client_protocol);

    return UsbInitialize();
}

static void
DestroyElement(
        _In_ element_t* Element,
        _In_ void*      Context)
{
    HubDeviceDestroy(Element->value);
}

void
OnUnload(void)
{
    list_clear(&g_devices, DestroyElement, NULL);
    UsbCleanup();
}

void ctt_driver_register_device_invocation(struct gracht_message* message, const struct sys_device* sysDevice)
{
    UsbDevice_t* usbDevice = (UsbDevice_t*)from_sys_device(sysDevice);
    HubDevice_t* hubDevice;

    hubDevice = HubDeviceCreate(usbDevice);
    if (!hubDevice) {
        ERROR("ctt_driver_register_device_invocation failed to create hub device");
        return;
    }
    list_append(&g_devices, &hubDevice->Header);
}

oserr_t OnUnregister(
    _In_ Device_t* device)
{
    HubDevice_t* hubDevice = HubDeviceGet(device->Id);
    if (hubDevice == NULL) {
        return OS_ENOENT;
    }

    list_remove(&g_devices, &hubDevice->Header);
    HubDeviceDestroy(hubDevice);
    return OS_EOK;
}

void ctt_driver_get_device_protocols_invocation(struct gracht_message* message, const uuid_t deviceId)
{
    ctt_driver_event_device_protocol_single(__crt_get_module_server(), message->client, deviceId,
                                            "usbhub\0\0\0\0\0\0\0\0\0\0",
                                            SERVICE_CTT_USBHUB_ID);
}

void ctt_usbhost_event_transfer_status_invocation(gracht_client_t* client, const uuid_t transferId,
                                                  const enum USBTransferCode status, const size_t dataIndex)
{
    HubDevice_t* hubDevice = NULL;
    TRACE("ctt_usbhost_event_transfer_status_callback(event->status %u, event->bytes_transferred %" PRIuIN ")",
          event->status, event->bytes_transferred);

    foreach(element, &g_devices) {
        HubDevice_t* i = element->value;
        if (i->TransferId == transferId) {
            hubDevice = i;
            break;
        }
    }

    if (hubDevice) {
        if (status == USBTRANSFERCODE_STALL) {
            WARNING("ctt_usbhost_event_transfer_status_callback stall, trying to fix");
            // we must clear stall condition and reset endpoint
            UsbClearFeature(&hubDevice->Base->DeviceContext, USBPACKET_DIRECTION_ENDPOINT,
                            USB_ENDPOINT_ADDRESS(hubDevice->Interrupt->Address), USB_FEATURE_HALT);
            UsbEndpointReset(&hubDevice->Base->DeviceContext,
                             USB_ENDPOINT_ADDRESS(hubDevice->Interrupt->Address));
            UsbTransferResetPeriodic(&hubDevice->Base->DeviceContext, hubDevice->TransferId);
        }
        else {
            HubInterrupt(hubDevice, dataIndex);
        }
    }
}

static inline void __PortStatusToDescriptor(
        _In_ PortStatus_t*          portStatus,
        _In_ USBPortDescriptor_t* descriptor)
{
    if (portStatus->Status & HUB_PORT_STATUS_CONNECTED)      { descriptor->Connected = 1; }
    if (portStatus->Status & HUB_PORT_STATUS_ENABLED)        { descriptor->Enabled = 1; }

    if (portStatus->Status & HUB_PORT_STATUS_LOWSPEED)       { descriptor->Speed = USB_SPEED_LOW; }
    else if (portStatus->Status & HUB_PORT_STATUS_HIGHSPEED) { descriptor->Speed = USB_SPEED_HIGH; }
    else                                                     { descriptor->Speed = USB_SPEED_FULL; }
}

void ctt_usbhub_query_port_invocation(struct gracht_message* message, const uuid_t deviceId, const uint8_t portId)
{
    USBPortDescriptor_t portDescriptor = {0 };
    HubDevice_t*          hubDevice;
    PortStatus_t          portStatus;
    oserr_t            osStatus;

    hubDevice = HubDeviceGet(deviceId);
    if (!hubDevice) {
        osStatus = OS_EINVALPARAMS;
        goto respond;
    }

    osStatus = HubGetPortStatus(hubDevice, portId, &portStatus);
    __PortStatusToDescriptor(&portStatus, &portDescriptor);
respond:
    ctt_usbhub_query_port_response(message, osStatus, (uint8_t*)&portDescriptor, sizeof(USBPortDescriptor_t));
}

void ctt_usbhub_reset_port_invocation(struct gracht_message* message, const uuid_t deviceId, const uint8_t portId)
{
    USBPortDescriptor_t portDescriptor = {0 };
    HubDevice_t*          hubDevice;
    PortStatus_t          portStatus;
    oserr_t            osStatus;

    hubDevice = HubDeviceGet(deviceId);
    if (!hubDevice) {
        osStatus = OS_EINVALPARAMS;
        goto respond;
    }

    osStatus = HubResetPort(hubDevice, portId);
    if (osStatus != OS_EOK) {
        goto respond;
    }

    osStatus = HubGetPortStatus(hubDevice, portId, &portStatus);
    __PortStatusToDescriptor(&portStatus, &portDescriptor);
respond:
    ctt_usbhub_reset_port_response(message, osStatus, (uint8_t*)&portDescriptor, sizeof(USBPortDescriptor_t));
}

void ctt_driver_ioctl_invocation(
        _In_ struct gracht_message* message,
        _In_ const uuid_t           deviceId,
        _In_ const unsigned int     request,
        _In_ const uint8_t*         out,
        _In_ const uint32_t         out_count)
{
    enum OSIOCtlRequest req = (enum OSIOCtlRequest)request;
    HubDevice_t*        device = HubDeviceGet(deviceId);
    oserr_t             oserr;
    if (device == NULL) {
        ctt_driver_ioctl_response(message, NULL, 0, OS_ENOENT);
        return;
    }

    switch (req) {
        case OSIOCTLREQUEST_IO_REQUIREMENTS: {
            // Hub driver does not pose any memory conformity requirements
            // on its I/O requests. So we need to query the controller itself
            // for this.
            struct OSIOCtlRequestRequirements ioRequirements;
            oserr = OSDeviceIOCtl2(
                    device->Base->DeviceContext.controller_device_id,
                    device->Base->DeviceContext.controller_driver_id,
                    req,
                    &ioRequirements,
                    sizeof(struct OSIOCtlRequestRequirements)
            );
            ctt_driver_ioctl_response(
                    message,
                    (uint8_t*)&ioRequirements,
                    sizeof(struct OSIOCtlRequestRequirements),
                    oserr
            );
            return;
        }

        default:
            break;
    }
    ctt_driver_ioctl_response(message, NULL, 0, OS_ENOTSUPPORTED);
}

// Caught by lazyness in libddk where all clients are gathered, and thus events need definitions.
void sys_device_event_protocol_device_invocation(void) { }
void sys_device_event_device_update_invocation(void) { }
void ctt_usbhub_event_port_status_invocation(void) { }
