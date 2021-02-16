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
 * Advanced Host Controller Interface Driver
 * TODO:
 *    - Port Multiplier Support
 *    - Power Management
 */

//#define __TRACE

#include <ddk/utils.h>
#include <ioset.h>
#include "manager.h"
#include <stdlib.h>

#include <ctt_driver_protocol_server.h>

static void ctt_driver_register_device_callback(struct gracht_recv_message* message, struct ctt_driver_register_device_args*);

static gracht_protocol_function_t ctt_driver_callbacks[1] = {
        { PROTOCOL_CTT_DRIVER_REGISTER_DEVICE_ID , ctt_driver_register_device_callback },
};
DEFINE_CTT_DRIVER_SERVER_PROTOCOL(ctt_driver_callbacks, 1);

#include <ctt_storage_protocol_server.h>
#include <io.h>

extern void ctt_storage_stat_callback(struct gracht_recv_message* message, struct ctt_storage_stat_args*);
extern void ctt_storage_transfer_async_callback(struct gracht_recv_message* message, struct ctt_storage_transfer_async_args*);
extern void ctt_storage_transfer_callback(struct gracht_recv_message* message, struct ctt_storage_transfer_args*);

static gracht_protocol_function_t ctt_storage_callbacks[3] = {
    { PROTOCOL_CTT_STORAGE_STAT_ID , ctt_storage_stat_callback },
    { PROTOCOL_CTT_STORAGE_TRANSFER_ASYNC_ID , ctt_storage_transfer_async_callback },
    { PROTOCOL_CTT_STORAGE_TRANSFER_ID , ctt_storage_transfer_callback },
};
DEFINE_CTT_STORAGE_SERVER_PROTOCOL(ctt_storage_callbacks, 3);

static list_t controllers = LIST_INIT;

InterruptStatus_t
OnFastInterrupt(
    _In_ InterruptFunctionTable_t* interruptTable,
    _In_ InterruptResourceTable_t* resourceTable)
{
    AhciInterruptResource_t*         resource  = (AhciInterruptResource_t*)INTERRUPT_RESOURCE(resourceTable, 0);
    volatile AHCIGenericRegisters_t* registers = (volatile AHCIGenericRegisters_t*)INTERRUPT_IOSPACE(resourceTable, 0)->Access.Memory.VirtualBase;
    reg32_t                          interruptStatus;
    int                              i;

    // Skip processing immediately if the interrupt was not for us
    interruptStatus = registers->InterruptStatus;
    if (!interruptStatus) {
        return InterruptNotHandled;
    }

    // Save the status to port that made it and clear
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if ((interruptStatus & (1 << i)) != 0) {
            volatile AHCIPortRegisters_t* portRegister = (volatile AHCIPortRegisters_t*)((uintptr_t)registers + AHCI_REGISTER_PORTBASE(i));
            atomic_fetch_or(&resource->PortInterruptStatus[i], portRegister->InterruptStatus);
            portRegister->InterruptStatus = portRegister->InterruptStatus;
        }
    }

    // Write clear interrupt register and return
    registers->InterruptStatus = interruptStatus;
    atomic_fetch_or(&resource->ControllerInterruptStatus, interruptStatus);
    interruptTable->EventSignal(resourceTable->HandleResource);
    return InterruptHandled;
}

void
OnInterrupt(
    _In_ AhciController_t* controller)
{
    reg32_t interruptStatus = atomic_exchange(&controller->InterruptResource.ControllerInterruptStatus, 0);
    int     i;
    TRACE("OnInterrupt(controller=0x%" PRIxIN ")", controller);

handler_loop:
    // Iterate the port-map and check if the interrupt
    // came from that port
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (controller->Ports[i] != NULL && ((interruptStatus & (1 << i)) != 0)) {
            AhciPortInterruptHandler(controller, controller->Ports[i]);
        }
    }

    interruptStatus = atomic_exchange(&controller->InterruptResource.ControllerInterruptStatus, 0);
    if (interruptStatus) {
        goto handler_loop;
    }
}

void GetModuleIdentifiers(unsigned int* vendorId, unsigned int* deviceId,
    unsigned int* class, unsigned int* subClass)
{
    *vendorId = 0;
    *deviceId = 0;
    *class    = 0x10006;
    *subClass = 0x10000;
}

OsStatus_t
OnLoad(void)
{
    // Register supported protocols
    gracht_server_register_protocol(&ctt_driver_server_protocol);
    gracht_server_register_protocol(&ctt_storage_server_protocol);
    
    // If AhciManagerInitialize should fail, then the OnUnload will
    // be called automatically
    return AhciManagerInitialize();
}

static void
ClearControllerCallback(
    _In_ element_t* element,
    _In_ void*      context)
{
    AhciControllerDestroy((AhciController_t*)element->value);
}

OsStatus_t
OnUnload(void)
{
    list_clear(&controllers, ClearControllerCallback, NULL);
    AhciManagerDestroy();
    return OsSuccess;
}

OsStatus_t OnEvent(struct ioset_event* event)
{
    TRACE("OnEvent(event->events=0x%x)", event->events);
    if (event->events & IOSETSYN) {
        AhciController_t* controller = event->data.context;
        unsigned int      value;
        
        if (read(controller->event_descriptor, &value, sizeof(unsigned int)) != sizeof(unsigned int)) {
            return OsError;
        }

        OnInterrupt(controller);
        return OsSuccess;
    }
    return OsDoesNotExist;
}

OsStatus_t
OnRegister(
    _In_ Device_t* device)
{
    AhciController_t* controller = AhciControllerCreate((BusDevice_t*)device);
    if (controller == NULL) {
        return OsError;
    }

    list_append(&controllers, &controller->header);
    return OsSuccess;
}

static void ctt_driver_register_device_callback(struct gracht_recv_message* message, struct ctt_driver_register_device_args* args)
{
    OnRegister(args->device);
}

OsStatus_t
OnUnregister(
    _In_ Device_t* device)
{
    AhciController_t* controller = list_find_value(&controllers, (void*)(uintptr_t)device->Id);
    if (controller == NULL) {
        return OsDoesNotExist;
    }
    
    list_remove(&controllers, &controller->header);
    return AhciControllerDestroy(controller);
}
