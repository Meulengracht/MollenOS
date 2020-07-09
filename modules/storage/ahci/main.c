/* MollenOS
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

#include <os/mollenos.h>
#include <ddk/io.h>
#include <ddk/utils.h>
#include "manager.h"
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include <ctt_driver_protocol_server.h>

static void ctt_driver_register_device_callback(struct gracht_recv_message* message, struct ctt_driver_register_device_args*);

static gracht_protocol_function_t ctt_driver_callbacks[1] = {
        { PROTOCOL_CTT_DRIVER_REGISTER_DEVICE_ID , ctt_driver_register_device_callback },
};
DEFINE_CTT_DRIVER_SERVER_PROTOCOL(ctt_driver_callbacks, 1);

#include <ctt_storage_protocol_server.h>

extern void ctt_storage_stat_callback(struct gracht_recv_message* message, struct ctt_storage_stat_args*);
extern void ctt_storage_transfer_async_callback(struct gracht_recv_message* message, struct ctt_storage_transfer_async_args*);
extern void ctt_storage_transfer_callback(struct gracht_recv_message* message, struct ctt_storage_transfer_args*);

static gracht_protocol_function_t ctt_storage_callbacks[3] = {
    { PROTOCOL_CTT_STORAGE_STAT_ID , ctt_storage_stat_callback },
    { PROTOCOL_CTT_STORAGE_TRANSFER_ASYNC_ID , ctt_storage_transfer_async_callback },
    { PROTOCOL_CTT_STORAGE_TRANSFER_ID , ctt_storage_transfer_callback },
};
DEFINE_CTT_STORAGE_SERVER_PROTOCOL(ctt_storage_callbacks, 3);

static Collection_t Controllers = COLLECTION_INIT(KeyId);

/* OnFastInterrupt
 * Is called for the sole purpose to determine if this source
 * has invoked an irq. If it has, silence and return (Handled) */
InterruptStatus_t
OnFastInterrupt(
    _In_ FastInterruptResources_t*  InterruptTable,
    _In_ void*                      Reserved)
{
    AhciInterruptResource_t* Resource  = (AhciInterruptResource_t*)INTERRUPT_RESOURCE(InterruptTable, 0);
    AHCIGenericRegisters_t*  Registers = (AHCIGenericRegisters_t*)INTERRUPT_IOSPACE(InterruptTable, 0)->Access.Memory.VirtualBase;
    reg32_t                  InterruptStatus;
    int                      i;
    _CRT_UNUSED(Reserved);

    // Skip processing immediately if the interrupt was not for us
    InterruptStatus = Registers->InterruptStatus;
    if (!InterruptStatus) {
        return InterruptNotHandled;
    }

    // Save the status to port that made it and clear
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if ((InterruptStatus & (1 << i)) != 0) {
            AHCIPortRegisters_t* PortRegister   = (AHCIPortRegisters_t*)((uintptr_t)Registers + AHCI_REGISTER_PORTBASE(i));
            Resource->PortInterruptStatus[i]   |= PortRegister->InterruptStatus;
            PortRegister->InterruptStatus       = PortRegister->InterruptStatus;
        }
    }

    // Write clear interrupt register and return
    Registers->InterruptStatus              = InterruptStatus;
    Resource->ControllerInterruptStatus    |= InterruptStatus;
    return InterruptHandled;
}

void
OnInterrupt(
    _In_     int   Signal,
    _In_Opt_ void* InterruptData)
{
    AhciController_t* Controller = (AhciController_t*)InterruptData;
    reg32_t           InterruptStatus;
    int               i;

HandleInterrupt:
    InterruptStatus = Controller->InterruptResource.ControllerInterruptStatus;
    Controller->InterruptResource.ControllerInterruptStatus = 0;
    
    // Iterate the port-map and check if the interrupt
    // came from that port
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (Controller->Ports[i] != NULL && ((InterruptStatus & (1 << i)) != 0)) {
            AhciPortInterruptHandler(Controller, Controller->Ports[i]);
        }
    }
    
    // Re-handle?
    if (Controller->InterruptResource.ControllerInterruptStatus != 0) {
        goto HandleInterrupt;
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
    sigprocess(SIGINT, OnInterrupt);
    
    // Register supported protocols
    gracht_server_register_protocol(&ctt_driver_server_protocol);
    gracht_server_register_protocol(&ctt_storage_server_protocol);
    
    // If AhciManagerInitialize should fail, then the OnUnload will
    // be called automatically
    return AhciManagerInitialize();
}

OsStatus_t
OnUnload(void)
{
    foreach(cNode, &Controllers) {
        AhciControllerDestroy((AhciController_t*)cNode->Data);
    }
    CollectionClear(&Controllers);
    
    signal(SIGINT, SIG_DFL);
    return AhciManagerDestroy();
}

OsStatus_t
OnRegister(
    _In_ Device_t* Device)
{
    AhciController_t* Controller;
    DataKey_t         Key = { .Value.Id = Device->Id };
    
    Controller = AhciControllerCreate((BusDevice_t*)Device);
    if (Controller == NULL) {
        return OsError;
    }
    return CollectionAppend(&Controllers, CollectionCreateNode(Key, Controller));
}

static void ctt_driver_register_device_callback(struct gracht_recv_message* message, struct ctt_driver_register_device_args* args)
{
    OnRegister(args->device);
}

OsStatus_t
OnUnregister(
    _In_ Device_t* Device)
{
    AhciController_t* Controller;
    DataKey_t         Key = { .Value.Id = Device->Id };
    
    Controller  = (AhciController_t*)CollectionGetDataByKey(&Controllers, Key, 0);
    if (Controller == NULL) {
        return OsDoesNotExist;
    }
    
    CollectionRemoveByKey(&Controllers, Key);
    return AhciControllerDestroy(Controller);
}
