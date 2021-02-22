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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Usb Hub Device Driver
 */

//#define __TRACE

#include <ddk/utils.h>
#include <ds/list.h>
#include <ioset.h>
#include "hub.h"

#include <ctt_driver_protocol_server.h>
#include <ctt_usbhub_protocol_server.h>
#include <ctt_usbhost_protocol_client.h>
#include <internal/_utils.h>

static void ctt_driver_register_device_callback(struct gracht_recv_message* message, struct ctt_driver_register_device_args*);
static void ctt_driver_get_device_protocols_callback(struct gracht_recv_message* message, struct ctt_driver_get_device_protocols_args*);

static gracht_protocol_function_t ctt_driver_callbacks[2] = {
        { PROTOCOL_CTT_DRIVER_REGISTER_DEVICE_ID , ctt_driver_register_device_callback },
        { PROTOCOL_CTT_DRIVER_GET_DEVICE_PROTOCOLS_ID , ctt_driver_get_device_protocols_callback },
};
DEFINE_CTT_DRIVER_SERVER_PROTOCOL(ctt_driver_callbacks, 2);

static void ctt_usbhub_query_port_callback(struct gracht_recv_message* message, struct ctt_usbhub_query_port_args*);
static void ctt_usbhub_reset_port_callback(struct gracht_recv_message* message, struct ctt_usbhub_reset_port_args*);

static gracht_protocol_function_t ctt_usbhub_callbacks[2] = {
    { PROTOCOL_CTT_USBHUB_QUERY_PORT_ID , ctt_usbhub_query_port_callback },
    { PROTOCOL_CTT_USBHUB_RESET_PORT_ID , ctt_usbhub_reset_port_callback },
};
DEFINE_CTT_USBHUB_SERVER_PROTOCOL(ctt_usbhub_callbacks, 2);

static void ctt_usbhost_event_transfer_status_callback(struct ctt_usbhost_transfer_status_event*);

static gracht_protocol_function_t ctt_usbhost_callbacks[1] = {
   { PROTOCOL_CTT_USBHOST_EVENT_TRANSFER_STATUS_ID , ctt_usbhost_event_transfer_status_callback },
};
DEFINE_CTT_USBHOST_CLIENT_PROTOCOL(ctt_usbhost_callbacks, 1);

static list_t g_devices = LIST_INIT;

HubDevice_t*
HubDeviceGet(
        _In_ UUId_t deviceId)
{
    return list_find_value(&g_devices, (void*)(uintptr_t)deviceId);
}

void GetModuleIdentifiers(unsigned int* vendorId, unsigned int* deviceId,
    unsigned int* class, unsigned int* subClass)
{
    *vendorId = 0;
    *deviceId = 0;
    *class    = 0xCABB;
    *subClass = 0x90000;
}

OsStatus_t OnEvent(struct ioset_event* event)
{
    return OsNotSupported;
}

