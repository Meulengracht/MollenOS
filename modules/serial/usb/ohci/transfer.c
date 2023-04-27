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

#define __TRACE
//#define __DIAGNOSE
#define __need_minmax
#include <assert.h>
#include "ohci.h"
#include <os/mollenos.h>
#include <ddk/utils.h>

static void
__DispatchTransfer(
    _In_ OhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer)
{
    TRACE("__DispatchTransfer(%u)", transfer->ID);
#ifdef __DIAGNOSE
    UsbManagerDumpChain(
            &controller->Base,
            transfer,
            (uint8_t*)transfer->RootElement,
            USB_CHAIN_DEPTH
    );
    for(;;);
#endif

    // Set the schedule state on ED and
    // enable SOF, ED is not scheduled before this interrupt
    transfer->State = USBTRANSFER_STATE_SCHEDULE;
    WRITE_VOLATILE(controller->Registers->HcInterruptStatus, OHCI_SOF_EVENT);
    WRITE_VOLATILE(controller->Registers->HcInterruptEnable, OHCI_SOF_EVENT);
}

void
OHCIReloadAsynchronous(
        _In_ OhciController_t*    controller,
        _In_ enum USBTransferType transferType)
{
    OhciQueueHead_t* qh;
    uintptr_t        qhAddress;
    uint16_t         index = USB_ELEMENT_NO_INDEX;
    oserr_t          oserr;
    TRACE("OHCIReloadAsynchronous(type=%u)", transferType);

    // Get correct new index
    if (transferType == USBTRANSFER_TYPE_CONTROL) index = controller->TransactionQueueControlIndex;
    else if (transferType == USBTRANSFER_TYPE_BULK) index = controller->TransactionQueueBulkIndex;
    assert(index != USB_ELEMENT_NO_INDEX);

    // Get the element data
    oserr = UsbSchedulerGetPoolElement(
            controller->Base.Scheduler,
            (index >> USB_ELEMENT_POOL_SHIFT) & USB_ELEMENT_POOL_MASK,
            index & USB_ELEMENT_INDEX_MASK,
            (uint8_t**)&qh,
            &qhAddress
    );
    assert(oserr == OS_EOK);

    // Handle the different queues
    if (transferType == USBTRANSFER_TYPE_CONTROL) {
        WRITE_VOLATILE(controller->Registers->HcControlHeadED, LODWORD(qhAddress));
        WRITE_VOLATILE(controller->Registers->HcCommandStatus,
                       READ_VOLATILE(controller->Registers->HcCommandStatus) | OHCI_COMMAND_CONTROL_FILLED);

        // Reset control queue
        controller->TransactionQueueControlIndex = USB_ELEMENT_NO_INDEX;
        controller->TransactionsWaitingControl   = 0;
    } else if (transferType == USBTRANSFER_TYPE_BULK) {
        WRITE_VOLATILE(controller->Registers->HcBulkHeadED, LODWORD(qhAddress));
        WRITE_VOLATILE(controller->Registers->HcCommandStatus,
                       READ_VOLATILE(controller->Registers->HcCommandStatus) | OHCI_COMMAND_BULK_FILLED);

        // Reset bulk queue
        controller->TransactionQueueBulkIndex = USB_ELEMENT_NO_INDEX;
        controller->TransactionsWaitingBulk   = 0;
    }
}

