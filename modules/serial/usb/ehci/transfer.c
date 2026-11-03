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
#include <ddk/utils.h>
#include <ddk/barrier.h>
#include <string.h>
#include <stdlib.h>

#include "ehci.h"

#define __XACTION_COUNT(_t) (((_t)->Type == USBTRANSFER_TYPE_ISOC) ? 8 : 5)

static void
__DispatchTransfer(
    _In_ EhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer)
{
#ifdef __TRACE
    UsbManagerDumpChain(&Controller->Base, Transfer, Transfer->RootElement, USB_CHAIN_DEPTH);
#ifdef __DIAGNOSE
    for(;;);
#endif
#endif
    
    Transfer->Dma.Linked = true;
    Transfer->State = USBTRANSFER_STATE_QUEUED;

    UsbManagerChainEnumerate(
        &Controller->Base,
        Transfer->RootElement,
        USB_CHAIN_DEPTH,
        HCIPROCESS_REASON_LINK,
        HCIProcessElement,
        Transfer
    );
}

oserr_t
HCITransferFinalize(
    _In_ UsbManagerController_t* controller,
    _In_ UsbManagerTransfer_t*   transfer,
    _In_ bool                    deferredClean)
{
    EhciController_t* ehci = (EhciController_t*)controller;
    reg32_t           command = 0;
    reg32_t           interrupts = 0;
    bool              resume = false;
    oserr_t           status;
    _CRT_UNUSED(deferredClean);

    if (transfer->RootElement == NULL) {
        return OS_EOK;
    }

    if (transfer->Dma.Linked && !controller->State.DmaStopped &&
        (!__Transfer_IsAsync(transfer) || transfer->ResultCode != USBTRANSFERCODE_SUCCESS ||
         (transfer->Flags & __USBTRANSFER_FLAG_SHORT))) {
        // Cancelled, short or failed transfers may still have active descriptors.
        // Periodic schedules have no async-advance acknowledgement, so halt
        // the controller before unlinking these chains. Resume the other
        // endpoints after cleanup if the controller is still in use.
        if (!ehci->ResumePending) {
            ehci->ResumeCommand = READ_VOLATILE(ehci->OpRegisters->UsbCommand);
            ehci->ResumeInterrupts = READ_VOLATILE(ehci->OpRegisters->UsbIntr);
            ehci->ResumePending = true;
        }

        status = EhciHalt(ehci);
        if (status != OS_EOK) {
            return status;
        }
        
        resume = controller->State.Phase < USB_CONTROLLER_DESTROY_PENDING;
    }

    if (transfer->Dma.Linked && !transfer->Dma.Unlinked) {
        UsbManagerChainEnumerate(
            controller,
            transfer->RootElement,
            USB_CHAIN_DEPTH,
            HCIPROCESS_REASON_UNLINK,
            HCIProcessElement,
            transfer
        );
        transfer->Dma.Unlinked = true;
        if (!controller->State.DmaStopped) {
            transfer->Dma.ReleaseEpoch = ehci->AdvanceIssued + 1;
            ehci->AdvanceQueued = transfer->Dma.ReleaseEpoch;
            EhciRingDoorbell(ehci);
        }
    }

    if (!controller->State.DmaStopped && transfer->Dma.Linked &&
        (int32_t)(ehci->AdvanceCompleted - transfer->Dma.ReleaseEpoch) < 0) {
        return OS_EBUSY;
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
        // The controller has halted, so earlier async epochs are complete.
        ehci->AdvanceCompleted = ehci->AdvanceQueued;
        ehci->AdvanceIssued = ehci->AdvanceQueued;
        ehci->AdvancePending = false;
        WRITE_VOLATILE(ehci->OpRegisters->UsbStatus, EHCI_STATUS_ASYNC_DOORBELL);
        
        atomic_fetch_and(&controller->InterruptStatus, ~EHCI_STATUS_ASYNC_DOORBELL);
        command = ehci->ResumeCommand;
        interrupts = ehci->ResumeInterrupts;
        ehci->ResumePending = false;
        dma_wmb();
        
        controller->State.DmaStopped = false;
        WRITE_VOLATILE(ehci->OpRegisters->UsbIntr, interrupts);
        WRITE_VOLATILE(ehci->OpRegisters->UsbCommand, command & ~EHCI_COMMAND_IOC_ASYNC_DOORBELL);
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

struct EhciDetachContext {
    USBAddress_t Address;
};

static int
__DetachTransfer(
    _In_ UsbManagerController_t* controller,
    _In_ UsbManagerTransfer_t*   transfer,
    _In_ void*                   userContext)
{
    struct EhciDetachContext* context = userContext;

    if (transfer->Address.DeviceAddress != context->Address.DeviceAddress ||
        transfer->Address.HubAddress != context->Address.HubAddress ||
        transfer->Address.PortAddress != context->Address.PortAddress) {
        return 0;
    }

    // Mark the transfer cancelled before scheduling descriptor cleanup.
    transfer->ResultCode = USBTRANSFERCODE_CANCELLED;
    if (__Transfer_IsAsync(transfer)) {
        transfer->TData.Async.ElementsCompleted = transfer->ElementCount;
    }
    HCITransferDequeue(transfer);
    _CRT_UNUSED(controller);
    return 0;
}

oserr_t
HCIDeviceDetach(
    _In_ UsbManagerController_t* controller,
    _In_ USBAddress_t*           address)
{
    if (controller == NULL || address == NULL) {
        return OS_EINVALPARAMS;
    }

    struct EhciDetachContext context = { .Address = *address };

    // Cancel transfers for this USB address. HCITransferDequeue marks them
    // for cleanup because the controller may still be using their descriptors.
    UsbManagerIterateTransfers(
            controller,
            __DetachTransfer,
            &context
    );

    // Cleanup must wait for the controller to halt or acknowledge the
    // async-advance doorbell before freeing linked descriptors.
    UsbManagerProcessTransfers(controller);
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

static inline void
__CalculateTransferElementMetrics(
    _In_ UsbManagerTransfer_t*   transfer,
    _In_ UsbManagerTransfer_t*   dmaTransfer,
    _In_ uint32_t                sgTableOffset,
    _In_ uint32_t                transferLength,
    _In_ uint32_t                bytesLeft,
    _In_ struct TransferElement* element)
{
    size_t    position = sgTableOffset + transferLength - bytesLeft;
    size_t    offset;
    size_t    span;
    uintptr_t address;
    uint32_t  length = 0;
    int       index;
    int       i;

    memset(element, 0, sizeof(struct TransferElement));
    
    // Periodic requests use a page-aligned staging packet. Async qTDs have
    // five page pointers, so split contiguous DMA segments at page boundaries.
    for (i = 0; i < __XACTION_COUNT(transfer) && bytesLeft != 0; i++) {
        USBTransferDmaOffset(dmaTransfer, position, &index, &offset);
        address = __GetAddress(dmaTransfer, index, offset);
        
        span = MIN(bytesLeft, __GetBytesLeft(dmaTransfer, index, offset));
        span = MIN(span, 0x1000 - (address & 0xFFF));
        
        if (transfer->Type == USBTRANSFER_TYPE_ISOC) {
            span = MIN(span, transfer->MaxPacketSize * (transfer->TData.Periodic.Bandwith + 1));
        }
        
        element->Data.EHCI.Addresses[i] = address;
        element->Data.EHCI.Lengths[i] = span;
        
        length += span;
        position += span;
        bytesLeft -= span;
    }

    if (bytesLeft != 0 && transfer->Type != USBTRANSFER_TYPE_ISOC) {
        // If the request continues in another qTD, end this descriptor on a
        // packet boundary to avoid sending a short packet. The next qTD
        // starts at the first byte removed here.
        span = length % transfer->MaxPacketSize;
        length -= span;
        
        while (span != 0) {
            i--;
            offset = MIN(span, element->Data.EHCI.Lengths[i]);
            element->Data.EHCI.Lengths[i] -= offset;
            span -= offset;
        }
    }
    element->Length = length;
}

oserr_t
HCITransferElementsNeeded(
    _In_  UsbManagerTransfer_t*     transfer,
    _In_  uint32_t                  transferLength,
    _In_  enum USBTransferDirection direction,
    _Out_ int*                      elementCountOut)
{
    struct TransferElement element;
    uint32_t               bytesLeft = transferLength;
    int                    tdsNeeded = 0;
    TRACE("EHCITransferElementsNeeded(transfer=%u, length=%u)", transfer->ID, transferLength);

    // Control transfers need SETUP and ACK packets around the data stage.
    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        if (transferLength < sizeof(usb_packet_t)) {
            return OS_EINVALPARAMS;
        }
        bytesLeft -= sizeof(usb_packet_t);
        // Count the SETUP and ACK packets.
        tdsNeeded += 2;
    }

    while (bytesLeft) {
        __CalculateTransferElementMetrics(
                transfer,
                transfer,
                0,
                transferLength,
                bytesLeft,
                &element
        );

        bytesLeft -= element.Length;
        tdsNeeded++;

        // Add a ZLP for bulk OUT if the last element is one full packet.
        if (transfer->Type == USBTRANSFER_TYPE_BULK &&
            bytesLeft == 0 && element.Length == transfer->MaxPacketSize) {
            if (direction == USBTRANSFER_DIRECTION_OUT) {
                tdsNeeded++;
            }
        }
    }
    TRACE("EHCITransferElementsNeeded: %i", tdsNeeded);
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
    TRACE("EHCITransferElementFill(transfer=%u, length=%u)", transfer->ID, transferLength);

    // Fill the SETUP packet before the control transfer's data stage.
    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        __CalculateTransferElementMetrics(
            transfer,
            transfer,
            0,
            sizeof(usb_packet_t),
            sizeof(usb_packet_t),
            &transfer->Elements[ei]
        );

        // The SETUP packet has a fixed type and length.
        transfer->Elements[ei].Type = TRANSFERELEMENT_TYPE_SETUP;
        transfer->Elements[ei].Length = sizeof(usb_packet_t);
        ei++;
        bytesLeft -= sizeof(usb_packet_t);

        // An IN data stage requires an OUT ACK.
        if (bytesLeft && direction == USBTRANSFER_DIRECTION_IN) {
            ackType = TRANSFERELEMENT_TYPE_OUT;
        }
    }

    while (bytesLeft) {
        __CalculateTransferElementMetrics(
            transfer,
            transfer,
            0,
            transferLength,
            bytesLeft,
            &transfer->Elements[ei]
        );
        transfer->Elements[ei].Type = __TransferElement_DirectionToType(direction);
        bytesLeft -= transfer->Elements[ei].Length;
        ei++;

        // Add a ZLP for bulk OUT if the last element is one full packet.
        if (transfer->Type == USBTRANSFER_TYPE_BULK && bytesLeft == 0) {
            if (direction == USBTRANSFER_DIRECTION_OUT &&
                    transfer->Elements[ei - 1].Length == transfer->MaxPacketSize) {
                transfer->Elements[ei++].Type = TRANSFERELEMENT_TYPE_OUT;
            }
        }
    }

    // Finish control transfers with the ACK stage.
    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        transfer->Elements[ei].Type = ackType;
    }
}

