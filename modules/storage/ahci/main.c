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

#include <ddk/contracts/storage.h>
#include <os/mollenos.h>
#include <os/ipc.h>
#include <ddk/utils.h>
#include "manager.h"
#include <string.h>
#include <stdlib.h>
#include <signal.h>

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

OsStatus_t
OnLoad(void)
{
    // If AhciManagerInitialize should fail, then the OnUnload will
    // be called automatically
    sigprocess(SIGINT, OnInterrupt);
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
    _In_ MCoreDevice_t* Device)
{
    AhciController_t* Controller;
    DataKey_t         Key = { .Value.Id = Device->Id };
    
    Controller = AhciControllerCreate(Device);
    if (Controller == NULL) {
        return OsError;
    }
    return CollectionAppend(&Controllers, CollectionCreateNode(Key, Controller));
}

OsStatus_t
OnUnregister(
    _In_ MCoreDevice_t* Device)
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

OsStatus_t 
OnQuery(
    _In_ IpcMessage_t* Message)
{
    if (IPC_GET_TYPED(Message, 1) != ContractStorage) {
        return OsError;
    }

    TRACE("Ahci.OnQuery(%i)", IPC_GET_TYPED(Message, 2));

    // Which kind of function has been invoked?
    switch (IPC_GET_TYPED(Message, 2)) {
        // Query stats about a disk identifier in the form of
        // a StorageDescriptor
        case __STORAGE_QUERY_STAT: {
            AhciDevice_t*       Device;
            StorageDescriptor_t NullDescriptor;
            UUId_t              DiskId = (UUId_t)IPC_GET_TYPED(Message, 1);

            Device = AhciManagerGetDevice(DiskId);
            if (Device != NULL) {
                return IpcReply(Message, &Device->Descriptor, sizeof(StorageDescriptor_t));
            }
            else {
                memset((void*)&NullDescriptor, 0, sizeof(StorageDescriptor_t));
                return IpcReply(Message, &NullDescriptor, sizeof(StorageDescriptor_t));
            }
        } break;

            // Read or write sectors from a disk identifier
            // They have same parameters with different direction
        case __STORAGE_TRANSFER: {
            // Get parameters
            StorageOperation_t*      Operation = (StorageOperation_t*)IPC_GET_UNTYPED(Message, 0);
            UUId_t                   DiskId    = (UUId_t)IPC_GET_TYPED(Message, 1);
            AhciDevice_t*            Device    = AhciManagerGetDevice(DiskId);
            StorageOperationResult_t Result    = { .Status = OsInvalidParameters };
            
            if (Device == NULL) {
                return IpcReply(Message, &Result, sizeof(StorageOperationResult_t));
            }
            
            // Create the requested transaction
            Result.Status = AhciTransactionStorageCreate(Device, Message->Sender, Operation);
            if (Result.Status != OsSuccess) {
                return IpcReply(Message, &Result, sizeof(StorageOperationResult_t));
            }
            
            return OsSuccess;
        } break;

        // Other cases not supported
        default: {
            return OsNotSupported;
        }
    }
}
