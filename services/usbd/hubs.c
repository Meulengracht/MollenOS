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
 * Service - Usb Manager
 * - Contains the implementation of the usb-manager which keeps track
 *   of all usb-controllers and their devices
 */

#define __TRACE

#include <ddk/devices/usb.h>
#include <ddk/utils.h>
#include <ds/hashtable.h>
#include "manager.h"
#include <stdlib.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>

#include <ctt_usbhost_service_client.h>

static uint64_t hub_hash(const void* element);
static int      hub_cmp(const void* element1, const void* element2);
static void     __HubFreeEntry(int index, const void* element, void* userContext);

static hashtable_t g_hubs;

void UsbCoreHubsInitialize(void)
{
    hashtable_construct(&g_hubs, 0, sizeof(struct UsbHub), hub_hash, hub_cmp);
}

void UsbCoreHubsCleanup(void)
{
    hashtable_enumerate(&g_hubs, __HubFreeEntry, NULL);
    hashtable_destroy(&g_hubs);
    memset(&g_hubs, 0, sizeof(hashtable_t));
}
oserr_t
UsbCoreHubsRegister(
        _In_ uuid_t  parentHubDeviceId,
        _In_ uuid_t  hubDeviceId,
        _In_ uuid_t  hubDriverId,
        _In_ int     portCount,
        _In_ uint16_t characteristics)
{
    UsbHub_t* parentHub;
    uuid_t    controllerDeviceId = parentHubDeviceId;
    uint8_t   portAddress = 0;
    uint8_t   deviceAddress = 0;
    TRACE("UsbCoreHubsRegister(parentHubDeviceId=%u, hubDeviceId=%u, hubDriverId=%u, portCount=%i)",
          parentHubDeviceId, hubDeviceId, hubDriverId, portCount);

    // Root hubs use the same parent device and driver IDs.
    if (parentHubDeviceId != hubDriverId) {
       parentHub = hashtable_get(&g_hubs, &(struct UsbHub) { .DeviceId = parentHubDeviceId });
       if (parentHub) {
           int i;

           controllerDeviceId = parentHub->ControllerDeviceId;
           for (i = 0; i < USB_MAX_PORTS; i++) {
               if (parentHub->Ports[i] && parentHub->Ports[i]->Device &&
                        parentHub->Ports[i]->Device->DeviceId == hubDeviceId) {
                   // Get the hub's USB device address and its port on the parent hub.
                   deviceAddress = parentHub->Ports[i]->Device->Base.device_address;
                   portAddress   = parentHub->Ports[i]->Address;
                   break;
               }
           }
       }
    }

    hashtable_set(&g_hubs, &(struct UsbHub) {
        .ParentHubDeviceId = parentHubDeviceId,
        .ControllerDeviceId = controllerDeviceId,
        .DeviceId = hubDeviceId,
        .DriverId = hubDriverId,
        .PortCount = portCount,
        .Characteristics = characteristics,
        .DeviceAddress = deviceAddress,
        .PortAddress = portAddress,
        .Ports = { 0 }
    });
    return OS_EOK;
}

void
UsbCoreHubsUnregister(
        _In_ uuid_t hubDeviceId)
{
    struct UsbHub                  hub;
    UsbController_t*               controller;
    UsbHub_t*                      parent;
    UsbHub_t*                      hubCopy;
    struct UsbEnumerationRecovery* pending;

    TRACE("UsbCoreHubsUnregister(hubDeviceId=%u)", hubDeviceId);

    hubCopy = hashtable_get(&g_hubs, &(struct UsbHub) {
        .DeviceId = hubDeviceId
    });
    if (!hubCopy) {
        return;
    }

    memcpy(&hub, hubCopy, sizeof(struct UsbHub));
    controller = UsbCoreControllerGet(hub.ControllerDeviceId);
    pending = controller != NULL ? &controller->EnumerationRecovery : NULL;
    
    for (int i = 0; i < USB_MAX_PORTS; i++) {
        if (hub.Ports[i] && hub.Ports[i]->Device) {
            if (UsbCoreHubsGet(hub.Ports[i]->Device->DeviceId) != NULL) {
                UsbCoreHubsUnregister(hub.Ports[i]->Device->DeviceId);
            }
            UsbCoreDevicesDestroy(controller, hub.Ports[i]);
        }
        free(hub.Ports[i]);
    }

    // Downstream ports may still be enabled after driver removal. If isolation
    // is pending, use the upstream port while the parent is still registered.
    // Recursive removal moves the pending isolation toward the root.
    if (pending != NULL && pending->Pending && pending->HubDeviceId == hubDeviceId) {
        parent = UsbCoreHubsGet(hub.ParentHubDeviceId);
        if (parent != NULL && parent->DeviceId != hubDeviceId) {
            pending->HubDeviceId = parent->DeviceId;
            pending->HubDriverId = parent->DriverId;
            pending->PortAddress = hub.PortAddress;
        }
    }

    hashtable_remove(&g_hubs, &(struct UsbHub) { .DeviceId = hubDeviceId });
}

