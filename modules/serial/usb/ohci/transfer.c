/**
 * Copyright 2023, Philip Meulengracht
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
 */

//#define __TRACE
//#define __DIAGNOSE

#define __need_minmax
#include <assert.h>
#include <ddk/barrier.h>
#include <os/mollenos.h>
#include <ddk/utils.h>

#include "ohci.h"

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

    // Schedule the ED on the next SOF interrupt.
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
    if (oserr != OS_EOK) {
        ERROR("OHCI: invalid asynchronous queue index %u", index);
        return;
    }

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

static void
__UnlinkAsync(
    _In_ OhciController_t* controller,
    _In_ UsbManagerTransfer_t* transfer)
{
    UsbSchedulerPool_t* pool = &controller->Base.Scheduler->Settings.Pools[OHCI_QH_POOL];
    OhciQueueHead_t*    target = (OhciQueueHead_t*)transfer->RootElement;
    OhciQueueHead_t*    current;
    OhciQueueHead_t*    previous = NULL;
    volatile reg32_t*   head;
    uint16_t*           waiting;
    int*                count;
    uintptr_t           address;
    uintptr_t           targetAddress = UsbSchedulerGetDma(pool, (uint8_t*)target);
    uint16_t            index;

    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        head = &controller->Registers->HcControlHeadED;
        waiting = &controller->TransactionQueueControlIndex;
        count = &controller->TransactionsWaitingControl;
    } else {
        head = &controller->Registers->HcBulkHeadED;
        waiting = &controller->TransactionQueueBulkIndex;
        count = &controller->TransactionsWaitingBulk;
    }

    // The pool mapping must be contiguous. Convert device links using its
    // cached base addresses to avoid SHM lookups or kernel translation here.
    address = READ_VOLATILE(*head);
    while (address != 0) {
        current = (OhciQueueHead_t*)(pool->ElementPool + address - pool->ElementPoolDeviceAddress);
        if (current == target) {
            if (previous == NULL) {
                WRITE_VOLATILE(*head, target->LinkPointer);
            } else {
                previous->LinkPointer = target->LinkPointer;
                previous->Object.BreathIndex = target->Object.BreathIndex;
            }
            break;
        }
        previous = current;
        address = current->LinkPointer;
    }
    
    previous = NULL;
    index = *waiting;
    
    while (index != USB_ELEMENT_NO_INDEX) {
        current = (OhciQueueHead_t*)USB_ELEMENT_INDEX(pool, index);
        if (current == target) {
            if (previous == NULL) {
                *waiting = target->Object.BreathIndex;
            } else {
                previous->LinkPointer = target->LinkPointer;
                previous->Object.BreathIndex = target->Object.BreathIndex;
            }
            (*count)--;
            break;
        }
        previous = current;
        index = current->Object.BreathIndex;
    }
    
    if (controller->Base.State.DmaStopped) {
        // DMA must be stopped before clearing the current ED. If it still
        // points to the cancelled ED, restart from the updated head register.
        if (transfer->Type == USBTRANSFER_TYPE_CONTROL &&
            READ_VOLATILE(controller->Registers->HcControlCurrentED) == targetAddress) {
            WRITE_VOLATILE(controller->Registers->HcControlCurrentED, 0);
        }
        
        if (transfer->Type == USBTRANSFER_TYPE_BULK &&
            READ_VOLATILE(controller->Registers->HcBulkCurrentED) == targetAddress) {
            WRITE_VOLATILE(controller->Registers->HcBulkCurrentED, 0);
        }
    }
}

