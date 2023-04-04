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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */

//#define __TRACE
#define __need_minmax
#include <ddk/utils.h>
#include "ohci.h"
#include <assert.h>
#include <stdlib.h>

static inline uintptr_t
__GetAddress(
        _In_ struct UsbManagerTransaction* xaction,
        _In_ int                           index,
        _In_ size_t                        offset)
{
    return xaction->SHMTable.Entries[index].Address + offset;
}

static inline size_t
__GetBytesLeft(
        _In_ struct UsbManagerTransaction* xaction,
        _In_ int                           index,
        _In_ size_t                        offset)
{
    return xaction->SHMTable.Entries[index].Length - offset;
}

static inline size_t
__CalculatePacketSize(
        _In_ struct UsbManagerTransaction* xaction,
        _In_ size_t                        originalLength,
        _In_ size_t                        bytesLeft)
{
    int       index;
    size_t    offset;
    SHMSGTableOffset(
            &xaction->SHMTable,
            (originalLength - bytesLeft),
            &index,
            &offset
    );
    uintptr_t address = __GetAddress(xaction, index, offset);
    size_t    leftInSG = MIN(bytesLeft, __GetBytesLeft(xaction, index, offset));
    return MIN((0x2000 - (address & (0x1000 - 1))), leftInSG);
}

static int
__CountDescriptorsNeeded(
        _In_ UsbManagerTransfer_t* transfer)
{
    int tdsNeeded = 0;
    for (int i = 0; i < USB_TRANSACTIONCOUNT; i++) {
        uint8_t type = transfer->Transfer.Transactions[i].Type;

        if (type == USB_TRANSACTION_SETUP) {
            tdsNeeded++;
        } else if (type == USB_TRANSACTION_IN || type == USB_TRANSACTION_OUT) {
            // Special case: zero length packets
            if (transfer->Transfer.Transactions[i].BufferHandle == UUID_INVALID) {
                tdsNeeded++;
            } else {
                // TDs have the capacity of 8196 bytes, but only when transferring on
                // a page-boundary. If not, an extra will be needed.
                size_t bytesLeft = transfer->Transfer.Transactions[i].Length;
                while (bytesLeft) {
                    bytesLeft -= __CalculatePacketSize(
                            &transfer->Transactions[i],
                            transfer->Transfer.Transactions[i].Length,
                            bytesLeft
                    );
                    tdsNeeded++;
                }
            }
        }
    }
    return tdsNeeded;
}

static int
__AllocateDescriptorChain(
        _In_ OhciController_t* controller,
        _In_ OhciQueueHead_t*  qh,
        _In_ int               tdCount)
{
    int tdsAllocated = 0;
    for (int i = 0; i < tdCount; i++) {
        OhciTransferDescriptor_t* td;
        oserr_t oserr = UsbSchedulerAllocateElement(
                controller->Base.Scheduler,
                OHCI_TD_POOL,
                (uint8_t**)&td
        );
        if (oserr != OS_EOK) {
            break;
        }

        oserr = UsbSchedulerChainElement(
                controller->Base.Scheduler,
                OHCI_QH_POOL,
                (uint8_t*)qh,
                OHCI_TD_POOL,
                (uint8_t*)td,
                qh->Object.DepthIndex,
                USB_CHAIN_DEPTH
        );
        if (oserr != OS_EOK) {
            UsbSchedulerFreeElement(controller->Base.Scheduler, (uint8_t*)td);
            break;
        }
        tdsAllocated++;
    }
    return tdsAllocated;
}

static inline void
__DestroyDescriptorChain(
        _In_ OhciController_t*     controller,
        _In_ UsbManagerTransfer_t* transfer)
{
    UsbManagerIterateChain(
            &controller->Base,
            transfer->EndpointDescriptor,
            USB_CHAIN_DEPTH,
            USB_REASON_CLEANUP,
            HciProcessElement,
            transfer
    );
}

