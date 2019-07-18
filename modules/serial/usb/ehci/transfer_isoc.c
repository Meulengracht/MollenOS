/**
 * MollenOS
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
 * Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 * - Transaction Translator Support
 */
#define __TRACE

#include <ddk/utils.h>
#include "ehci.h"
#include <string.h>
#include <stdlib.h>

UsbTransferStatus_t
HciQueueTransferIsochronous(
    _In_ UsbManagerTransfer_t* Transfer)
{
    EhciIsochronousDescriptor_t* FirstTd    = NULL;
    EhciIsochronousDescriptor_t* PreviousTd = NULL;
    EhciController_t*            Controller;
    size_t                       BytesToTransfer;
    size_t                       MaxBytesPerDescriptor;
    DataKey_t                    Key;
    int                          i;

    Controller       = (EhciController_t *)UsbManagerGetController(Transfer->DeviceId);
    Transfer->Status = TransferNotProcessed;
    BytesToTransfer  = Transfer->Transfer.Transactions[0].Length;

    // Calculate mpd
    MaxBytesPerDescriptor   = 1024 * MAX(3, Transfer->Transfer.Endpoint.Bandwidth);
    MaxBytesPerDescriptor  *= 8;

    // Allocate resources
    while (BytesToTransfer) {
        EhciIsochronousDescriptor_t* iTd;
        uintptr_t                    AddressPointer;
        size_t                       BytesStep;
        
        // Out of three different limiters we must select the lowest one. Either
        // we must transfer lower bytes because of the requested amount, or the limit
        // of a descriptor, or the limit of the DMA table
        BytesStep = MIN(BytesToTransfer, MaxBytesPerDescriptor);
        BytesStep = MIN(BytesStep, Transfer->Transactions[0].DmaTable.entries[
            Transfer->Transactions[0].SgIndex].length - Transfer->Transactions[0].SgOffset);
        
        AddressPointer = Transfer->Transactions[0].DmaTable.entries[
            Transfer->Transactions[0].SgIndex].address + Transfer->Transactions[0].SgOffset;
        
        if (UsbSchedulerAllocateElement(Controller->Base.Scheduler, EHCI_iTD_POOL, (uint8_t**)&iTd) == OsSuccess) {
            if (EhciTdIsochronous(Controller, &Transfer->Transfer, iTd, 
                    AddressPointer, BytesStep, Transfer->Transfer.Address.DeviceAddress, 
                    Transfer->Transfer.Address.EndpointAddress) != OsSuccess) {
                // TODO: Out of bandwidth
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
            UsbSchedulerChainElement(Controller->Base.Scheduler, EHCI_iTD_POOL, 
                (uint8_t*)FirstTd, EHCI_iTD_POOL, (uint8_t*)iTd, USB_ELEMENT_NO_INDEX, USB_CHAIN_DEPTH);
            
            for (i = 0; i < 8; i++) {
                if (PreviousTd->Transactions[i] & EHCI_iTD_IOC) {
                    PreviousTd->Transactions[i] &= ~(EHCI_iTD_IOC);
                    PreviousTd->TransactionsCopy[i] &= ~(EHCI_iTD_IOC);
                }
            }

            PreviousTd  = iTd;
        }
        
        // Increase the DmaTable metrics
        Transfer->Transactions[0].SgOffset += BytesStep;
        if (Transfer->Transactions[0].SgOffset == 
                Transfer->Transactions[0].DmaTable.entries[
                    Transfer->Transactions[0].SgIndex].length) {
            Transfer->Transactions[0].SgIndex++;
            Transfer->Transactions[0].SgOffset = 0;
        }
        BytesToTransfer -= BytesStep;
    }

    Transfer->EndpointDescriptor = (void*)FirstTd;
    Key.Value.Integer            = 0;
    
    CollectionAppend(Controller->Base.TransactionList, CollectionCreateNode(Key, Transfer));
    return EhciTransactionDispatch(Controller, Transfer);
}