oserr_t
HCITransferFinalize(
        _In_ UsbManagerController_t* controller,
        _In_ UsbManagerTransfer_t*   transfer,
        _In_ bool                    deferredClean)
{
    OhciController_t* ohciController = (OhciController_t*)controller;
    TRACE("OHCITransferFinalize(transfer=%u, deferredClean=%u)", transfer->ID, deferredClean);

    // If it's an asynchronous transfer check for end of link, then we should
    // reload the asynchronous queue
    if (__Transfer_IsAsync(transfer)) {
        // Check if the link of the QH is eol
        reg32_t Status  = READ_VOLATILE(ohciController->Registers->HcCommandStatus);
        reg32_t Control = ohciController->QueuesActive;
        if ((Status & OHCI_COMMAND_CONTROL_FILLED) == 0) {
            if (ohciController->TransactionsWaitingControl != 0) {
                // Conditions for control fulfilled
                OHCIReloadAsynchronous(ohciController, USBTRANSFER_TYPE_CONTROL);
                Status  |= OHCI_COMMAND_CONTROL_FILLED;
                Control |= OHCI_CONTROL_CONTROL_ACTIVE;
            }
            else {
                if (READ_VOLATILE(ohciController->Registers->HcControlHeadED) == 0) {
                    Control &= ~(OHCI_CONTROL_CONTROL_ACTIVE);
                }
            }
        }
        if ((Status & OHCI_COMMAND_BULK_FILLED) == 0) {
            if (ohciController->TransactionsWaitingBulk != 0) {
                // Conditions for bulk fulfilled
                OHCIReloadAsynchronous(ohciController, USBTRANSFER_TYPE_BULK);
                Status  |= OHCI_COMMAND_BULK_FILLED;
                Control |= OHCI_CONTROL_BULK_ACTIVE;
            }
            else {
                if (READ_VOLATILE(ohciController->Registers->HcBulkHeadED) == 0) {
                    Control &= ~(OHCI_CONTROL_BULK_ACTIVE);
                }
            }
        }

        // Update
        WRITE_VOLATILE(ohciController->Registers->HcCommandStatus, Status);
        ohciController->QueuesActive = Control;
    }

    // Don't unlink asynchronous transfers
    if (__Transfer_IsPeriodic(transfer)) {
        UsbManagerChainEnumerate(controller, transfer->RootElement,
                                 USB_CHAIN_DEPTH, HCIPROCESS_REASON_UNLINK, HCIProcessElement, transfer);

        // Only do cleanup if not deferred
        if (!deferredClean) {
            UsbManagerChainEnumerate(controller, transfer->RootElement,
                                     USB_CHAIN_DEPTH, HCIPROCESS_REASON_CLEANUP, HCIProcessElement, transfer);
        }
    } else {
        // Always cleanup asynchronous transfers
        UsbManagerChainEnumerate(controller, transfer->RootElement,
                                 USB_CHAIN_DEPTH, HCIPROCESS_REASON_CLEANUP, HCIProcessElement, transfer);
    }
    return OS_EOK;
}