static oserr_t
__CreateSetupPacket(
        _In_ OhciController_t*             controller,
        _In_ UsbManagerTransfer_t*         transfer,
        _In_ struct UsbManagerTransaction* xaction)
{
    OhciTransferDescriptor_t* td;
    SHMSG_t*                  sg;
    uintptr_t                 dataAddress;
    uint16_t                  zeroIndex;
    oserr_t                   oserr;

    // Setup transactions MUST include a transfer buffer
    if (xaction->SHM.ID != UUID_INVALID) {
        return OS_EBUFFER;
    }

    // No need to care for partial transfers on SETUP tokens
    zeroIndex   = ((OhciQueueHead_t*)transfer->EndpointDescriptor)->Object.DepthIndex;
    sg          = &xaction->SHMTable.Entries[xaction->SGIndex];
    dataAddress = sg->Address + xaction->SGOffset;

    // Allocate a new transfer descriptor to use for the packet
    oserr = UsbSchedulerAllocateElement(
            controller->Base.Scheduler,
            OHCI_TD_POOL,
            (uint8_t**)&td
    );
    if (oserr != OS_EOK) {
        TRACE("__ConstructPackets: failed to allocate td: %u", oserr);
        return oserr;
    }

    OHCITDSetup(td, dataAddress);

    oserr = UsbSchedulerChainElement(
            controller->Base.Scheduler,
            OHCI_QH_POOL,
            transfer->EndpointDescriptor,
            OHCI_TD_POOL,
            (uint8_t*)td,
            zeroIndex,
            USB_CHAIN_DEPTH
    );
    if (oserr != OS_EOK) {
        UsbSchedulerFreeElement(controller->Base.Scheduler, (uint8_t*)td);
    }
    return oserr;
}

static oserr_t
__ConstructPackets(
        _In_ OhciController_t*             controller,
        _In_ UsbManagerTransfer_t*         transfer,
        _In_ struct UsbManagerTransaction* xaction,
        _In_ size_t                        length)
{
    OhciTransferDescriptor_t* previousTd = NULL;
    OhciTransferDescriptor_t* td;

    SHMSG_t*   sg          = NULL;
    size_t     bytesLeft   = length;
    uintptr_t  dataAddress = 0;
    int        dataToggle  = UsbManagerGetToggle(transfer->DeviceId, &transfer->Transfer.Address);

    // Adjust for partial progress on a transaction
    if (bytesLeft && xaction->SHM.ID != UUID_INVALID) {
        sg          = &xaction->SHMTable.Entries[xaction->SGIndex];
        dataAddress = sg->Address + xaction->SGOffset;
        bytesLeft   = MIN(bytesLeft, sg->Length - xaction->SGOffset);
    }

    while (bytesLeft) {
        oserr_t oserr = UsbSchedulerAllocateElement(controller->Base.Scheduler, OHCI_TD_POOL, (uint8_t**)&td);
        if (oserr != OS_EOK) {
            TRACE("__ConstructPackets: failed to allocate td: %u", oserr);
            return oserr;
        }

        if ( == USB_TRANSACTION_SETUP) {
            TRACE("... setup packet");
            Toggle = 0; // Initial toggle must ALWAYS be 0 for setup
            Length = OhciTdSetup(Td, dataAddress, Length);
        }
        else {
            TRACE("... io packet");
            Length = OhciTdIo(Td, Transfer->Transfer.Type,
                              (Type == USB_TRANSACTION_IN ? OHCI_TD_IN : OHCI_TD_OUT),
                              Toggle, dataAddress, Length);
        }
        DEBUG("packet: type=%i, toggle=%i", Type, dataToggle);
        if (UsbSchedulerAllocateElement(Controller->Base.Scheduler, OHCI_TD_POOL, (uint8_t**)&Td) == OS_EOK) {
        }

        // If we didn't allocate a td, we ran out of
        // resources, and have to wait for more. Queue up what we have
        if (Td == NULL) {
            TRACE(".. failed to allocate descriptor");
            if (PreviousToggle != -1) {
                UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.dataAddress, PreviousToggle);
                Transfer->Transfer.Transactions[i].Flags |= USB_TRANSACTION_HANDSHAKE;
            }
            OutOfResources = 1;
            break;
        }
        else {
            UsbSchedulerChainElement(Controller->Base.Scheduler,
                                     OHCI_QH_POOL, (uint8_t*)Qh, OHCI_TD_POOL, (uint8_t*)Td, ZeroIndex, USB_CHAIN_DEPTH);
            previousTd = Td;

            // Update toggle by flipping
            UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.dataAddress, Toggle ^ 1);

            // We have two terminating conditions, either we run out of bytes
            // or we had one ZLP that had to added.
            // Make sure we handle the one where we run out of bytes
            if (Length) {
                BytesToTransfer                    -= Length;
                Transfer->Transactions[i].SGOffset += Length;
                if (sg && Transfer->Transactions[i].SGOffset == sg->Length) {
                    Transfer->Transactions[i].SGIndex++;
                    Transfer->Transactions[i].SGOffset = 0;
                }
            }
            else {
                assert(IsZLP != 0);
                TRACE(".. zlp, done");
                Transfer->Transfer.Transactions[i].Flags &= ~(USB_TRANSACTION_ZLP);
                break;
            }
        }
    }
}

