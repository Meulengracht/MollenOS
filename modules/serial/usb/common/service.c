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
 * MollenOS MCore - USB Controller Manager
 * - Contains the implementation of a shared controller manager
 *   for all the usb drivers
 */
#define __TRACE

/* Includes
 * - System */
#include <os/mollenos.h>
#include <os/utils.h>
#include "hci.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <stdlib.h>

/* OnLoad
 * The entry-point of a driver, this is called
 * as soon as the driver is loaded in the system */
OsStatus_t
OnLoad(void) {
    return UsbManagerInitialize();
}

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t
OnUnload(void) {
    return UsbManagerDestroy();
}

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
OsStatus_t
OnRegister(
    _In_ MCoreDevice_t *Device)
{
    if (HciControllerCreate(Device) == NULL) {
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
    UsbManagerController_t *Controller = NULL;
    
    // Lookup controller
    Controller = UsbManagerGetController(Device->Id);
    if (Controller == NULL) {
        return OsError;
    }
    return HciControllerDestroy(Controller);
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
    // Variables
    UsbManagerController_t *Controller  = NULL;
    UsbManagerTransfer_t *Transfer      = NULL;
    OsStatus_t Result                   = OsError;
    UUId_t Device                       = UUID_INVALID;

    // Debug
    TRACE("Hci.OnQuery(Function %i)", QueryFunction);

    // Instantiate some variables
    Device      = (UUId_t)Arg0->Data.Value;
    Controller  = UsbManagerGetController(Device);
    if (Controller == NULL) {
        return RPCRespond(Address, (void*)&Result, sizeof(OsStatus_t));
    }

    switch (QueryFunction) {
        // Generic Queue
        case __USBHOST_QUEUETRANSFER: {
            // Variables
            UsbTransferResult_t ResPackage;

            // Create and setup new transfer
            Transfer                    = UsbManagerCreateTransfer(
                (UsbTransfer_t*)Arg1->Data.Buffer, Address, Device);
            
            // Queue the periodic transfer
            ResPackage.Status           = HciQueueTransferGeneric(Transfer);
            ResPackage.Id               = Transfer->Id;
            ResPackage.BytesTransferred = 0;
            if (ResPackage.Status != TransferQueued) {
                return RPCRespond(Address, (void*)&ResPackage, sizeof(UsbTransferResult_t));
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
            Transfer                    = UsbManagerCreateTransfer(
                (UsbTransfer_t*)Arg1->Data.Buffer, Address, Device);

            // Queue the periodic transfer
            if (Transfer->Transfer.Type == IsochronousTransfer) {
                ResPackage.Status       = HciQueueTransferIsochronous(Transfer);
            }
            else {
                ResPackage.Status       = HciQueueTransferGeneric(Transfer);
            }
            ResPackage.Id               = Transfer->Id;
            ResPackage.BytesTransferred = 0;
            return RPCRespond(Address, (void*)&ResPackage, sizeof(UsbTransferResult_t));
        } break;

        // Dequeue Transfer
        case __USBHOST_DEQUEUEPERIODIC: {
            // Variables
            UsbManagerTransfer_t *Transfer  = NULL;
            UUId_t Id                       = (UUId_t)Arg1->Data.Value;
            UsbTransferStatus_t Status      = TransferInvalid;

            // Lookup transfer by iterating through
            // available transfers
            foreach(tNode, Controller->TransactionList) {
                // Cast data to our type
                UsbManagerTransfer_t *NodeTransfer = (UsbManagerTransfer_t*)tNode->Data;
                if (NodeTransfer->Id == Id) {
                    Transfer = NodeTransfer;
                    break;
                }
            }

            // Dequeue and send result back
            if (Transfer != NULL) {
                Status = HciDequeueTransfer(Transfer);
            }

            // Send back package
            return RPCRespond(Address, (void*)&Status, sizeof(UsbTransferStatus_t));
        } break;

        // Reset port
        case __USBHOST_RESETPORT: {
            // Call reset procedure, then let it fall through to QueryPort
            HciPortReset(Controller, (int)Arg1->Data.Value);
        };
        // Query port
        case __USBHOST_QUERYPORT: {
            UsbHcPortDescriptor_t Descriptor;
            HciPortGetStatus(Controller, (int)Arg1->Data.Value, &Descriptor);
            return RPCRespond(Address, (void*)&Descriptor, sizeof(UsbHcPortDescriptor_t));
        } break;
        
        // Reset endpoint toggles
        case __USBHOST_RESETENDPOINT: {
            UsbHcAddress_t *Address = NULL;
            if (RPCCastArgumentToPointer(Arg1, (void**)&Address) == OsSuccess) {
                Result = UsbManagerSetToggle(Device, Address, 0);
            }
        } break;

        // Fall-through, error
        default:
            break;
    }

    // Dunno, fall-through case
    // Return status response
    return RPCRespond(Address, (void*)&Result, sizeof(OsStatus_t));
}