oserr_t
HCITransferDequeue(
        _In_ UsbManagerTransfer_t* transfer)
{
    OhciController_t* controller;
    TRACE("OHCITransferDequeue(transfer=%u)", transfer->ID);

    controller = (OhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (!controller) {
        return OS_EINVALPARAMS;
    }

    // Mark for unscheduling and
    // enable SOF, ED is not scheduled before
    transfer->State = USBTRANSFER_STATE_UNSCHEDULE;
    WRITE_VOLATILE(controller->Registers->HcInterruptStatus, OHCI_SOF_EVENT);
    WRITE_VOLATILE(controller->Registers->HcInterruptEnable, OHCI_SOF_EVENT);
    return OS_EOK;
}

static inline uintptr_t
__GetAddress(
        _In_ SHMSGTable_t* sgTable,
        _In_ int           index,
        _In_ size_t        offset)
{
    return sgTable->Entries[index].Address + offset;
}

static inline size_t
__GetBytesLeft(
        _In_ SHMSGTable_t* sgTable,
        _In_ int           index,
        _In_ size_t        offset)
{
    return sgTable->Entries[index].Length - offset;
}

static inline void
__CalculateTransferElementMetrics(
        _In_  enum USBTransferType transferType,
        _In_  size_t               maxPacketSize,
        _In_  SHMSGTable_t*        sgTable,
        _In_  uint32_t             sgTableOffset,
        _In_  uint32_t             transferLength,
        _In_  size_t               bytesLeft,
        _Out_ uintptr_t*           dataAddressOut,
        _Out_ uint32_t*            lengthOut)
{
    int       index;
    size_t    offset;
    SHMSGTableOffset(
            sgTable,
            sgTableOffset + (transferLength - bytesLeft),
            &index,
            &offset
    );
    uintptr_t address = __GetAddress(sgTable, index, offset);
    size_t    leftInSG = MIN(bytesLeft, __GetBytesLeft(sgTable, index, offset));

    *dataAddressOut = address;
    if (transferType == USBTRANSFER_TYPE_ISOC) {
        *lengthOut = MIN(leftInSG, maxPacketSize * 8);
    } else {
        *lengthOut =  MIN((0x2000 - (address & (0x1000 - 1))), leftInSG);
    }
}

int
HCITransferElementsNeeded(
        _In_ UsbManagerTransfer_t*     transfer,
        _In_ uint32_t                  transferLength,
        _In_ enum USBTransferDirection direction,
        _In_ SHMSGTable_t*             sgTable,
        _In_ uint32_t                  sgTableOffset)
{
    uint32_t  bytesLeft = transferLength;
    int       tdsNeeded = 0;
    uintptr_t waste;
    uint32_t  count;
    TRACE("OHCITransferElementsNeeded(transfer=%u, length=%u)", transfer->ID, transferLength);

    // In regard to isochronous transfers, we must allocate an
    // additional transfer descriptor, which acts as the 'zero-td'.
    // All isochronous transfers should end with a zero td.
    if (transfer->Type == USBTRANSFER_TYPE_ISOC) {
        tdsNeeded++;
    }
    // Handle control transfers a bit different due to control transfers needing
    // some additional packets.
    else if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        bytesLeft -= sizeof(usb_packet_t);

        // add two additional packets, one for SETUP, and one for ACK
        tdsNeeded += 2;
    }

    while (bytesLeft) {
        __CalculateTransferElementMetrics(
                transfer->Type,
                transfer->MaxPacketSize,
                sgTable,
                sgTableOffset,
                transferLength,
                bytesLeft,
                &waste,
                &count
        );

        bytesLeft -= count;
        tdsNeeded++;

        // If this was the last packet, and the packet was filled, and
        // the transaction is an 'OUT', then we must add a ZLP.
        if (transfer->Type != USBTRANSFER_TYPE_ISOC &&
            bytesLeft == 0 && count == transfer->MaxPacketSize) {
            if (direction == USBTRANSFER_DIRECTION_OUT) {
                tdsNeeded++;
            }
        }
    }
    TRACE("OHCITransferElementsNeeded: %i", tdsNeeded);
    return tdsNeeded;
}