oserr_t
HCITransferFinalize(
    _In_ UsbManagerController_t* controller,
    _In_ UsbManagerTransfer_t*   transfer,
    _In_ bool                    deferredClean)
{
    OhciController_t* legacy = (OhciController_t*)controller;
    bool              resume = false;
    uint16_t          frame;
    oserr_t           status;
    _CRT_UNUSED(deferredClean);

    if (transfer->RootElement == NULL) {
        return OS_EOK;
    }

    if (transfer->Dma.Linked && !controller->State.DmaStopped &&
        (transfer->ResultCode != USBTRANSFERCODE_SUCCESS ||
         (transfer->Flags & __USBTRANSFER_FLAG_SHORT))) {
        // An error, cancellation or short packet may leave later packets active.
        // Stop DMA before returning the buffer to the CPU or reusing descriptors.
        status = OHCIStop(legacy);
        if (status != OS_EOK) {
            return status;
        }
        resume = controller->State.Phase < USB_CONTROLLER_DESTROY_PENDING;
    }
    
    if (transfer->Dma.Linked && !transfer->Dma.Unlinked) {
        
        if (__Transfer_IsAsync(transfer)) {
            __UnlinkAsync(legacy, transfer);
        } else {
            UsbManagerChainEnumerate(controller, transfer->RootElement, USB_CHAIN_DEPTH,
                HCIPROCESS_REASON_UNLINK, HCIProcessElement, transfer);
        }
        
        dma_wmb();
        transfer->Dma.Unlinked = true;
        transfer->Dma.ReleaseEpoch = (uint16_t)(READ_VOLATILE(legacy->Registers->HcFmNumber) + 2);
        
        // If the controller has finished the list, start any waiting batch
        // while the removed ED waits for its descriptors to be safe to reuse.
        if (legacy->TransactionsWaitingControl != 0 &&
            !(READ_VOLATILE(legacy->Registers->HcCommandStatus) & OHCI_COMMAND_CONTROL_FILLED)) {
            OHCIReloadAsynchronous(legacy, USBTRANSFER_TYPE_CONTROL);
        }
        
        if (legacy->TransactionsWaitingBulk != 0 &&
            !(READ_VOLATILE(legacy->Registers->HcCommandStatus) & OHCI_COMMAND_BULK_FILLED)) {
            OHCIReloadAsynchronous(legacy, USBTRANSFER_TYPE_BULK);
        }
    }

    if (transfer->Dma.Linked && !controller->State.DmaStopped) {
        // The controller may still have the removed link cached even though
        // the TDs are inactive. Wait two frames before reusing the descriptors.
        frame = READ_VOLATILE(legacy->Registers->HcFmNumber);
        if ((int16_t)(frame - (uint16_t)transfer->Dma.ReleaseEpoch) < 0) {
            // Retry at the next SOF so pending requests can use the freed
            // descriptors without waiting for the one-second recovery timer.
            WRITE_VOLATILE(legacy->Registers->HcInterruptStatus, OHCI_SOF_EVENT);
            WRITE_VOLATILE(legacy->Registers->HcInterruptEnable, OHCI_SOF_EVENT);
            return OS_EBUSY;
        }
    }

    UsbManagerChainEnumerate(
        controller,
        transfer->RootElement,
        USB_CHAIN_DEPTH,
        HCIPROCESS_REASON_CLEANUP,
        HCIProcessElement,
        transfer
    );
    transfer->RootElement = NULL;
    transfer->ChainLength = 0;
    transfer->Dma.Linked = false;
    
    if (resume) {
        OHCISetMode(legacy, OHCI_CONTROL_ACTIVE);
    }
    return OS_EOK;
}

oserr_t
HCIEndpointReset(
    _In_ UsbManagerController_t* controller,
    _In_ USBAddress_t*           address)
{
    UsbManagerSetToggle(controller, address, 0);
    return OS_EOK;
}

oserr_t
HCIDeviceDetach(
    _In_ UsbManagerController_t* controller,
    _In_ USBAddress_t*           address)
{
    _CRT_UNUSED(controller);
    _CRT_UNUSED(address);
    return OS_EOK;
}

oserr_t
HCITransferDequeue(
    _In_ UsbManagerTransfer_t* transfer)
{
    transfer->ResultCode = USBTRANSFERCODE_CANCELLED;
    transfer->State = USBTRANSFER_STATE_CLEANUP;
    return OS_EOK;
}