static oserr_t
OhciTransferFill(
    _In_ OhciController_t*     Controller,
    _In_ UsbManagerTransfer_t* Transfer)
{
    OhciTransferDescriptor_t *PreviousTd    = NULL;
    OhciTransferDescriptor_t *Td            = NULL;
    OhciQueueHead_t *Qh                     = (OhciQueueHead_t*)Transfer->EndpointDescriptor;
    uint16_t ZeroIndex                      = Qh->Object.DepthIndex;
    
    int OutOfResources = 0;
    int i;

    // Debug
    TRACE("[usb] [ohci] fill transfer");

    // Clear out the TransferFlagPartial
    Transfer->Flags &= ~(TransferFlagPartial);

    // Get next address from which we need to load
    for (i = 0; i < USB_TRANSACTIONCOUNT; i++) {
        uint8_t Type            = Transfer->Transfer.Transactions[i].Type;
        size_t  BytesToTransfer = Transfer->Transfer.Transactions[i].Length;
        int     PreviousToggle  = -1;
        int     Toggle          = 0;
        int     IsZLP           = Transfer->Transfer.Transactions[i].Flags & USB_TRANSACTION_ZLP;
        int     IsHandshake     = Transfer->Transfer.Transactions[i].Flags & USB_TRANSACTION_HANDSHAKE;
        
        TRACE("[usb] [ohci] xaction %i, length %u, type %i, zlp %i, handshake %i", 
            i, BytesToTransfer, Type, IsZLP, IsHandshake);

        BytesToTransfer -= Transfer->Transactions[i].BytesTransferred;
        if (BytesToTransfer == 0 && !IsZLP) {
            TRACE(" ... skipping");
            continue;
        }

        // If it's a handshake package AND it's first td of package, then set toggle
        if (Transfer->Transactions[i].BytesTransferred == 0 && IsHandshake) {
            TRACE("... setting toggle");
            Transfer->Transfer.Transactions[i].Flags &= ~(USB_TRANSACTION_HANDSHAKE);
            PreviousToggle = UsbManagerGetToggle(Transfer->DeviceId, &Transfer->Transfer.Address);
            UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.Address, 1);
        }
        
        // If its a bulk transfer, with a direction of out, and the requested length is a multiple of
        // the MPS, then we should make sure we add a ZLP
        if ((Transfer->Transfer.Transactions[i].Length % Transfer->Transfer.MaxPacketSize) == 0 &&
            Transfer->Transfer.Type == USB_TRANSFER_BULK &&
            Transfer->Transfer.Transactions[i].Type == USB_TRANSACTION_OUT) {
            TRACE("... appending zlp");
            Transfer->Transfer.Transactions[i].Flags |= USB_TRANSACTION_ZLP;
            IsZLP = 1;
        }

        TRACE("[usb] [ohci] trimmed length %u", BytesToTransfer);
        while (BytesToTransfer || IsZLP) {
            SHMSG_t*   SG      = NULL;
            size_t     Length  = BytesToTransfer;
            uintptr_t  Address = 0;
            
            if (Length && Transfer->Transfer.Transactions[i].BufferHandle != UUID_INVALID) {
                SG      = &Transfer->Transactions[i].SHMTable.Entries[Transfer->Transactions[i].SGIndex];
                Address = SG->Address + Transfer->Transactions[i].SGOffset;
                Length  = MIN(Length, SG->Length - Transfer->Transactions[i].SGOffset);
            }
            
            Toggle = UsbManagerGetToggle(Transfer->DeviceId, &Transfer->Transfer.Address);
            DEBUG("packet: type=%i, toggle=%i", Type, Toggle);
            if (UsbSchedulerAllocateElement(Controller->Base.Scheduler, OHCI_TD_POOL, (uint8_t**)&Td) == OS_EOK) {
                if (Type == USB_TRANSACTION_SETUP) {
                    TRACE("... setup packet");
                    Toggle = 0; // Initial toggle must ALWAYS be 0 for setup
                    Length = OhciTdSetup(Td, Address, Length);
                }
                else {
                    TRACE("... io packet");
                    Length = OhciTdIo(Td, Transfer->Transfer.Type, 
                        (Type == USB_TRANSACTION_IN ? OHCI_TD_IN : OHCI_TD_OUT), 
                        Toggle, Address, Length);
                }
            }

            // If we didn't allocate a td, we ran out of 
            // resources, and have to wait for more. Queue up what we have
            if (Td == NULL) {
                TRACE(".. failed to allocate descriptor");
                if (PreviousToggle != -1) {
                    UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.Address, PreviousToggle);
                    Transfer->Transfer.Transactions[i].Flags |= USB_TRANSACTION_HANDSHAKE;
                }
                OutOfResources = 1;
                break;
            }
            else {
                UsbSchedulerChainElement(Controller->Base.Scheduler, 
                    OHCI_QH_POOL, (uint8_t*)Qh, OHCI_TD_POOL, (uint8_t*)Td, ZeroIndex, USB_CHAIN_DEPTH);
                PreviousTd = Td;

                // Update toggle by flipping
                UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.Address, Toggle ^ 1);
                
                // We have two terminating conditions, either we run out of bytes
                // or we had one ZLP that had to added. 
                // Make sure we handle the one where we run out of bytes
                if (Length) {
                    BytesToTransfer                    -= Length;
                    Transfer->Transactions[i].SGOffset += Length;
                    if (SG && Transfer->Transactions[i].SGOffset == SG->Length) {
                        Transfer->Transactions[i].SGIndex++;
                        Transfer->Transactions[i].SGOffset = 0;
                    }
                }
                else {
                    assert(IsZLP != 0);
                    TRACE(".. zlp, done");
                    Transfer->Transfer.Transactions[i].Flags &= ~(USB_TRANSACTION_ZLP);
                    break;
                }
            }
        }
        
        // Check for partial transfers
        if (OutOfResources == 1) {
            Transfer->Flags |= TransferFlagPartial;
            break;
        }
    }

    // If we ran out of resources queue up later
    if (PreviousTd != NULL) {
        // Enable ioc
        PreviousTd->Flags         &= ~OHCI_TD_IOC_NONE;
        PreviousTd->OriginalFlags = PreviousTd->Flags;
        return OS_EOK;
    }
    
    // Queue up for later
    return OS_EUNKNOWN;
}

