/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * - Transaction Translator Support
 */
#define __TRACE

/* Includes
 * - System */
#include <os/utils.h>
#include "ehci.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* HciQueueTransferIsochronous 
 * Queues a new isochronous transfer for the given driver and pipe. 
 * The function does not block. */
UsbTransferStatus_t
HciQueueTransferIsochronous(
    _In_ UsbManagerTransfer_t*      Transfer)
{
    // Variables
    EhciIsochronousDescriptor_t *FirstTd    = NULL;
    EhciIsochronousDescriptor_t *PreviousTd = NULL;
    EhciController_t *Controller            = NULL;
    uintptr_t AddressPointer                = Transfer->Transfer.Transactions[0].BufferAddress;
    size_t BytesToTransfer                  = Transfer->Transfer.Transactions[0].Length;
    size_t MaxBytesPerDescriptor            = 0;
    size_t Address, Endpoint;
    DataKey_t Key;
    int i;

    // Get Controller
    Controller              = (EhciController_t *)UsbManagerGetController(Transfer->DeviceId);
    Transfer->Status        = TransferNotProcessed;

    // Extract address and endpoint
    Address                 = HIWORD(Transfer->Pipe);
    Endpoint                = LOWORD(Transfer->Pipe);

    // Calculate mpd
    MaxBytesPerDescriptor   = 1024 * MAX(3, Transfer->Transfer.Endpoint.Bandwidth);
    MaxBytesPerDescriptor  *= 8;

    // Allocate resources
    while (BytesToTransfer) {
        EhciIsochronousDescriptor_t *iTd    = NULL;
        size_t BytesStep                    = MIN(BytesToTransfer, MaxBytesPerDescriptor);
        if (UsbSchedulerAllocateElement(Controller->Base.Scheduler, EHCI_iTD_POOL, (uint8_t**)&iTd) == OsSuccess) {
            if (EhciTdIsochronous(Controller, &Transfer->Transfer, iTd, 
                    AddressPointer, BytesStep, Address, Endpoint) != OsSuccess) {
                // Out of bandwidth @todo
                TRACE(" > Out of bandwidth");
                for(;;);
            }
        }

        if (iTd == NULL) {
            TRACE(" > Failed to allocate descriptor");
            for(;;);
            break;
        }

        // Update pointers
        if (FirstTd == NULL) {
            FirstTd     = iTd;
            PreviousTd  = iTd;
        }
        else {
            UsbSchedulerChainElement(Controller->Base.Scheduler, 
                (uint8_t*)FirstTd, (uint8_t*)iTd, USB_ELEMENT_NO_INDEX, USB_CHAIN_DEPTH);
            
            for (i = 0; i < 8; i++) {
                if (PreviousTd->Transactions[i] & EHCI_iTD_IOC) {
                    PreviousTd->Transactions[i] &= ~(EHCI_iTD_IOC);
                    PreviousTd->TransactionsCopy[i] &= ~(EHCI_iTD_IOC);
                }
            }

            PreviousTd  = iTd;
        }

        AddressPointer              += BytesStep;
        BytesToTransfer             -= BytesStep;
    }

    // Add transfer
    Transfer->EndpointDescriptor    = (void*)FirstTd;
    Key.Value                       = 0;
    CollectionAppend(Controller->Base.TransactionList, CollectionCreateNode(Key, Transfer));
    return EhciTransactionDispatch(Controller, Transfer);
}