static inline uintptr_t
__GetAddress(
    _In_ UsbManagerTransfer_t* dmaTransfer,
    _In_ int           index,
    _In_ size_t        offset)
{
    return dmaTransfer->Dma.Segments[index].Address + offset;
}

static inline size_t
__GetBytesLeft(
    _In_ UsbManagerTransfer_t* dmaTransfer,
    _In_ int           index,
    _In_ size_t        offset)
{
    return dmaTransfer->Dma.Segments[index].Length - offset;
}

static inline oserr_t
__CalculateTransferElementMetrics(
    _In_ enum USBTransferType    transferType,
    _In_ size_t                  maxPacketSize,
    _In_ UsbManagerTransfer_t* dmaTransfer,
    _In_ uint32_t                sgTableOffset,
    _In_ uint32_t                transferLength,
    _In_ size_t                  bytesLeft,
    _In_ struct TransferElement* element)
{
    size_t    position = sgTableOffset + transferLength - bytesLeft;
    size_t    offset;
    uint32_t  length = MIN(maxPacketSize, bytesLeft);
    uintptr_t address;
    int       index;
    oserr_t   status;
    _CRT_UNUSED(transferType);

    // Each descriptor describes one packet, which also fits the periodic
    // bounce mapping. An OHCI TD can cross one device-page boundary.
    memset(element, 0, sizeof(*element));
    
    status = USBTransferDmaOffset(dmaTransfer, position, &index, &offset);
    if (status != OS_EOK) {
        return status;
    }

    address = __GetAddress(dmaTransfer, index, offset);
    element->Data.OHCI.Page0 = address & ~(uintptr_t)0xFFF;
    element->Data.OHCI.Offsets[0] = address & 0xFFF;
    
    status = USBTransferDmaOffset(dmaTransfer, position + length - 1, &index, &offset);
    if (status != OS_EOK) {
        return status;
    }
    
    element->Data.OHCI.Page1 = __GetAddress(dmaTransfer, index, offset);
    element->Length = length;
    return OS_EOK;
}

oserr_t
HCITransferElementsNeeded(
    _In_  UsbManagerTransfer_t*     transfer,
    _In_  uint32_t                  transferLength,
    _In_  enum USBTransferDirection direction,
        _Out_ int*                      elementCountOut)
{
    uint32_t  bytesLeft = transferLength;
    int       tdsNeeded = 0;
    TRACE("OHCITransferElementsNeeded(transfer=%u, length=%u)", transfer->ID, transferLength);

    // Control transfers need separate SETUP and ACK descriptors.
    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        bytesLeft -= sizeof(usb_packet_t);
        // Count SETUP and ACK separately from the data packets.
        tdsNeeded += 2;
    }

    while (bytesLeft) {
        struct TransferElement element;
        oserr_t oserr = __CalculateTransferElementMetrics(
            transfer->Type,
            transfer->MaxPacketSize,
            transfer,
            0,
            transferLength,
            bytesLeft,
            &element
        );
        if (oserr != OS_EOK) {
            return oserr;
        }

        bytesLeft -= element.Length;
        tdsNeeded++;

        // A bulk OUT transfer ending with a full packet needs a ZLP.
        if (transfer->Type == USBTRANSFER_TYPE_BULK &&
            bytesLeft == 0 && element.Length == transfer->MaxPacketSize) {
            if (direction == USBTRANSFER_DIRECTION_OUT) {
                tdsNeeded++;
            }
        }
    }
    TRACE("OHCITransferElementsNeeded: %i", tdsNeeded);
    *elementCountOut = tdsNeeded;
    return tdsNeeded == 0 ? OS_EINVALPARAMS : OS_EOK;
}