void
HCITransferElementFill(
        _In_ UsbManagerTransfer_t*     transfer,
        _In_ uint32_t                  transferLength,
        _In_ enum USBTransferDirection direction,
        _In_ SHMSGTable_t*             sgTable,
        _In_ uint32_t                  sgTableOffset)
{
    enum TransferElementType ackType   = TRANSFERELEMENT_TYPE_IN;
    uint32_t                 bytesLeft = transferLength;
    int                      ei = 0;
    TRACE("OHCITransferElementFill(transfer=%u, length=%u)", transfer->ID, transferLength);

    // Handle control transfers a bit different due to control transfers needing
    // some additional packets.
    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        __CalculateTransferElementMetrics(
                transfer->Type,
                transfer->MaxPacketSize,
                sgTable,
                sgTableOffset,
                transferLength,
                sizeof(usb_packet_t),
                &transfer->Elements[ei].Data.Address,
                &transfer->Elements[ei].Length
        );
        transfer->Elements[ei].Type = TRANSFERELEMENT_TYPE_SETUP;
        TRACE("OHCITransferElementFill: element[%i] = %u", ei, TRANSFERELEMENT_TYPE_SETUP);
        ei++;
        bytesLeft -= sizeof(usb_packet_t);

        // determine ACK direction, if we have an in data stage, then the
        // ACK stage must be out
        if (bytesLeft && direction == USBTRANSFER_DIRECTION_IN) {
            ackType = TRANSFERELEMENT_TYPE_OUT;
        }
    }

    while (bytesLeft) {
        __CalculateTransferElementMetrics(
                transfer->Type,
                transfer->MaxPacketSize,
                sgTable,
                sgTableOffset,
                transferLength,
                bytesLeft,
                &transfer->Elements[ei].Data.Address,
                &transfer->Elements[ei].Length
        );
        transfer->Elements[ei].Type = __TransferElement_DirectionToType(direction);
        TRACE("OHCITransferElementFill: element[%i] = %u", ei, __TransferElement_DirectionToType(direction));
        bytesLeft -= transfer->Elements[ei].Length;
        ei++;

        // Cases left to handle:
        // Generic: adding ZLP on MPS boundary OUTs
        // Isoc:    adding ZLP
        if (bytesLeft == 0) {
            if (transfer->Type == USBTRANSFER_TYPE_ISOC) {
                transfer->Elements[ei].Type = __TransferElement_DirectionToType(direction);
                TRACE("OHCITransferElementFill: element[%i] = %u", ei, __TransferElement_DirectionToType(direction));
            } else if (direction == USBTRANSFER_DIRECTION_OUT &&
                       transfer->Elements[ei - 1].Length == transfer->MaxPacketSize) {
                transfer->Elements[ei].Type = TRANSFERELEMENT_TYPE_OUT;
                TRACE("OHCITransferElementFill: element[%i] = %u", ei, TRANSFERELEMENT_TYPE_OUT);
            }
            ei++;
        }
    }

    // Finally, handle the ACK stage of control transfers
    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        transfer->Elements[ei].Type = ackType;
        TRACE("OHCITransferElementFill: element[%i] = %u", ei, ackType);
    }
}