UsbPort_t* __CreatePort(
        _In_ uint8_t address)
{
    UsbPort_t* usbPort;

    usbPort = (UsbPort_t*)malloc(sizeof(UsbPort_t));
    if (!usbPort) {
        return NULL;
    }

    memset(usbPort, 0, sizeof(UsbPort_t));
    usbPort->Address = address;
    return usbPort;
}

UsbPort_t*
UsbCoreHubsGetPort(
        _In_ UsbHub_t* hub,
        _In_ uint8_t   portIndex)
{
    if (!hub || portIndex >= USB_MAX_PORTS) {
        return NULL;
    }

    if (!hub->Ports[portIndex]) {
        hub->Ports[portIndex] = __CreatePort(portIndex);
    }
    return hub->Ports[portIndex];
}

UsbHub_t*
UsbCoreHubsGet(
        _In_ uuid_t hubDeviceId)
{
    return hashtable_get(&g_hubs, &(struct UsbHub) { .DeviceId = hubDeviceId });
}

static void
__HubFreeEntry(
    _In_ int         index,
    _In_ const void* element,
    _In_ void*       userContext)
{
    const UsbHub_t* hub = element;
    _CRT_UNUSED(index);
    _CRT_UNUSED(userContext);

    // Connected devices must already be removed by the controllers. Status
    // queries can create port records on empty hubs, so free those before
    // destroying the table.
    for (int i = 0; i < USB_MAX_PORTS; i++) {
        free(hub->Ports[i]);
    }
}

static uint64_t hub_hash(const void* element)
{
    const struct UsbHub* hub = element;
    return hub->DeviceId;
}

static int hub_cmp(const void* element1, const void* element2)
{
    const struct UsbHub* hub1 = element1;
    const struct UsbHub* hub2 = element2;
    return hub1->DeviceId == hub2->DeviceId ? 0 : 1;
}

void
UsbCoreHubsRegisterAndConfigure(
    _In_ uuid_t   parentHubDeviceId,
    _In_ uuid_t   deviceId,
    _In_ uuid_t   driverId,
    _In_ int      portCount,
    _In_ uint16_t characteristics)
{
    oserr_t                  osStatus;
    oserr_t                  controllerStatus;
    UsbHub_t*                hub;
    UsbController_t*         controller;
    struct vali_link_message msg;

    // Register the hub and configure its controller in the same worker command
    // so configuration finishes before the worker handles later port events.
    osStatus = UsbCoreHubsRegister(parentHubDeviceId, deviceId, driverId, portCount, characteristics);
    hub = UsbCoreHubsGet(deviceId);

    if (osStatus == OS_EOK && hub != NULL && hub->DeviceAddress != 0) {
        controller = UsbCoreControllerGet(hub->ControllerDeviceId);
        msg = (struct vali_link_message)VALI_MSG_INIT_HANDLE(controller->DriverId);
        
        ctt_usbhost_configure_hub(
            GetGrachtClient(), &msg.base,
            hub->ControllerDeviceId, 
            hub->DeviceAddress,
            (uint8_t)portCount,
            characteristics
        );
        gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
        ctt_usbhost_configure_hub_result(GetGrachtClient(), &msg.base, &controllerStatus);
    }
    if (osStatus != OS_EOK) {
        WARNING("USB: hub %u registration failed (%u)", deviceId, osStatus);
    }
}