static oserr_t
__EnsureQueueHead(
    _In_ EhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer)
{
    uint8_t* qh;
    oserr_t  oserr;

    if (transfer->RootElement != NULL) {
        return OS_EOK;
    }

    oserr = UsbSchedulerAllocateElement(
        controller->Base.Scheduler,
        EHCI_QH_POOL,
        &qh
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = EHCIQHInitialize(
        controller,
        transfer,
        (EhciQueueHead_t*)qh,
        transfer->Address.DeviceAddress,
        transfer->Address.EndpointAddress
    );
    if (oserr != OS_EOK) {
        // Free the queue head if initialization fails to reserve bandwidth.
        UsbSchedulerFreeElement(controller->Base.Scheduler, qh);
        return oserr;
    }
    transfer->RootElement = qh;
    return OS_EOK;
}

static void
__DestroyDescriptors(
    _In_ EhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer)
{
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

static bool
__AllocateBandwidth(
    _In_ EhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer,
    _In_  uint8_t*              element,
    _In_  size_t                elementLength)
{
    oserr_t oserr;

    // For non-high-speed isochronous transfers, reserve bandwidth for the
    // siTD's start-split and complete-split transactions through the TT.
    if (transfer->Type == USBTRANSFER_TYPE_ISOC && transfer->Speed != USBSPEED_HIGH) {
        uint8_t startMask;
        uint8_t completeMask;
        uint8_t completionOffset;
        bool    isOut = __Transfer_Direction(transfer) == USBTRANSFER_DIRECTION_OUT;
        
        // OUT sends consecutive start-splits of up to 188 bytes without a
        // complete-split. IN uses one start-split followed by a complete-split
        // window.
        uint8_t startSplitCount = isOut ?
                (uint8_t)MIN(6, MAX(1, (elementLength + 187) / 188)) : 1;
        size_t bandwidth = UsbSchedulerCalculateBandwidth(
                transfer->Speed,
                __Transfer_Direction(transfer),
                USBTRANSFER_TYPE_ISOC,
                elementLength);

        oserr = UsbSchedulerAllocateSplitBandwidth(
                controller->Base.Scheduler,
                transfer->TData.Periodic.Interval,
                bandwidth,
                bandwidth,
                startSplitCount,
                !isOut,
                element,
                &startMask,
                &completeMask,
                &completionOffset);
        if (oserr == OS_EOK) {
            // Save the masks and bandwidth in the siTD so cleanup can release
            // the bandwidth reserved for each split phase.
            EhciSplitIsochronousDescriptor_t* siTD = (EhciSplitIsochronousDescriptor_t*)element;
            siTD->FrameStartMask = startMask;
            siTD->FrameCompletionMask = completeMask;
            siTD->CompletionFrameOffset = completionOffset;
            siTD->StartBandwidth = bandwidth;
            siTD->CompleteBandwidth = isOut ? 0 : bandwidth;
        }
    } else {
        oserr = UsbSchedulerAllocateBandwidth(
                controller->Base.Scheduler,
                transfer->TData.Periodic.Interval,
                transfer->MaxPacketSize,
                __Transfer_Direction(transfer),
                __Transfer_Length(transfer),
                USBTRANSFER_TYPE_ISOC,
                transfer->Speed,
                element
        );
    }
    if (oserr != OS_EOK) {
        return false;
    }
    return true;
}

static uint16_t*
__MarkerPointer(
    _In_ const uint8_t* element,
    _In_ int            descriptorPool)
{
    switch (descriptorPool) {
        case EHCI_QH_POOL: {
            return &((EhciQueueHead_t*)element)->Object.DepthIndex;
        }
        case EHCI_TD_POOL: {
            return &((EhciTransferDescriptor_t*)element)->Object.DepthIndex;
        }
        case EHCI_iTD_POOL: {
            return &((EhciIsochronousDescriptor_t*)element)->Object.DepthIndex;
        }
        case EHCI_siTD_POOL: {
            return &((EhciSplitIsochronousDescriptor_t*)element)->Object.DepthIndex;
        }
        default:
            return NULL;
    }
}

static void
__CopySplitSchedule(
    _In_ const EhciSplitIsochronousDescriptor_t* source,
    _In_ EhciSplitIsochronousDescriptor_t*       destination)
{
    destination->Object.FrameInterval = source->Object.FrameInterval;
    destination->Object.StartFrame = source->Object.StartFrame;
    destination->Object.FrameMask = source->Object.FrameMask;
    destination->FrameStartMask = source->FrameStartMask;
    destination->FrameCompletionMask = source->FrameCompletionMask;
    destination->CompletionFrameOffset = source->CompletionFrameOffset;
}

static int
__AllocateDescriptors(
    _In_ EhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer,
    _In_ int                   descriptorPool)
{
    uint16_t* markerPtr    = NULL;
    int       rootPool     = EHCI_QH_POOL;
    int       tdsRemaining = __RemainingDescriptorCount(transfer);
    int       tdsAllocated = 0;

    if (transfer->RootElement != NULL) {
        // Isochronous chains start with an iTD or siTD. Control, bulk and
        // interrupt chains start with a QH.
        rootPool = transfer->Type == USBTRANSFER_TYPE_ISOC ? descriptorPool : EHCI_QH_POOL;
        markerPtr = __MarkerPointer(transfer->RootElement, rootPool);
    }
    
    for (int i = 0; i < tdsRemaining; i++) {
        uint8_t* element;
        oserr_t oserr = UsbSchedulerAllocateElement(
            controller->Base.Scheduler,
            descriptorPool,
            &element
        );
        if (oserr != OS_EOK) {
            break;
        }

        // Isochronous transfers have no QH. Use the first iTD or siTD as the
        // chain root and reserve bandwidth for the transfer.
        if (transfer->RootElement == NULL) {
            size_t elementLength = transfer->ElementCount > i ?
                    transfer->Elements[i].Length : __Transfer_Length(transfer);
            if (!__AllocateBandwidth(controller, transfer, element, elementLength)) {
                UsbSchedulerFreeElement(controller->Base.Scheduler, element);
                break;
            }

            transfer->RootElement = element;
            rootPool = descriptorPool;
            markerPtr = __MarkerPointer(element, descriptorPool);
            tdsAllocated++;
            continue;
        }

        if (descriptorPool == EHCI_siTD_POOL) {
            __CopySplitSchedule(
                    (EhciSplitIsochronousDescriptor_t*)transfer->RootElement,
                    (EhciSplitIsochronousDescriptor_t*)element
            );
        }

        oserr = UsbSchedulerChainElement(
                controller->Base.Scheduler,
                rootPool,
                transfer->RootElement,
                descriptorPool,
                element,
                *markerPtr,
                USB_CHAIN_DEPTH
        );
        if (oserr != OS_EOK) {
            UsbSchedulerFreeElement(controller->Base.Scheduler, element);
            break;
        }
        tdsAllocated++;
    }

    // Allocate the complete chain before preparing the transfer. Free a
    // partial chain so the next retry starts with ChainLength and the
    // completed element count in agreement.
    if (tdsAllocated != tdsRemaining) {
        __DestroyDescriptors(controller, transfer);
        transfer->RootElement = NULL;
        return 0;
    }
    return tdsAllocated;
}

/**
 * @brief Tracks the transfer element index, data toggle and errors while
 * preparing a descriptor chain.
 */
struct __PrepareContext {
    UsbManagerTransfer_t* Transfer;
    int                   Toggle;
    int                   TDIndex;
    int                   LastTDIndex;
    bool                  Failed;
};

static bool
__PrepareDescriptor(
    _In_ UsbManagerController_t* controllerBase,
    _In_ uint8_t*                element,
    _In_ enum HCIProcessReason   reason,
    _In_ void*                   userContext)
{
    struct __PrepareContext*  context = userContext;
    EhciTransferDescriptor_t* td      = (EhciTransferDescriptor_t*)element;
    _CRT_UNUSED(reason);

    if (element == context->Transfer->RootElement &&
        context->Transfer->Type != USBTRANSFER_TYPE_ISOC) {
        return true;
    }
    // Each descriptor must have a matching transfer element. Stop if the
    // index falls outside the element array.
    if (context->TDIndex < 0 || context->TDIndex >= context->Transfer->ElementCount) {
        context->Failed = true;
        return false;
    }

    // Control transfers use fixed toggles for SETUP, the first data packet
    // and STATUS.
    if (context->Transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        if (context->TDIndex == 0) {
            // SETUP(0)
            context->Toggle = 0;
        } else if (context->TDIndex == 1) {
            // First data packet: DATA1.
            context->Toggle = 1;
        } else if (context->TDIndex == (context->Transfer->ElementCount - 1)) {
            // STATUS(1)
            context->Toggle = 1;
        }
    }

    td->Object.Flags &= ~USB_ELEMENT_PROCESSED;

    switch (context->Transfer->Elements[context->TDIndex].Type) {
        case TRANSFERELEMENT_TYPE_SETUP: {
            EHCITDSetup(
                (EhciController_t*)controllerBase,
                td,
                &context->Transfer->Elements[context->TDIndex]
            );
        } break;
        case TRANSFERELEMENT_TYPE_IN: {
            EHCITDData(
                (EhciController_t*)controllerBase,
                td,
                EHCI_TD_IN,
                &context->Transfer->Elements[context->TDIndex],
                context->Toggle
            );
        } break;
        case TRANSFERELEMENT_TYPE_OUT: {
            EHCITDData(
                (EhciController_t*)controllerBase,
                td,
                EHCI_TD_OUT,
                &context->Transfer->Elements[context->TDIndex],
                context->Toggle
            );
        } break;
    }

    if (context->TDIndex == context->LastTDIndex) {
        td->Token         |= EHCI_TD_IOC;
        td->OriginalToken |= EHCI_TD_IOC;
    }

    // qTDs can contain multiple USB packets. The next qTD uses the toggle
    // after all packets in this element, with a ZLP counting as one packet.
    context->Toggle ^= context->Transfer->Elements[context->TDIndex].Length == 0 ? 1 :
        (DIVUP(context->Transfer->Elements[context->TDIndex].Length, context->Transfer->MaxPacketSize) & 1);
    context->TDIndex++;
    return true;
}

static bool
__PrepareIsochronousDescriptor(
    _In_ UsbManagerController_t* controllerBase,
    _In_ uint8_t*                element,
    _In_ enum HCIProcessReason   reason,
    _In_ void*                   userContext)
{
    struct __PrepareContext*     context = userContext;
    EhciIsochronousDescriptor_t* iTD     = (EhciIsochronousDescriptor_t*)element;
    _CRT_UNUSED(controllerBase);
    _CRT_UNUSED(reason);

    if (context->TDIndex < 0 || context->TDIndex >= context->Transfer->ElementCount) {
        context->Failed = true;
        return false;
    }

    switch (context->Transfer->Elements[context->TDIndex].Type) {
        case TRANSFERELEMENT_TYPE_IN: {
            if (!EHCITDIsochronous(
                    (EhciController_t*)controllerBase,
                    context->Transfer,
                    iTD,
                    EHCI_iTD_IN,
                    context->Transfer->Elements[context->TDIndex].Data.EHCI.Addresses,
                    context->Transfer->Elements[context->TDIndex].Data.EHCI.Lengths
            )) {
                context->Failed = true;
                return false;
            }
        } break;
        case TRANSFERELEMENT_TYPE_OUT: {
            if (!EHCITDIsochronous(
                    (EhciController_t*)controllerBase,
                    context->Transfer,
                    iTD,
                    EHCI_iTD_OUT,
                    context->Transfer->Elements[context->TDIndex].Data.EHCI.Addresses,
                    context->Transfer->Elements[context->TDIndex].Data.EHCI.Lengths
            )) {
                context->Failed = true;
                return false;
            }
        } break;
        default:
            context->Failed = true;
            return false;
    }

    if (context->TDIndex == context->LastTDIndex) {
        int lastTransaction = -1;
        for (int i = 0; i < 8; i++) {
            if (iTD->Transactions[i]) {
                lastTransaction = i;
            }
        }
        if (lastTransaction >= 0) {
            iTD->Transactions[lastTransaction] |= EHCI_iTD_IOC;
            iTD->TransactionsCopy[lastTransaction] |= EHCI_iTD_IOC;
        }
    }
    context->TDIndex++;
    return true;
}

static bool
__PrepareSplitIsochronousDescriptor(
    _In_ UsbManagerController_t* controllerBase,
    _In_ uint8_t*                element,
    _In_ enum HCIProcessReason   reason,
    _In_ void*                   userContext)
{
    struct __PrepareContext*          context = userContext;
    EhciSplitIsochronousDescriptor_t* siTD = (EhciSplitIsochronousDescriptor_t*)element;
    enum TransferElementType          type;
    uint32_t                          pid;

    _CRT_UNUSED(reason);

    if (context->TDIndex < 0 || context->TDIndex >= context->Transfer->ElementCount) {
        context->Failed = true;
        return false;
    }

    type = context->Transfer->Elements[context->TDIndex].Type;
    if (type != TRANSFERELEMENT_TYPE_IN && type != TRANSFERELEMENT_TYPE_OUT) {
        context->Failed = true;
        return false;
    }
    
    pid = type == TRANSFERELEMENT_TYPE_IN ? EHCI_siTD_IN : EHCI_siTD_OUT;
    if (!EHCISiTDInitialize(
            (EhciController_t*)controllerBase,
            context->Transfer,
            siTD,
            pid,
            context->Transfer->Elements[context->TDIndex].Data.EHCI.Addresses,
            context->Transfer->Elements[context->TDIndex].Data.EHCI.Lengths)) {
        context->Failed = true;
        return false;
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

static bool
__PrepareTransferDescriptors(
    _In_ EhciController_t*     controller,
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
        if (transfer->Speed != USBSPEED_HIGH) {
            UsbManagerChainEnumerate(
                &controller->Base,
                transfer->RootElement,
                USB_CHAIN_DEPTH,
                HCIPROCESS_REASON_NONE,
                __PrepareSplitIsochronousDescriptor,
                &context
            );
        } else {
            UsbManagerChainEnumerate(
                &controller->Base,
                transfer->RootElement,
                USB_CHAIN_DEPTH,
                HCIPROCESS_REASON_NONE,
                __PrepareIsochronousDescriptor,
                &context
            );
        }
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
    return !context.Failed;
}

oserr_t
HCITransferQueue(
    _In_ UsbManagerTransfer_t* transfer)
{
    EhciController_t* controller;
    oserr_t           oserr;

    controller = (EhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (controller == NULL) {
        return OS_ENOENT;
    }

    // If the periodic chain is still linked, reuse its DMA mapping and staging
    // page. Prepare the next payload and update the existing descriptors.
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
        
        if (!__PrepareTransferDescriptors(controller, transfer, transfer->ChainLength)) {
            transfer->ResultCode = USBTRANSFERCODE_BUFFERERROR;
            transfer->State = USBTRANSFER_STATE_CLEANUP;
            return OS_EINVALPARAMS;
        }
        
        HCIProcessEvent(&controller->Base, HCIPROCESS_EVENT_RESET_DONE, transfer);
        __Transfer_Reset(transfer);
        return OS_EOK;
    }

    oserr = __EnsureQueueHead(controller, transfer);
    if (oserr != OS_EOK) {
        return oserr;
    }

    transfer->ChainLength = __AllocateDescriptors(controller, transfer, EHCI_TD_POOL);
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
    
    if (!__PrepareTransferDescriptors(controller, transfer, transfer->ChainLength)) {
        __DestroyDescriptors(controller, transfer);
        transfer->RootElement = NULL;
        transfer->State = USBTRANSFER_STATE_WAITING;
        return OS_EINVALPARAMS;
    }

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
    EhciController_t* controller;
    oserr_t           oserr;
    int               descriptorPool;


    controller = (EhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (controller == NULL) {
        return OS_ENOENT;
    }

    // If the periodic chain is still linked, reuse its DMA mapping and staging
    // page. Prepare the next payload and update the existing descriptors.
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
        
        if (!__PrepareTransferDescriptors(controller, transfer, transfer->ChainLength)) {
            transfer->ResultCode = USBTRANSFERCODE_BUFFERERROR;
            transfer->State = USBTRANSFER_STATE_CLEANUP;
            return OS_EINVALPARAMS;
        }
        
        HCIProcessEvent(&controller->Base, HCIPROCESS_EVENT_RESET_DONE, transfer);
        __Transfer_Reset(transfer);
        return OS_EOK;
    }

    descriptorPool = transfer->Speed == USBSPEED_HIGH ? EHCI_iTD_POOL : EHCI_siTD_POOL;
    transfer->ChainLength = __AllocateDescriptors(controller, transfer, descriptorPool);
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

    if (!__PrepareTransferDescriptors(controller, transfer, transfer->ChainLength)) {
        __DestroyDescriptors(controller, transfer);
        transfer->RootElement = NULL;
        transfer->State = USBTRANSFER_STATE_WAITING;
        return OS_EINVALPARAMS;
    }
    
    transfer->Dma.Unlinked = false;
    transfer->Dma.ReleaseEpoch = 0;
    dma_wmb();
    __DispatchTransfer(controller, transfer);
    return OS_EOK;
}