OsStatus_t OnLoad(void)
{
    // Register supported server protocols
    gracht_server_register_protocol(&ctt_driver_server_protocol);
    gracht_server_register_protocol(&ctt_usbhub_server_protocol);

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

OsStatus_t
OnUnload(void)
{
    list_clear(&g_devices, DestroyElement, NULL);
    return UsbCleanup();
}

OsStatus_t OnRegister(
    _In_ Device_t* device)
{
    HubDevice_t* hubDevice;

    hubDevice = HubDeviceCreate((UsbDevice_t*)device);
    if (!hubDevice) {
        return OsError;
    }

    list_append(&g_devices, &hubDevice->Header);
    return OsSuccess;
}

void ctt_driver_register_device_callback(
        _In_ struct gracht_recv_message*             message,
        _In_ struct ctt_driver_register_device_args* args)
{
    OnRegister(args->device);
}

OsStatus_t OnUnregister(
    _In_ Device_t* device)
{
    HubDevice_t* hubDevice = HubDeviceGet(device->Id);
    if (hubDevice == NULL) {
        return OsDoesNotExist;
    }

    list_remove(&g_devices, &hubDevice->Header);
    HubDeviceDestroy(hubDevice);
    return OsSuccess;
}

static void ctt_driver_get_device_protocols_callback(struct gracht_recv_message* message, struct ctt_driver_get_device_protocols_args* args)
{
    ctt_driver_event_device_protocol_single(message->client, args->device_id,
                                            "usbhub\0\0\0\0\0\0\0\0\0\0", PROTOCOL_CTT_USBHUB_ID);
}

static void ctt_usbhost_event_transfer_status_callback(
        _In_ struct ctt_usbhost_transfer_status_event* event)
{
    HubDevice_t* hubDevice = NULL;
    TRACE("ctt_usbhost_event_transfer_status_callback(event->status %u, event->bytes_transferred %" PRIuIN ")",
          event->status, event->bytes_transferred);

    foreach(element, &g_devices) {
        HubDevice_t* i = element->value;
        if (i->TransferId == event->id) {
            hubDevice = i;
            break;
        }
    }

    if (hubDevice) {
        if (event->status == TransferStalled) {
            WARNING("ctt_usbhost_event_transfer_status_callback stall, trying to fix");
            // we must clear stall condition and reset endpoint
            UsbClearFeature(&hubDevice->Base.DeviceContext, USBPACKET_DIRECTION_ENDPOINT,
                            USB_ENDPOINT_ADDRESS(hubDevice->Interrupt->Address), USB_FEATURE_HALT);
            UsbEndpointReset(&hubDevice->Base.DeviceContext,
                             USB_ENDPOINT_ADDRESS(hubDevice->Interrupt->Address));
            UsbTransferResetPeriodic(&hubDevice->Base.DeviceContext, hubDevice->TransferId);
        }
        else {
            HubInterrupt(hubDevice, event->bytes_transferred);
        }
    }
}

static inline void __PortStatusToDescriptor(
        _In_ PortStatus_t*          portStatus,
        _In_ UsbHcPortDescriptor_t* descriptor)
{
    if (portStatus->Status & HUB_PORT_STATUS_CONNECTED)      { descriptor->Connected = 1; }
    if (portStatus->Status & HUB_PORT_STATUS_ENABLED)        { descriptor->Enabled = 1; }

    if (portStatus->Status & HUB_PORT_STATUS_LOWSPEED)       { descriptor->Speed = USB_SPEED_LOW; }
    else if (portStatus->Status & HUB_PORT_STATUS_HIGHSPEED) { descriptor->Speed = USB_SPEED_HIGH; }
    else                                                     { descriptor->Speed = USB_SPEED_FULL; }
}

static void ctt_usbhub_query_port_callback(
        _In_ struct gracht_recv_message*        message,
        _In_ struct ctt_usbhub_query_port_args* args)
{
    UsbHcPortDescriptor_t portDescriptor = { 0 };
    HubDevice_t*          hubDevice;
    PortStatus_t          portStatus;
    OsStatus_t            osStatus;

    hubDevice = HubDeviceGet(args->device_id);
    if (!hubDevice) {
        osStatus = OsInvalidParameters;
        goto respond;
    }

    osStatus = HubGetPortStatus(hubDevice, args->port_id, &portStatus);
    __PortStatusToDescriptor(&portStatus, &portDescriptor);
respond:
    ctt_usbhub_query_port_response(message, osStatus, &portDescriptor);
}

static void ctt_usbhub_reset_port_callback(
        _In_ struct gracht_recv_message*        message,
        _In_ struct ctt_usbhub_reset_port_args* args)
{
    UsbHcPortDescriptor_t portDescriptor = { 0 };
    HubDevice_t*          hubDevice;
    PortStatus_t          portStatus;
    OsStatus_t            osStatus;

    hubDevice = HubDeviceGet(args->device_id);
    if (!hubDevice) {
        osStatus = OsInvalidParameters;
        goto respond;
    }

    osStatus = HubResetPort(hubDevice, args->port_id);
    if (osStatus != OsSuccess) {
        goto respond;
    }

    osStatus = HubGetPortStatus(hubDevice, args->port_id, &portStatus);
    __PortStatusToDescriptor(&portStatus, &portDescriptor);
respond:
    ctt_usbhub_reset_port_response(message, osStatus, &portDescriptor);
}
