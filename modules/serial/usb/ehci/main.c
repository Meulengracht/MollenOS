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
 * MollenOS MCore - Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 * - Isochronous Transport
 * - Transaction Translator Support
 */
//#define __TRACE

/* Includes 
 * - System */
#include <os/mollenos.h>
#include <os/utils.h>

#include "../common/manager.h"
#include "ehci.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* OnFastInterrupt
 * Is called for the sole purpose to determine if this source
 * has invoked an irq. If it has, silence and return (Handled) */
InterruptStatus_t
OnFastInterrupt(
    _In_Opt_ void *InterruptData)
{
    // Variables
    EhciController_t *Controller = NULL;
    reg32_t InterruptStatus;

    // Instantiate the pointer
    Controller = (EhciController_t*)InterruptData;

    // Calculate the kinds of interrupts this controller accepts
    InterruptStatus = 
        (Controller->OpRegisters->UsbStatus & Controller->OpRegisters->UsbIntr);

    // Trace
    TRACE("EHCI-Interrupt - Status 0x%x", InterruptStatus);

    // Was the interrupt even from this controller?
    if (!InterruptStatus) {
        return InterruptNotHandled;
    }

    // Acknowledge the interrupt by clearing
    Controller->OpRegisters->UsbStatus = InterruptStatus;
    Controller->Base.InterruptStatus |= InterruptStatus;
    return InterruptHandled;
}

/* OnInterrupt
 * Is called by external services to indicate an external interrupt.
 * This is to actually process the device interrupt */
InterruptStatus_t 
OnInterrupt(
    _In_Opt_ void *InterruptData,
    _In_Opt_ size_t Arg0,
    _In_Opt_ size_t Arg1,
    _In_Opt_ size_t Arg2)
{
    // Variables
    EhciController_t *Controller = NULL;
    reg32_t InterruptStatus;
    
    // Unused
    _CRT_UNUSED(Arg0);
    _CRT_UNUSED(Arg1);
    _CRT_UNUSED(Arg2);

    // Instantiate the pointer
    Controller = (EhciController_t*)InterruptData;

ProcessInterrupt:
    InterruptStatus = Controller->Base.InterruptStatus;
    Controller->Base.InterruptStatus = 0;
    // Transaction update, either error or completion
    if (InterruptStatus & (EHCI_STATUS_PROCESS | EHCI_STATUS_PROCESSERROR)) {
        EhciProcessTransfers(Controller);
    }

    // Hub change? We should enumerate ports and detect
    // which events occured
    if (InterruptStatus & EHCI_STATUS_PORTCHANGE) {
        EhciPortScan(Controller);
    }

    // HC Fatal Error
    // Clear all queued, reset controller
    if (InterruptStatus & EHCI_STATUS_HOSTERROR) {
        if (EhciQueueReset(Controller) != OsSuccess) {
            ERROR("EHCI-Failure: Failed to reset queue after fatal error");
        }
        if (EhciRestart(Controller) != OsSuccess) {
            ERROR("EHCI-Failure: Failed to reset controller after fatal error");
        }
    }

    // Doorbell? Process transactions in progress
    if (InterruptStatus & EHCI_STATUS_ASYNC_DOORBELL) {
        EhciProcessDoorBell(Controller);
    }
    
    // In case an interrupt fired during processing
    if (Controller->Base.InterruptStatus != 0) {
        goto ProcessInterrupt;
    }
    return InterruptHandled;
}

/* OnTimeout
 * Is called when one of the registered timer-handles
 * times-out. A new timeout event is generated and passed
 * on to the below handler */
OsStatus_t
OnTimeout(
    _In_ UUId_t Timer,
    _In_ void *Data)
{
    return OsSuccess;
}

/* OnLoad
 * The entry-point of a driver, this is called
 * as soon as the driver is loaded in the system */
OsStatus_t
OnLoad(void)
{
    // Initialize the device manager here
    return UsbManagerInitialize();
}

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t
OnUnload(void)
{
    // Cleanup the internal device manager
    return UsbManagerDestroy();
}

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
OsStatus_t
OnRegister(
    _In_ MCoreDevice_t *Device)
{
    // Variables
    EhciController_t *Controller = NULL;
    
    // Register the new controller
    Controller = EhciControllerCreate(Device);

    // Sanitize
    if (Controller == NULL) {
        return OsError;
    }
    else {
        return OsSuccess;
    }
}

/* OnUnregister
 * Is called when the device-manager wants to unload
 * an instance of this driver from the system */
OsStatus_t
OnUnregister(
    _In_ MCoreDevice_t *Device)
{
    // Variables
    EhciController_t *Controller = NULL;
    
    // Lookup controller
    Controller = (EhciController_t*)UsbManagerGetController(Device->Id);

    // Sanitize lookup
    if (Controller == NULL) {
        return OsError;
    }

    // Destroy it
    return EhciControllerDestroy(Controller);
}

