/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Human Input Device Driver (Generic)
 */

//#define __TRACE

#include <ddk/utils.h>
#include <ds/list.h>
#include "hid.h"
#include <ioset.h>

#include <ctt_driver_service_server.h>
#include <ctt_input_service_server.h>
#include <ctt_usbhost_service_client.h>
#include <internal/_utils.h>

extern gracht_server_t* __crt_get_module_server(void);

static list_t g_devices = LIST_INIT;

HidDevice_t*
HidDeviceGet(
        _In_ UUId_t deviceId)
{
    return list_find_value(&g_devices, (void*)(uintptr_t)deviceId);
}

OsStatus_t OnEvent(struct ioset_event* event)
{
    return OsNotSupported;
}

OsStatus_t OnLoad(void)
{
    // Register supported server protocols
    gracht_server_register_protocol(__crt_get_module_server(), &ctt_driver_server_protocol);
    gracht_server_register_protocol(__crt_get_module_server(), &ctt_input_server_protocol);

    // register supported client protocols
    gracht_client_register_protocol(GetGrachtClient(), &ctt_usbhost_client_protocol);

    return UsbInitialize();
}

static void
DestroyElement(
        _In_ element_t* Element,
        _In_ void*      Context)
{
    HidDeviceDestroy(Element->value);
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
    HidDevice_t* hidDevice;

    hidDevice = HidDeviceCreate((UsbDevice_t*)device);
    if (!hidDevice) {
        return OsError;
    }

    list_append(&g_devices, &hidDevice->Header);
    return OsSuccess;
}

void ctt_driver_register_device_invocation(struct gracht_message* message, const uint8_t* device, const uint32_t device_count)
{
    OnRegister((Device_t*)device);
}

OsStatus_t OnUnregister(
    _In_ Device_t* device)
{
    HidDevice_t* hidDevice = HidDeviceGet(device->Id);
    if (hidDevice == NULL) {
        return OsDoesNotExist;
    }

    list_remove(&g_devices, &hidDevice->Header);
    HidDeviceDestroy(hidDevice);
    return OsSuccess;
}

void ctt_driver_get_device_protocols_invocation(struct gracht_message* message, const UUId_t deviceId)
{
    ctt_driver_event_device_protocol_single(__crt_get_module_server(), message->client, deviceId,
                                            "input\0\0\0\0\0\0\0\0\0\0",
                                            SERVICE_CTT_INPUT_ID);
}

void ctt_input_stat_invocation(struct gracht_message* message, const UUId_t deviceId)
{
    struct UsbHidReportCollectionItem* item;
    HidDevice_t*                       hidDevice = HidDeviceGet(deviceId);
    if (!hidDevice || !hidDevice->Collection) {
        ctt_input_event_stats_single(__crt_get_module_server(), message->client, deviceId, CTT_INPUT_TYPE_INVALID);
        return;
    }

    item = hidDevice->Collection->Childs;
    while (item) {
        if (item->InputType != CTT_INPUT_TYPE_INVALID) {
            ctt_input_event_stats_single(__crt_get_module_server(), message->client, deviceId, item->InputType);
        }
        item = item->Link;
    }
}

void ctt_usbhost_event_transfer_status_invocation(gracht_client_t* client, const UUId_t transferId,
                                                  const UsbTransferStatus_t status, const size_t dataIndex)
{
    HidDevice_t* hidDevice = NULL;

    TRACE("ctt_usbhost_event_transfer_status_callback(event->status %u, event->bytes_transferred %" PRIuIN ")",
          event->status, event->bytes_transferred);

    foreach(element, &g_devices) {
        HidDevice_t* i = element->value;
        if (i->TransferId == transferId) {
            hidDevice = i;
            break;
        }
    }

    if (hidDevice) {
        if (status == TransferStalled) {
            WARNING("ctt_usbhost_event_transfer_status_callback stall, trying to fix");
            // we must clear stall condition and reset endpoint
            UsbClearFeature(&hidDevice->Base.DeviceContext, USBPACKET_DIRECTION_ENDPOINT,
                            USB_ENDPOINT_ADDRESS(hidDevice->Interrupt->Address), USB_FEATURE_HALT);
            UsbEndpointReset(&hidDevice->Base.DeviceContext,
                             USB_ENDPOINT_ADDRESS(hidDevice->Interrupt->Address));
            UsbTransferResetPeriodic(&hidDevice->Base.DeviceContext, hidDevice->TransferId);
        }
        else {
            HidInterrupt(hidDevice, status, dataIndex);
        }
    }
}

// lazyness by libddk
void sys_device_event_protocol_device_invocation(void) { }
void sys_device_event_device_update_invocation(void) { }
void ctt_usbhub_event_port_status_invocation(void) { }