UsbTransferStatus_t
HciQueueTransferGeneric(
    _In_ UsbManagerTransfer_t* transfer)
{
    OhciQueueHead_t*    endpointDescriptor = NULL;
    OhciController_t*   controller;
    UsbTransferStatus_t status;

    controller = (OhciController_t*) UsbManagerGetController(transfer->DeviceId);
    if (!controller) {
        return TransferInvalid;
    }

    // Step 1 - Allocate queue head
    if (transfer->EndpointDescriptor == NULL) {
        if (UsbSchedulerAllocateElement(controller->Base.Scheduler,
                                        OHCI_QH_POOL, (uint8_t**)&endpointDescriptor) != OS_EOK) {
            goto queued;
        }
        assert(endpointDescriptor != NULL);
        transfer->EndpointDescriptor = endpointDescriptor;

        // Store and initialize the qh
        if (OhciQhInitialize(controller, transfer,
                             transfer->Transfer.Address.DeviceAddress,
                             transfer->Transfer.Address.EndpointAddress) != OS_EOK) {
            // No bandwidth, serious.
            UsbSchedulerFreeElement(controller->Base.Scheduler, (uint8_t*)endpointDescriptor);
            status = TransferNoBandwidth;
            goto exit;
        }
    }

    // If it fails to queue up => restore toggle
    if (OhciTransferFill(controller, transfer) != OS_EOK) {
        goto queued;
    }

    OhciTransactionDispatch(controller, transfer);
    status = TransferInProgress;
    goto exit;

queued:
    transfer->Status = TransferQueued;
    status = TransferQueued;

exit:
    return status;
}
