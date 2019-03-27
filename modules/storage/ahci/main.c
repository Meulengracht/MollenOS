/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Advanced Host Controller Interface Driver
 * TODO:
 *    - Port Multiplier Support
 *    - Power Management
 */
//#define __TRACE

#include <ddk/contracts/storage.h>
#include <os/mollenos.h>
#include <ddk/utils.h>
#include "manager.h"
#include <string.h>
#include <stdlib.h>

static Collection_t Controllers = COLLECTION_INIT(KeyId);

/* OnFastInterrupt
 * Is called for the sole purpose to determine if this source
 * has invoked an irq. If it has, silence and return (Handled) */
InterruptStatus_t
OnFastInterrupt(
    _In_ FastInterruptResources_t*  InterruptTable,
    _In_ void*                      Reserved)
{
    AhciInterruptResource_t* Resource = (AhciInterruptResource_t*)INTERRUPT_RESOURCE(InterruptTable, 0);
    AHCIGenericRegisters_t* Registers = (AHCIGenericRegisters_t*)INTERRUPT_IOSPACE(InterruptTable, 0)->Access.Memory.VirtualBase;
    reg32_t InterruptStatus;
    int i;
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

/* OnInterrupt
 * Is called by external services to indicate an external interrupt.
 * This is to actually process the device interrupt */
InterruptStatus_t 
OnInterrupt(
    _In_Opt_ void*  InterruptData,
    _In_Opt_ size_t Arg0,
    _In_Opt_ size_t Arg1,
    _In_Opt_ size_t Arg2)
{
    AhciController_t* Controller;
    reg32_t           InterruptStatus;
    int               i;

    // Unused
    _CRT_UNUSED(Arg0);
    _CRT_UNUSED(Arg1);
    _CRT_UNUSED(Arg2);
    Controller = (AhciController_t*)InterruptData;

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
    return InterruptHandled;
}

/* OnLoad
 * The entry-point of a driver, this is called as soon as the driver is loaded in the system */
OsStatus_t
OnLoad(void)
{
    return AhciManagerInitialize();
}

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t
OnUnload(void)
{
    foreach(cNode, &Controllers) {
        AhciControllerDestroy((AhciController_t*)cNode->Data);
    }
    CollectionClear(&Controllers);
    return AhciManagerDestroy();
}

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
OsStatus_t
OnRegister(
    _In_ MCoreDevice_t* Device)
{
    AhciController_t* Controller;
    DataKey_t         Key = { .Value.Id = Device->Id };
    
    // Register the new controller
    Controller = AhciControllerCreate(Device);
    if (Controller == NULL) {
        return OsError;
    }
    return CollectionAppend(&Controllers, CollectionCreateNode(Key, Controller));
}

/* OnUnregister
 * Is called when the device-manager wants to unload
 * an instance of this driver from the system */
OsStatus_t
OnUnregister(
    _In_ MCoreDevice_t*                 Device)
{
    // Variables
    AhciController_t *Controller = NULL;
    DataKey_t Key = { .Value.Id = Device->Id };
    Controller  = (AhciController_t*)CollectionGetDataByKey(&Controllers, Key, 0);
    if (Controller == NULL) {
        return OsError;
    }
    CollectionRemoveByKey(&Controllers, Key);
    return AhciControllerDestroy(Controller);
}

/* OnQuery
 * Occurs when an external process or server quries
 * this driver for data, this will correspond to the query
 * function that is defined in the contract */
OsStatus_t 
OnQuery(
    _In_     MContractType_t        QueryType, 
    _In_     int                    QueryFunction, 
    _In_Opt_ MRemoteCallArgument_t* Arg0,
    _In_Opt_ MRemoteCallArgument_t* Arg1,
    _In_Opt_ MRemoteCallArgument_t* Arg2,
    _In_     MRemoteCallAddress_t*  Address)
{
    // Unused params
    _CRT_UNUSED(Arg2);

    // Sanitize the QueryType
    if (QueryType != ContractStorage) {
        return OsError;
    }

    TRACE("Ahci.OnQuery(%i)", QueryFunction);

    // Which kind of function has been invoked?
    switch (QueryFunction) {
        // Query stats about a disk identifier in the form of
        // a StorageDescriptor
        case __STORAGE_QUERY_STAT: {
            AhciDevice_t*       Device;
            StorageDescriptor_t NullDescriptor;
            UUId_t              DiskId = (UUId_t)Arg0->Data.Value;

            // Lookup device
            Device = AhciManagerGetDevice(DiskId);
            if (Device != NULL) {
                return RPCRespond(Address, (void*)&Device->Descriptor, sizeof(StorageDescriptor_t));
            }
            else {
                memset((void*)&NullDescriptor, 0, sizeof(StorageDescriptor_t));
                return RPCRespond(Address, (void*)&NullDescriptor, sizeof(StorageDescriptor_t));
            }
        } break;

            // Read or write sectors from a disk identifier
            // They have same parameters with different direction
        case __STORAGE_QUERY_WRITE:
        case __STORAGE_QUERY_READ: {
            // Get parameters
            StorageOperation_t*      Operation = (StorageOperation_t*)Arg1->Data.Buffer;
            UUId_t                   DiskId    = (UUId_t)Arg0->Data.Value;
            AhciDevice_t*            Device    = AhciManagerGetDevice(DiskId);
            StorageOperationResult_t Result    = { .Status = OsInvalidParameters };
            AhciTransaction_t*       Transaction;
            
            if (Device == NULL) {
                return RPCRespond(Address, (void*)&Result, sizeof(StorageOperationResult_t));
            }

            // Create a new transaction
            Transaction  = (AhciTransaction_t*)malloc(sizeof(AhciTransaction_t));
            memset((void*)Transaction, 0, sizeof(AhciTransaction_t));
            memcpy((void*)&Transaction->ResponseAddress, Address, sizeof(MRemoteCallAddress_t));
            Transaction->Address     = Operation->PhysicalBuffer;
            Transaction->SectorCount = Operation->SectorCount;
            Transaction->Device      = Device;

            // Determine the kind of operation
            if (Operation->Direction == __STORAGE_OPERATION_READ) {
                Result.Status = AhciReadSectors(Transaction, Operation->AbsoluteSector);
            }
            else if (Operation->Direction == __STORAGE_OPERATION_WRITE) {
                Result.Status = AhciWriteSectors(Transaction, Operation->AbsoluteSector);
            }

            // Only return immediately if there was an error
            if (Result.Status != OsSuccess) {
                return RPCRespond(Address, (void*)&Result, sizeof(StorageOperationResult_t));
            }
            else {
                return OsSuccess;
            }

        } break;

        // Other cases not supported
        default: {
            return OsError;
        }
    }
}
