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
 * USB Controller Manager
 * - Contains the implementation of a shared controller manager
 *   for all the usb drivers
 */
//#define __TRACE

#include <ddk/utils.h>
#include "hci.h"
#include <os/mollenos.h>
#include <os/ipc.h>
#include <stdlib.h>
#include <signal.h>

extern void OnInterrupt(int, void*);

OsStatus_t
OnLoad(void)
{
    sigprocess(SIGINT, OnInterrupt);
    return UsbManagerInitialize();
}

OsStatus_t
OnUnload(void)
{
    signal(SIGINT, SIG_DFL);
    return UsbManagerDestroy();
}

OsStatus_t
OnRegister(
    _In_ MCoreDevice_t* Device)
{
    if (HciControllerCreate(Device) == NULL) {
        return OsError;
    }
    else {
        return OsSuccess;
    }
}

OsStatus_t
OnUnregister(
    _In_ MCoreDevice_t *Device)
{
    UsbManagerController_t* Controller = UsbManagerGetController(Device->Id);
    if (Controller == NULL) {
        return OsError;
    }
    return HciControllerDestroy(Controller);
}

OsStatus_t
OnQuery(
    _In_ IpcMessage_t* Message)
{
    UsbManagerController_t* Controller;
    UsbManagerTransfer_t*   Transfer;
    OsStatus_t              Result = OsError;
    UUId_t                  Device = UUID_INVALID;

    // Debug
    TRACE("Hci.OnQuery(Function %i)", IPC_GET_TYPED(Message, 1));

    // Instantiate some variables
    Device      = (UUId_t)IPC_GET_TYPED(Message, 3);
    Controller  = UsbManagerGetController(Device);
    if (Controller == NULL) {
        return IpcReply(Message, &Result, sizeof(OsStatus_t));
    }

    switch (IPC_GET_TYPED(Message, 1)) {
        // Generic Queue
        case __USBHOST_QUEUETRANSFER: {
            UsbTransferResult_t ResPackage;

            // Create and setup new transfer
            Transfer = UsbManagerCreateTransfer(IPC_GET_UNTYPED(Message, 0),
                Message->Sender, Device);
            
            // Queue the periodic transfer
            ResPackage.Status           = HciQueueTransferGeneric(Transfer);
            ResPackage.Id               = Transfer->Id;
            ResPackage.BytesTransferred = 0;
            if (ResPackage.Status != TransferQueued) {
                return IpcReply(Message, &ResPackage, sizeof(UsbTransferResult_t));
            }
            else {
                return OsSuccess;
            }
        } break;

        // Periodic Queue
        case __USBHOST_QUEUEPERIODIC: {
            UsbTransferResult_t ResPackage;

            // Create and setup new transfer
            Transfer = UsbManagerCreateTransfer(IPC_GET_UNTYPED(Message, 0),
                Message->Sender, Device);

            // Queue the periodic transfer
            if (Transfer->Transfer.Type == IsochronousTransfer) {
                ResPackage.Status = HciQueueTransferIsochronous(Transfer);
            }
            else {
                ResPackage.Status = HciQueueTransferGeneric(Transfer);
            }
            ResPackage.Id               = Transfer->Id;
            ResPackage.BytesTransferred = 0;
            return IpcReply(Message, &ResPackage, sizeof(UsbTransferResult_t));
        } break;

        // Dequeue Transfer
        case __USBHOST_DEQUEUEPERIODIC: {
            UUId_t              Id     = (UUId_t)IPC_GET_TYPED(Message, 4);
            UsbTransferStatus_t Status = TransferInvalid;
            Transfer = NULL;

            // Lookup transfer by iterating through available transfers
            foreach(tNode, Controller->TransactionList) {
                UsbManagerTransfer_t* NodeTransfer = (UsbManagerTransfer_t*)tNode->Data;
                if (NodeTransfer->Id == Id) {
                    Transfer = NodeTransfer;
                    break;
                }
            }

            // Dequeue and send result back
            if (Transfer != NULL) {
                Status = HciDequeueTransfer(Transfer);
            }
            return IpcReply(Message, &Status, sizeof(UsbTransferStatus_t));
        } break;

        // Reset port
        case __USBHOST_RESETPORT: {
            // Call reset procedure, then let it fall through to QueryPort
            HciPortReset(Controller, (int)IPC_GET_TYPED(Message, 4));
        };
        // Query port
        case __USBHOST_QUERYPORT: {
            UsbHcPortDescriptor_t Descriptor;
            HciPortGetStatus(Controller, (int)IPC_GET_TYPED(Message, 4), &Descriptor);
            return IpcReply(Message, &Descriptor, sizeof(UsbHcPortDescriptor_t));
        } break;
        
        // Reset endpoint toggles
        case __USBHOST_RESETENDPOINT: {
            UsbHcAddress_t* Address = IPC_GET_UNTYPED(Message, 0);
            Result = UsbManagerSetToggle(Device, Address, 0);
        } break;

        // Fall-through, error
        default:
            break;
    }
    return IpcReply(Message, &Result, sizeof(OsStatus_t));
}
