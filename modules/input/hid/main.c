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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Human Input Device Driver (Generic)
 */

//#define __TRACE

#include <ddk/utils.h>
#include <ds/list.h>
#include "hid.h"
#include <ioset.h>
#include <os/mollenos.h>
#include <string.h>
#include <stdlib.h>

#include <ctt_driver_protocol_server.h>
#include <ctt_input_protocol_server.h>

static void ctt_driver_register_device_callback(struct gracht_recv_message* message, struct ctt_driver_register_device_args*);
static void ctt_driver_get_device_protocols_callback(struct gracht_recv_message* message, struct ctt_driver_get_device_protocols_args*);

static gracht_protocol_function_t ctt_driver_callbacks[2] = {
        { PROTOCOL_CTT_DRIVER_REGISTER_DEVICE_ID , ctt_driver_register_device_callback },
        { PROTOCOL_CTT_DRIVER_GET_DEVICE_PROTOCOLS_ID , ctt_driver_get_device_protocols_callback },
};
DEFINE_CTT_DRIVER_SERVER_PROTOCOL(ctt_driver_callbacks, 2);

static void ctt_input_get_properties_callback(struct gracht_recv_message* message, struct ctt_input_get_properties_args*);

static gracht_protocol_function_t ctt_input_callbacks[1] = {
        { PROTOCOL_CTT_INPUT_GET_PROPERTIES_ID , ctt_input_get_properties_callback },
};
DEFINE_CTT_INPUT_SERVER_PROTOCOL(ctt_input_callbacks, 1);

static list_t g_devices = LIST_INIT;

HidDevice_t*
HidDeviceGet(
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
    *subClass = 0x30000;
}

InterruptStatus_t OnInterrupt(
    _In_Opt_ void*  InterruptData,
    _In_Opt_ size_t Arg0,
    _In_Opt_ size_t Arg1,
    _In_Opt_ size_t Arg2)
{
    _CRT_UNUSED(Arg2);
    return HidInterrupt((HidDevice_t*)InterruptData, (UsbTransferStatus_t)Arg0, Arg1);
}

OsStatus_t OnEvent(struct ioset_event* event)
{
    return OsNotSupported;
}

OsStatus_t OnLoad(void)
{
    // Register supported protocols
    gracht_server_register_protocol(&ctt_driver_server_protocol);

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
    _In_ Device_t *Device)
{
    HidDevice_t* hidDevice;

    hidDevice = HidDeviceCreate((UsbDevice_t*)Device);
    if (hidDevice == NULL) {
        return OsError;
    }

    list_append(&g_devices, &hidDevice->Header);
    return OsSuccess;
}

void ctt_driver_register_device_callback(struct gracht_recv_message* message, struct ctt_driver_register_device_args* args)
{
    OnRegister(args->device);
}

OsStatus_t OnUnregister(
    _In_ Device_t* device)
{
    HidDevice_t* hidDevice = HidDeviceGet(device->Id);
    if (hidDevice == NULL) {
        return OsDoesNotExist;
    }

    list_remove(&g_devices, &hidDevice->Header);
    return HidDeviceDestroy(hidDevice);
}

static void ctt_driver_get_device_protocols_callback(struct gracht_recv_message* message, struct ctt_driver_get_device_protocols_args* args)
{
    ctt_driver_event_device_protocol_single(message->client, args->device_id,
                                            "input\0\0\0\0\0\0\0\0\0\0", PROTOCOL_CTT_INPUT_ID);
}

static void ctt_input_get_properties_callback(struct gracht_recv_message* message, struct ctt_input_get_properties_args* args)
{
    //if (port) {
    //    if (port->Signature == 0xAB41 || port->Signature == 0xABC1 || port->Signature == 0xAB83) {
    //        ctt_input_event_properties_single(message->client, args->device_id, input_type_keyboard);
    //    }
    //    else {
    //        ctt_input_event_properties_single(message->client, args->device_id, input_type_mouse);
    //    }
    //}
}