static oserr_t
__EnsureQueueHead(
        _In_ OhciController_t*     controller,
        _In_ UsbManagerTransfer_t* transfer)
{
    uint8_t* qh;
    oserr_t  oserr;
    TRACE("__EnsureQueueHead(transfer=%u)", transfer->ID);

    if (transfer->RootElement != NULL) {
        return OS_EOK;
    }

    oserr = UsbSchedulerAllocateElement(
            controller->Base.Scheduler,
            OHCI_QH_POOL,
            &qh
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = OHCIQHInitialize(
            controller,
            transfer,
            transfer->Address.DeviceAddress,
            transfer->Address.EndpointAddress
    );
    if (oserr != OS_EOK) {
        // No bandwidth, serious.
        UsbSchedulerFreeElement(controller->Base.Scheduler, qh);
        return oserr;
    }
    transfer->RootElement = qh;
    return OS_EOK;
}

static void
__DestroyDescriptors(
        _In_ OhciController_t*     controller,
        _In_ UsbManagerTransfer_t* transfer)
{
    TRACE("__DestroyDescriptors(transfer=%u)", transfer->ID);
    UsbManagerChainEnumerate(
            &controller->Base,
            transfer->RootElement,
            USB_CHAIN_DEPTH,
            HCIPROCESS_REASON_CLEANUP,
            HCIProcessElement,
            transfer
    );
}

static int
__RemainingDescriptorCount(
        _In_ UsbManagerTransfer_t* transfer)
{
    if (__Transfer_IsAsync(transfer)) {
        return transfer->ElementCount - transfer->TData.Async.ElementsCompleted;
    }
    return transfer->ElementCount;
}

static int
__AllocateDescriptors(
        _In_ OhciController_t*     controller,
        _In_ UsbManagerTransfer_t* transfer,
        _In_ int                   descriptorPool)
{
    OhciQueueHead_t* qh           = transfer->RootElement;
    int              tdsRemaining = __RemainingDescriptorCount(transfer);
    int              tdsAllocated = 0;
    TRACE("__AllocateDescriptors(transfer=%u)", transfer->ID);
    for (int i = 0; i < tdsRemaining; i++) {
        uint8_t* element;
        oserr_t oserr = UsbSchedulerAllocateElement(
                controller->Base.Scheduler,
                OHCI_TD_POOL,
                &element
        );
        if (oserr != OS_EOK) {
            break;
        }

        oserr = UsbSchedulerChainElement(
                controller->Base.Scheduler,
                OHCI_QH_POOL,
                (uint8_t*)qh,
                descriptorPool,
                element,
                qh->Object.DepthIndex,
                USB_CHAIN_DEPTH
        );
        if (oserr != OS_EOK) {
            UsbSchedulerFreeElement(controller->Base.Scheduler, element);
            break;
        }
        tdsAllocated++;
    }

    // For periodic transfers, we must be able to allocate all
    // requested transfer descriptors.
    if (__Transfer_IsPeriodic(transfer) && tdsAllocated != tdsRemaining) {
        __DestroyDescriptors(controller, transfer);
        return 0;
    }
    return tdsAllocated;
}

struct __PrepareContext {
    UsbManagerTransfer_t* Transfer;
    int                   Toggle;
    int                   TDIndex;
    int                   LastTDIndex;
};

static bool
__PrepareDescriptor(
        _In_ UsbManagerController_t* controllerBase,
        _In_ uint8_t*                element,
        _In_ enum HCIProcessReason   reason,
        _In_ void*                   userContext)
{
    struct __PrepareContext*  context = userContext;
    OhciTransferDescriptor_t* td      = (OhciTransferDescriptor_t*)element;
    _CRT_UNUSED(controllerBase);
    _CRT_UNUSED(reason);
    TRACE("__PrepareDescriptor(transfer=%u)", context->Transfer->ID);

    // Handle special stuff for Control transfers. They have special needs.
    if (context->Transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        if (context->TDIndex == 0) {
            // SETUP(0)
            context->Toggle = 0;
        } else if (context->TDIndex == 1) {
            // DATA(1) for the first stage
            context->Toggle = 1;
        } else if (context->TDIndex == (context->Transfer->ElementCount - 1)) {
            // STATUS(1)
            context->Toggle = 1;
        }
    }

    switch (context->Transfer->Elements[context->TDIndex].Type) {
        case TRANSFERELEMENT_TYPE_SETUP: {
            OHCITDSetup(
                    td,
                    context->Transfer->Elements[context->TDIndex].Data.Address
            );
        } break;
        case TRANSFERELEMENT_TYPE_IN: {
            OHCITDData(
                    td,
                    context->Transfer->Type,
                    OHCI_TD_IN,
                    context->Transfer->Elements[context->TDIndex].Data.Address,
                    context->Transfer->Elements[context->TDIndex].Length,
                    context->Toggle
            );
        } break;
        case TRANSFERELEMENT_TYPE_OUT: {
            OHCITDData(
                    td,
                    context->Transfer->Type,
                    OHCI_TD_OUT,
                    context->Transfer->Elements[context->TDIndex].Data.Address,
                    context->Transfer->Elements[context->TDIndex].Length,
                    context->Toggle
            );
        } break;
    }

    if (context->TDIndex == context->LastTDIndex) {
        td->Flags         &= ~(OHCI_TD_IOC_NONE);
        td->OriginalFlags = td->Flags;
    }
    context->TDIndex++;
    context->Toggle ^= 1;
    return true;
}

static bool
__PrepareIsochronousDescriptor(
        _In_ UsbManagerController_t* controllerBase,
        _In_ uint8_t*                element,
        _In_ enum HCIProcessReason   reason,
        _In_ void*                   userContext)
{
    struct __PrepareContext*      context = userContext;
    OhciIsocTransferDescriptor_t* iTD     = (OhciIsocTransferDescriptor_t*)element;
    _CRT_UNUSED(controllerBase);
    _CRT_UNUSED(reason);
    TRACE("__PrepareIsochronousDescriptor(transfer=%u)", context->Transfer->ID);

    switch (context->Transfer->Elements[context->TDIndex].Type) {
        case TRANSFERELEMENT_TYPE_IN: {
            OHCITDIsochronous(
                    iTD,
                    context->Transfer->Type,
                    OHCI_TD_IN,
                    context->Transfer->Elements[context->TDIndex].Data.Address,
                    context->Transfer->Elements[context->TDIndex].Length
            );
        } break;
        case TRANSFERELEMENT_TYPE_OUT: {
            OHCITDIsochronous(
                    iTD,
                    context->Transfer->Type,
                    OHCI_TD_OUT,
                    context->Transfer->Elements[context->TDIndex].Data.Address,
                    context->Transfer->Elements[context->TDIndex].Length
            );
        } break;
        default:
            return false;
    }

    if (context->TDIndex == context->LastTDIndex) {
        iTD->Flags         &= ~(OHCI_TD_IOC_NONE);
        iTD->OriginalFlags = iTD->Flags;
    }
    context->TDIndex++;
    return true;
}

static int
__ElementsCompleted(
        _In_ UsbManagerTransfer_t* transfer)
{
    if (__Transfer_IsAsync(transfer)) {
        return transfer->TData.Async.ElementsCompleted;
    }
    return 0;
}

static void
__PrepareTransferDescriptors(
        _In_ OhciController_t*     controller,
        _In_ UsbManagerTransfer_t* transfer,
        _In_ int                   count)
{
    struct __PrepareContext context = {
            .Transfer = transfer,
            .Toggle = UsbManagerGetToggle(&controller->Base, &transfer->Address),
            .TDIndex = __ElementsCompleted(transfer),
            .LastTDIndex = (__ElementsCompleted(transfer) + count - 1)
    };
    if (transfer->Type == USBTRANSFER_TYPE_ISOC) {
        UsbManagerChainEnumerate(
                &controller->Base,
                transfer->RootElement,
                USB_CHAIN_DEPTH,
                HCIPROCESS_REASON_NONE,
                __PrepareIsochronousDescriptor,
                &context
        );
    } else {
        UsbManagerChainEnumerate(
                &controller->Base,
                transfer->RootElement,
                USB_CHAIN_DEPTH,
                HCIPROCESS_REASON_NONE,
                __PrepareDescriptor,
                &context
        );
        UsbManagerSetToggle(&controller->Base, &transfer->Address, context.Toggle);
    }
}

oserr_t
HCITransferQueue(
        _In_ UsbManagerTransfer_t* transfer)
{
    OhciController_t* controller;
    oserr_t           oserr;
    int               tdsReady;
    TRACE("OHCITransferQueue(transfer=%u)", transfer->ID);

    controller = (OhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (controller == NULL) {
        return OS_ENOENT;
    }

    oserr = __EnsureQueueHead(controller, transfer);
    if (oserr != OS_EOK) {
        return oserr;
    }

    tdsReady = __AllocateDescriptors(controller, transfer, OHCI_TD_POOL);
    if (!tdsReady) {
        transfer->State = USBTRANSFER_STATE_WAITING;
        return OS_EOK;
    }

    __PrepareTransferDescriptors(controller, transfer, tdsReady);
    __DispatchTransfer(controller, transfer);
    return OS_EOK;
}

oserr_t
HCITransferQueueIsochronous(
        _In_ UsbManagerTransfer_t* transfer)
{
    OhciController_t* controller;
    oserr_t           oserr;
    int               tdsReady;
    TRACE("OHCITransferQueueIsochronous(transfer=%u)", transfer->ID);

    controller = (OhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (controller == NULL) {
        return OS_ENOENT;
    }

    oserr = __EnsureQueueHead(controller, transfer);
    if (oserr != OS_EOK) {
        return oserr;
    }

    tdsReady = __AllocateDescriptors(controller, transfer, OHCI_iTD_POOL);
    if (!tdsReady) {
        transfer->State = USBTRANSFER_STATE_WAITING;
        return OS_EOK;
    }

    __PrepareTransferDescriptors(controller, transfer, tdsReady);
    __DispatchTransfer(controller, transfer);
    return OS_EOK;
}