/* OnQuery
 * Occurs when an external process or server quries
 * this driver for data, this will correspond to the query
 * function that is defined in the contract */
OsStatus_t 
OnQuery(
    _In_ MContractType_t QueryType, 
    _In_ int QueryFunction, 
    _In_Opt_ RPCArgument_t *Arg0,
    _In_Opt_ RPCArgument_t *Arg1,
    _In_Opt_ RPCArgument_t *Arg2, 
    _In_ UUId_t Queryee, 
    _In_ int ResponsePort)
{
    // Variables
    UsbManagerTransfer_t *Transfer = NULL;
    EhciController_t *Controller = NULL;
    UUId_t Device = UUID_INVALID, Pipe = UUID_INVALID;
    OsStatus_t Result = OsError;

    // Instantiate some variables
    Device = (UUId_t)Arg0->Data.Value;
    Pipe = (UUId_t)Arg1->Data.Value;
    
    // Lookup controller
    Controller = (EhciController_t*)UsbManagerGetController(Device);

    // Sanitize we have a controller
    if (Controller == NULL) {
        // Null response
        return PipeSend(Queryee, ResponsePort, 
            (void*)&Result, sizeof(OsStatus_t));
    }

    switch (QueryFunction) {
        // Generic Queue
        case __USBHOST_QUEUETRANSFER: {
            // Variables
            UsbTransferResult_t ResPackage;

            // Create and setup new transfer
            Transfer = UsbManagerCreateTransfer(
                (UsbTransfer_t*)Arg2->Data.Buffer,
                Queryee, ResponsePort, Device, Pipe);
                
            // Queue the periodic transfer
            ResPackage.Status = UsbQueueTransferGeneric(Transfer);
            ResPackage.Id = Transfer->Id;
            ResPackage.BytesTransferred = 0;

            // Send back package?
            if (ResPackage.Status != TransferQueued) {
                return PipeSend(Queryee, ResponsePort, 
                    (void*)&ResPackage, sizeof(UsbTransferResult_t));
            }
            else {
                return OsSuccess;
            }
        } break;

        // Periodic Queue
        case __USBHOST_QUEUEPERIODIC: {
            // Variables
            UsbTransferResult_t ResPackage;

            // Create and setup new transfer
            Transfer = UsbManagerCreateTransfer(
                (UsbTransfer_t*)Arg2->Data.Buffer,
                Queryee, ResponsePort, Device, Pipe);

            // Queue the periodic transfer
            ResPackage.Status = UsbQueueTransferGeneric(Transfer);
            ResPackage.Id = Transfer->Id;
            ResPackage.BytesTransferred = 0;

            // Send back package
            return PipeSend(Queryee, ResponsePort, 
                (void*)&ResPackage, sizeof(UsbTransferResult_t));
        } break;

        // Dequeue Transfer
        case __USBHOST_DEQUEUEPERIODIC: {
            // Variables
            UsbManagerTransfer_t *Transfer = NULL;
            UUId_t Id = (UUId_t)Arg1->Data.Value;
            UsbTransferStatus_t Status = TransferInvalid;

            // Lookup transfer by iterating through
            // available transfers
            foreach(tNode, Controller->QueueControl.TransactionList) {
                // Cast data to our type
                UsbManagerTransfer_t *NodeTransfer = 
                    (UsbManagerTransfer_t*)tNode->Data;
                if (NodeTransfer->Id == Id) {
                    Transfer = NodeTransfer;
                    break;
                }
            }

            // Dequeue and send result back
            if (Transfer != NULL) {
                Status = UsbDequeueTransferGeneric(Transfer);
            }

            // Send back package
            return PipeSend(Queryee, ResponsePort, 
                (void*)&Status, sizeof(UsbTransferStatus_t));
        } break;

        // Reset port
        case __USBHOST_RESETPORT: {
            // Call reset procedure, then let it fall through
            // to QueryPort
            EhciPortReset(Controller, (int)Pipe);
        };
        // Query port
        case __USBHOST_QUERYPORT: {
            // Variables
            UsbHcPortDescriptor_t Descriptor;

            // Fill port descriptor
            EhciPortGetStatus(Controller, (int)Pipe, &Descriptor);

            // Send descriptor back
            return PipeSend(Queryee, ResponsePort, 
                (void*)&Descriptor, sizeof(UsbHcPortDescriptor_t));
        } break;
        
        // Reset endpoint toggles
        case __USBHOST_RESETENDPOINT: {
            Result = UsbManagerSetToggle(Device, Pipe, 0);
        } break;

        // Fall-through, error
        default:
            break;
    }

    // Dunno, fall-through case
    // Return status response
    return PipeSend(Queryee, ResponsePort, (void*)&Result, sizeof(OsStatus_t));
}