void
HCITransferElementFill(
    _In_ UsbManagerTransfer_t*     transfer,
    _In_ uint32_t                  transferLength,
    _In_ enum USBTransferDirection direction)
{
    enum TransferElementType ackType   = TRANSFERELEMENT_TYPE_IN;
    uint32_t                 bytesLeft = transferLength;
    int                      ei = 0;
    TRACE("OHCITransferElementFill(transfer=%u, length=%u)", transfer->ID, transferLength);

    // Prepare the SETUP packet before the control transfer's data stage.
    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        oserr_t oserr = __CalculateTransferElementMetrics(
            transfer->Type,
            sizeof(usb_packet_t),
            transfer,
            0,
            sizeof(usb_packet_t),
            sizeof(usb_packet_t),
            &transfer->Elements[ei]
        );
        if (oserr != OS_EOK) {
            return;
        }

        // The first control packet always uses SETUP.
        transfer->Elements[ei].Type = TRANSFERELEMENT_TYPE_SETUP;
        ei++;
        bytesLeft -= sizeof(usb_packet_t);

        // An IN data stage requires an OUT ACK.
        if (bytesLeft && direction == USBTRANSFER_DIRECTION_IN) {
            ackType = TRANSFERELEMENT_TYPE_OUT;
        }
    }

    while (bytesLeft) {
        oserr_t oserr = __CalculateTransferElementMetrics(
            transfer->Type,
            transfer->MaxPacketSize,
            transfer,
            0,
            transferLength,
            bytesLeft,
            &transfer->Elements[ei]
        );
        if (oserr != OS_EOK) {
            return;
        }
        transfer->Elements[ei].Type = __TransferElement_DirectionToType(direction);
        bytesLeft -= transfer->Elements[ei].Length;
        ei++;

        if (bytesLeft == 0 && transfer->Type == USBTRANSFER_TYPE_BULK &&
            direction == USBTRANSFER_DIRECTION_OUT &&
            transfer->Elements[ei - 1].Length == transfer->MaxPacketSize) {
            transfer->Elements[ei++].Type = TRANSFERELEMENT_TYPE_OUT;
        }
    }

    // Append the ACK stage for control transfers.
    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        transfer->Elements[ei].Type = ackType;
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
            (OhciQueueHead_t*)qh
    );
    if (oserr != OS_EOK) {
        // Release the allocated queue head if initialization fails.
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
    if (transfer->RootElement == NULL) {
        return;
    }

    UsbManagerChainEnumerate(
        &controller->Base,
        transfer->RootElement,
        USB_CHAIN_DEPTH,
        HCIPROCESS_REASON_CLEANUP,
        HCIProcessElement,
        transfer
    );
    transfer->RootElement = NULL;
    transfer->ChainLength = 0;
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
    int              i;
    uint8_t*         element;
    oserr_t          oserr;
    TRACE("__AllocateDescriptors(transfer=%u, count=%i)", transfer->ID, tdsRemaining);

    // Only asynchronous requests can be split into batches. The manager keeps
    // their DMA mapping available to the device and queues the next batch
    // after this one completes.
    if (__Transfer_IsAsync(transfer) && tdsRemaining > OHCI_TD_BATCH) {
        tdsRemaining = OHCI_TD_BATCH;
    }
    for (i = 0; i < tdsRemaining; i++) {
        oserr = UsbSchedulerAllocateElement(
                controller->Base.Scheduler,
                descriptorPool,
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

    // Periodic transfers require all descriptors before they can be queued.
    if (__Transfer_IsPeriodic(transfer) && tdsAllocated != tdsRemaining) {
        __DestroyDescriptors(controller, transfer);
        return 0;
    }
    return tdsAllocated;
}

/**
 * @brief Transfer, data toggle and descriptor positions used while preparing a batch.
 */
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
    uintptr_t                 linkAddress;
    _CRT_UNUSED(reason);
    TRACE("__PrepareDescriptor(transfer=%u, i=%i)", context->Transfer->ID, context->TDIndex);

    // The queue head is initialized separately.
    if (element == context->Transfer->RootElement) {
        TRACE("__PrepareDescriptor: skipping queue-head");
        return true;
    }

    // Control transfers start SETUP with DATA0 and the data and status stages
    // with DATA1.
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

    // Before updating the ED head, restore NextTD from the scheduler links.
    // OHCI overwrites NextTD when it adds a descriptor to the done list.
    UsbSchedulerGetPoolElement(
        controllerBase->Scheduler,
        (td->Object.DepthIndex >> USB_ELEMENT_POOL_SHIFT) & USB_ELEMENT_POOL_MASK,
        td->Object.DepthIndex & USB_ELEMENT_INDEX_MASK,
        NULL,
        &linkAddress
    );
    
        td->Link = linkAddress;
    td->Object.Flags &= ~USB_ELEMENT_PROCESSED;

    switch (context->Transfer->Elements[context->TDIndex].Type) {
        case TRANSFERELEMENT_TYPE_SETUP: {
            OHCITDSetup(
                td,
                &context->Transfer->Elements[context->TDIndex]
            );
        } break;
        case TRANSFERELEMENT_TYPE_IN: {
            OHCITDData(
                td,
                context->Transfer->Type,
                OHCI_TD_IN,
                &context->Transfer->Elements[context->TDIndex],
                context->Toggle
            );
        } break;
        case TRANSFERELEMENT_TYPE_OUT: {
            OHCITDData(
                td,
                context->Transfer->Type,
                OHCI_TD_OUT,
                &context->Transfer->Elements[context->TDIndex],
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
    uintptr_t linkAddress;
    _CRT_UNUSED(reason);
    TRACE("__PrepareIsochronousDescriptor(transfer=%u)", context->Transfer->ID);

    if (element == context->Transfer->RootElement) {
        return true;
    }

    // Before updating the ED head, restore NextTD from the scheduler links.
    // OHCI overwrites NextTD when it adds a descriptor to the done list.
    UsbSchedulerGetPoolElement(
        controllerBase->Scheduler,
        (iTD->Object.DepthIndex >> USB_ELEMENT_POOL_SHIFT) & USB_ELEMENT_POOL_MASK,
        iTD->Object.DepthIndex & USB_ELEMENT_INDEX_MASK,
        NULL,
        &linkAddress
    );
    
    iTD->Link = linkAddress;
    iTD->Flags = (READ_VOLATILE(((OhciController_t*)controllerBase)->Registers->HcFmNumber) + 2) & 0xFFFF;
    iTD->Object.Flags &= ~USB_ELEMENT_PROCESSED;

    switch (context->Transfer->Elements[context->TDIndex].Type) {
        case TRANSFERELEMENT_TYPE_IN: {
            OHCITDIsochronous(
                iTD,
                context->Transfer->MaxPacketSize,
                OHCI_TD_IN,
                &context->Transfer->Elements[context->TDIndex]
            );
        } break;
        case TRANSFERELEMENT_TYPE_OUT: {
            OHCITDIsochronous(
                iTD,
                context->Transfer->MaxPacketSize,
                OHCI_TD_OUT,
                &context->Transfer->Elements[context->TDIndex]
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
    TRACE("OHCITransferQueue(transfer=%u)", transfer->ID);

    controller = (OhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (controller == NULL) {
        return OS_ENOENT;
    }

    // If a periodic transfer already has a descriptor chain, reuse it with
    // the existing DMA mapping and bounce page. Prepare the buffer and
    // descriptor fields for the next transfer.
    if (__Transfer_IsPeriodic(transfer) && transfer->RootElement != NULL &&
        transfer->ChainLength != 0) {
        
            oserr = USBTransferDmaBegin(transfer);
        if (oserr != OS_EOK) {
            return oserr;
        }
        
        if (controller->Base.State.Phase >= USB_CONTROLLER_DESTROY_PENDING ||
            transfer->ResultCode == USBTRANSFERCODE_CANCELLED) {
            transfer->ResultCode = USBTRANSFERCODE_CANCELLED;
            transfer->State = USBTRANSFER_STATE_CLEANUP;
            return OS_EBUSY;
        }
        
        __PrepareTransferDescriptors(controller, transfer, transfer->ChainLength);
        HCIProcessEvent(&controller->Base, HCIPROCESS_EVENT_RESET_DONE, transfer);
        __Transfer_Reset(transfer);
        return OS_EOK;
    }

    oserr = __EnsureQueueHead(controller, transfer);
    if (oserr != OS_EOK) {
        return oserr;
    }

    transfer->ChainLength = __AllocateDescriptors(controller, transfer, OHCI_TD_POOL);
    if (!transfer->ChainLength) {
        transfer->State = USBTRANSFER_STATE_WAITING;
        return OS_EOK;
    }

    oserr = USBTransferDmaBegin(transfer);
    if (oserr != OS_EOK) {
        __DestroyDescriptors(controller, transfer);
        transfer->RootElement = NULL;
        transfer->ChainLength = 0;
        return oserr;
    }

    if (controller->Base.State.Phase >= USB_CONTROLLER_DESTROY_PENDING ||
        transfer->ResultCode == USBTRANSFERCODE_CANCELLED) {
        transfer->ResultCode = USBTRANSFERCODE_CANCELLED;
        transfer->State = USBTRANSFER_STATE_CLEANUP;
        return OS_EBUSY;
    }

    __PrepareTransferDescriptors(controller, transfer, transfer->ChainLength);
    transfer->Dma.Unlinked = false;
    transfer->Dma.ReleaseEpoch = 0;
    dma_wmb();
    __DispatchTransfer(controller, transfer);
    return OS_EOK;
}

oserr_t
HCITransferQueueIsochronous(
    _In_ UsbManagerTransfer_t* transfer)
{
    OhciController_t* controller;
    oserr_t           oserr;
    TRACE("OHCITransferQueueIsochronous(transfer=%u)", transfer->ID);

    controller = (OhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (controller == NULL) {
        return OS_ENOENT;
    }

    // If a periodic transfer already has a descriptor chain, reuse it with
    // the existing DMA mapping and bounce page. Prepare the buffer and
    // descriptor fields for the next transfer.
    if (__Transfer_IsPeriodic(transfer) && transfer->RootElement != NULL &&
        transfer->ChainLength != 0) {
        
        oserr = USBTransferDmaBegin(transfer);
        if (oserr != OS_EOK) {
            return oserr;
        }
        
        if (controller->Base.State.Phase >= USB_CONTROLLER_DESTROY_PENDING ||
            transfer->ResultCode == USBTRANSFERCODE_CANCELLED) {
            transfer->ResultCode = USBTRANSFERCODE_CANCELLED;
            transfer->State = USBTRANSFER_STATE_CLEANUP;
            return OS_EBUSY;
        }

        __PrepareTransferDescriptors(controller, transfer, transfer->ChainLength);
        HCIProcessEvent(&controller->Base, HCIPROCESS_EVENT_RESET_DONE, transfer);
        __Transfer_Reset(transfer);
        return OS_EOK;
    }

    oserr = __EnsureQueueHead(controller, transfer);
    if (oserr != OS_EOK) {
        return oserr;
    }

    transfer->ChainLength = __AllocateDescriptors(controller, transfer, OHCI_iTD_POOL);
    if (!transfer->ChainLength) {
        transfer->State = USBTRANSFER_STATE_WAITING;
        return OS_EOK;
    }

    oserr = USBTransferDmaBegin(transfer);
    if (oserr != OS_EOK) {
        __DestroyDescriptors(controller, transfer);
        transfer->RootElement = NULL;
        transfer->ChainLength = 0;
        return oserr;
    }

    if (controller->Base.State.Phase >= USB_CONTROLLER_DESTROY_PENDING ||
        transfer->ResultCode == USBTRANSFERCODE_CANCELLED) {
        transfer->ResultCode = USBTRANSFERCODE_CANCELLED;
        transfer->State = USBTRANSFER_STATE_CLEANUP;
        return OS_EBUSY;
    }
    
    __PrepareTransferDescriptors(controller, transfer, transfer->ChainLength);
    transfer->Dma.Unlinked = false;
    transfer->Dma.ReleaseEpoch = 0;
    dma_wmb();
    __DispatchTransfer(controller, transfer);
    return OS_EOK;
}
