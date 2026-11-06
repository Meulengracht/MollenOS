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
#include <ddk/utils.h>
#include <ddk/barrier.h>
#include <limits.h>
#include "uhci.h"

static void
__DispatchTransfer(
    _In_ UhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer)
{
#ifdef __TRACE
    UsbManagerDumpChain(&Controller->Base, Transfer, (uint8_t*)Transfer->EndpointDescriptor, USB_CHAIN_DEPTH);
#ifdef __DIAGNOSE
    for(;;);
#endif
#endif

    transfer->Dma.Linked = true;
    transfer->State = USBTRANSFER_STATE_QUEUED;
    UhciUpdateCurrentFrame(controller);

    UsbManagerChainEnumerate(
            &controller->Base,
            transfer->RootElement,
            USB_CHAIN_DEPTH,
            HCIPROCESS_REASON_LINK,
            HCIProcessElement,
            transfer
    );
}

oserr_t
HCITransferFinalize(
        _In_ UsbManagerController_t* controller,
        _In_ UsbManagerTransfer_t*   transfer,
        _In_ bool                    deferredClean)
{
    UhciController_t* legacy = (UhciController_t*)controller;
    bool              resume = false;
    uint32_t          frame;
    oserr_t           status;
    _CRT_UNUSED(deferredClean);

    if (transfer->RootElement == NULL) {
        return OS_EOK;
    }

    if (transfer->Dma.Linked && !controller->State.DmaStopped &&
        (transfer->ResultCode != USBTRANSFERCODE_SUCCESS ||
         (transfer->Flags & __USBTRANSFER_FLAG_SHORT))) {
        // Errors, cancellation and short packets may leave later TDs active.
        // Stop the controller before returning the payload to the CPU or reusing TDs.
        status = UhciStop(legacy);
        if (status != OS_EOK) {
            return status;
        }
        resume = controller->State.Phase < USB_CONTROLLER_DESTROY_PENDING;
    }

    if (transfer->Dma.Linked && !transfer->Dma.Unlinked) {
        UsbManagerChainEnumerate(controller, transfer->RootElement, USB_CHAIN_DEPTH,
            HCIPROCESS_REASON_UNLINK, HCIProcessElement, transfer);
        dma_wmb();
        transfer->Dma.Unlinked = true;
        transfer->Dma.ReleaseEpoch = legacy->Frame + 2;
    }

    if (transfer->Dma.Linked && !controller->State.DmaStopped) {
        // Inactive TDs no longer access the payload. Wait two frames before
        // reusing descriptors because the controller may have cached the removed link.
        frame = legacy->Frame;
        if ((int32_t)(frame - transfer->Dma.ReleaseEpoch) < 0) {
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
        UhciStart(legacy, 0);
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

static bool
__PacketAddress(
    _In_  UsbManagerTransfer_t* transfer,
    _In_  size_t                offset,
    _In_  size_t                length,
    _Out_ uintptr_t*            addressOut)
{
    int index;
    size_t segmentOffset;

    UsbTransferDmaOffset(transfer, offset, &index, &segmentOffset);
    
    // A TD has one buffer pointer, so the packet needs consecutive device
    // addresses. A page boundary within the same DMA segment is valid.
    *addressOut = (uintptr_t)(transfer->Dma.Segments[index].Address + segmentOffset);
    return length <= transfer->Dma.Segments[index].Length - segmentOffset;
}

oserr_t
HCITransferElementsNeeded(
    _In_  UsbManagerTransfer_t*     transfer,
    _In_  uint32_t                  transferLength,
    _In_  enum USBTransferDirection direction,
    _Out_ int*                      elementCountOut)
{
    uint32_t dataLength = transferLength;
    uint64_t count;

    if (transfer->MaxPacketSize == 0 || transfer->MaxPacketSize > UHCI_TD_MAXLENGTH) {
        return OS_EINVALPARAMS;
    }

    if (__Transfer_IsAsync(transfer) && transfer->MaxPacketSize > USB_BOUNCE_SLOT_SIZE) {
        return OS_EINVALPARAMS;
    }

    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        if (transferLength < sizeof(usb_packet_t)) {
            return OS_EINVALPARAMS;
        }
        dataLength -= sizeof(usb_packet_t);
    }

    // Keep each USB packet in one descriptor. Splitting it between DMA
    // segments would send a short packet to the device.
    count = dataLength / transfer->MaxPacketSize;
    if (dataLength % transfer->MaxPacketSize != 0) {
        count++;
    }

    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        count += 2;
    } else if (transfer->Type == USBTRANSFER_TYPE_BULK &&
        direction == USBTRANSFER_DIRECTION_OUT && dataLength != 0 &&
        dataLength % transfer->MaxPacketSize == 0) {
        count++;
    }

    if (count == 0 || count > INT_MAX) {
        return OS_EINVALPARAMS;
    }

    *elementCountOut = (int)count;
    return OS_EOK;
}

void
HCITransferElementFill(
    _In_ UsbManagerTransfer_t*     transfer,
    _In_ uint32_t                  transferLength,
    _In_ enum USBTransferDirection direction)
{
    struct TransferElement*  element;
    enum TransferElementType ackType = TRANSFERELEMENT_TYPE_IN;
    size_t                   offset = 0;
    uint32_t                 remaining = transferLength;
    uint32_t                 length = 0;
    int                      index = 0;

    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        element = &transfer->Elements[index++];
        element->Type = TRANSFERELEMENT_TYPE_SETUP;
        element->Length = sizeof(usb_packet_t);
        element->NeedsBounce = !__PacketAddress(transfer, 0, element->Length, &element->Data.Address);
        
        offset += element->Length;
        remaining -= element->Length;
        if (remaining != 0 && direction == USBTRANSFER_DIRECTION_IN) {
            ackType = TRANSFERELEMENT_TYPE_OUT;
        }
    }

    while (remaining != 0) {
        length = MIN(remaining, transfer->MaxPacketSize);
        element = &transfer->Elements[index++];
        
        element->Type = __TransferElement_DirectionToType(direction);
        element->Length = length;
        element->NeedsBounce = !__PacketAddress(transfer, offset, length, &element->Data.Address);
        
        offset += length;
        remaining -= length;
    }

    // Zero-length status and bulk packets have no payload or bounce slot.
    // The element array was cleared before filling, including their lengths.
    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        transfer->Elements[index].Type = ackType;
    } else if (transfer->Type == USBTRANSFER_TYPE_BULK &&
        direction == USBTRANSFER_DIRECTION_OUT && length == transfer->MaxPacketSize) {
        transfer->Elements[index].Type = TRANSFERELEMENT_TYPE_OUT;
    }
}

static oserr_t
__EnsureQueueHead(
        _In_ UhciController_t*     controller,
        _In_ UsbManagerTransfer_t* transfer)
{
    uint8_t* qh;
    oserr_t  oserr;

    if (transfer->RootElement != NULL) {
        return OS_EOK;
    }

    oserr = UsbSchedulerAllocateElement(
            controller->Base.Scheduler,
            UHCI_QH_POOL,
            &qh
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = UHCIQHInitialize(
            controller,
            transfer,
            (UhciQueueHead_t*)qh
    );
    if (oserr != OS_EOK) {
        // Release the queue head if initialization fails.
        UsbSchedulerFreeElement(controller->Base.Scheduler, qh);
        return oserr;
    }
    transfer->RootElement = qh;
    return OS_EOK;
}

static void
__DestroyDescriptors(
        _In_ UhciController_t*     controller,
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

static bool
__AllocateBandwidth(
        _In_ UhciController_t*     controller,
        _In_ UsbManagerTransfer_t* transfer,
        _In_ uint8_t*              element)
{
    oserr_t oserr = UsbSchedulerAllocateBandwidth(
            controller->Base.Scheduler,
            transfer->TData.Periodic.Interval,
            transfer->MaxPacketSize,
            __Transfer_Direction(transfer),
            __Transfer_Length(transfer),
            USBTRANSFER_TYPE_ISOC,
            transfer->Speed,
            element
    );
    if (oserr != OS_EOK) {
        return false;
    }
    return true;
}

static int
__AllocateDescriptors(
        _In_ UhciController_t*     controller,
        _In_ UsbManagerTransfer_t* transfer)
{
    uint16_t* markerPtr    = NULL;
    int       rootPool     = UHCI_QH_POOL;
    int       tdsRemaining = __RemainingDescriptorCount(transfer);
    int       tdsAllocated = 0;
    TRACE("__AllocateDescriptors(transfer=%u, count=%i)", transfer->ID, tdsRemaining);

    if (transfer->RootElement != NULL) {
        markerPtr = &((UhciQueueHead_t*)transfer->RootElement)->Object.DepthIndex;
    }
    for (int i = 0; i < tdsRemaining; i++) {
        uint8_t* element;
        oserr_t oserr = UsbSchedulerAllocateElement(
                controller->Base.Scheduler,
                UHCI_TD_POOL,
                &element
        );
        if (oserr != OS_EOK) {
            break;
        }

        // Without a queue head, the first TD becomes the root of the chain.
        if (transfer->RootElement == NULL) {
            if (!__AllocateBandwidth(controller, transfer, element)) {
                UsbSchedulerFreeElement(controller->Base.Scheduler, element);
                break;
            }

            transfer->RootElement = element;
            rootPool = UHCI_TD_POOL;
            markerPtr = &((UhciTransferDescriptor_t*)element)->Object.DepthIndex;
            tdsAllocated++;
            continue;
        }

        oserr = UsbSchedulerChainElement(
                controller->Base.Scheduler,
                rootPool,
                transfer->RootElement,
                UHCI_TD_POOL,
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

    // Periodic transfers require the full descriptor chain before scheduling.
    if (__Transfer_IsPeriodic(transfer) && tdsAllocated != tdsRemaining) {
        __DestroyDescriptors(controller, transfer);
        return 0;
    }
    return tdsAllocated;
}

/**
 * @brief Transfer, data toggle and TD indices used while preparing the chain.
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
    UhciTransferDescriptor_t* td      = (UhciTransferDescriptor_t*)element;
    _CRT_UNUSED(controllerBase);
    _CRT_UNUSED(reason);

    if (element == context->Transfer->RootElement &&
        context->Transfer->Type != USBTRANSFER_TYPE_ISOC) {
        return true;
    }

    // Control transfers require fixed toggles for SETUP, first DATA and STATUS.
    if (context->Transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        if (context->TDIndex == 0) {
            // SETUP(0)
            context->Toggle = 0;
        } else if (context->TDIndex == 1) {
            // First DATA(1)
            context->Toggle = 1;
        } else if (context->TDIndex == (context->Transfer->ElementCount - 1)) {
            // STATUS(1)
            context->Toggle = 1;
        }
    }

    td->Object.Flags &= ~USB_ELEMENT_PROCESSED;

    switch (context->Transfer->Elements[context->TDIndex].Type) {
        case TRANSFERELEMENT_TYPE_SETUP: {
            UHCITDSetup(
                    td,
                    context->Transfer->Address.DeviceAddress,
                    context->Transfer->Address.EndpointAddress,
                    context->Transfer->Speed,
                    context->Transfer->Elements[context->TDIndex].Data.Address
            );
        } break;
        case TRANSFERELEMENT_TYPE_IN: {
            UHCITDData(
                    td,
                    context->Transfer->Type,
                    UHCI_TD_PID_IN,
                    context->Transfer->Address.DeviceAddress,
                    context->Transfer->Address.EndpointAddress,
                    context->Transfer->Speed,
                    context->Transfer->Elements[context->TDIndex].Data.Address,
                    context->Transfer->Elements[context->TDIndex].Length,
                    context->Toggle
            );
        } break;
        case TRANSFERELEMENT_TYPE_OUT: {
            UHCITDData(
                    td,
                    context->Transfer->Type,
                    UHCI_TD_PID_OUT,
                    context->Transfer->Address.DeviceAddress,
                    context->Transfer->Address.EndpointAddress,
                    context->Transfer->Speed,
                    context->Transfer->Elements[context->TDIndex].Data.Address,
                    context->Transfer->Elements[context->TDIndex].Length,
                    context->Toggle
            );
        } break;
    }

    if (context->TDIndex == context->LastTDIndex) {
        td->Flags        |= UHCI_TD_IOC;
        td->OriginalFlags = td->Flags;
    }
    context->TDIndex++;
    context->Toggle ^= 1;
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
        _In_ UhciController_t*     controller,
        _In_ UsbManagerTransfer_t* transfer,
        _In_ int                   count)
{
    struct __PrepareContext context = {
            .Transfer = transfer,
            .Toggle = UsbManagerGetToggle(&controller->Base, &transfer->Address),
            .TDIndex = __ElementsCompleted(transfer),
            .LastTDIndex = (__ElementsCompleted(transfer) + count - 1)
    };
    UsbManagerChainEnumerate(
            &controller->Base,
            transfer->RootElement,
            USB_CHAIN_DEPTH,
            HCIPROCESS_REASON_NONE,
            __PrepareDescriptor,
            &context
    );
    if (transfer->Type != USBTRANSFER_TYPE_ISOC) {
        UsbManagerSetToggle(&controller->Base, &transfer->Address, context.Toggle);
    }
}

oserr_t
HCITransferQueue(
        _In_ UsbManagerTransfer_t* transfer)
{
    UhciController_t* controller;
    oserr_t           oserr;

    controller = (UhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (controller == NULL) {
        return OS_ENOENT;
    }

    // Linked periodic descriptors keep their mapping and bounce page.
    // Begin DMA again before preparing them for the next transfer.
    if (__Transfer_IsPeriodic(transfer) && transfer->RootElement != NULL &&
        transfer->ChainLength != 0) {

            oserr = UsbTransferDmaBegin(transfer);
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

    // Reserve all bounce slots before allocating descriptors. If slots are
    // unavailable, leave both slots and TDs free so other transfers can finish
    // and free the resources this request needs.
    oserr = UsbTransferBounceReserve(transfer);
    if (oserr == OS_EBUSY) {
        transfer->State = USBTRANSFER_STATE_WAITING;
        return OS_EOK;
    }
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __EnsureQueueHead(controller, transfer);
    if (oserr != OS_EOK) {
        return oserr;
    }

    transfer->ChainLength = __AllocateDescriptors(controller, transfer);
    if (!transfer->ChainLength) {
        transfer->State = USBTRANSFER_STATE_WAITING;
        return OS_EOK;
    }

    oserr = UsbTransferDmaBegin(transfer);
    if (oserr != OS_EOK) {
        __DestroyDescriptors(controller, transfer);
        transfer->RootElement = NULL;
        transfer->ChainLength = 0;
        
        if (oserr == OS_EBUSY && transfer->Dma.Packets != NULL) {
            transfer->State = USBTRANSFER_STATE_WAITING;
            return OS_EOK;
        }
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
    UhciController_t* controller;
    oserr_t           oserr;

    controller = (UhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (controller == NULL) {
        return OS_ENOENT;
    }

    // Linked periodic descriptors keep their mapping and bounce page.
    // Begin DMA again before preparing them for the next transfer.
    if (__Transfer_IsPeriodic(transfer) && transfer->RootElement != NULL &&
        transfer->ChainLength != 0) {

            oserr = UsbTransferDmaBegin(transfer);
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

    transfer->ChainLength = __AllocateDescriptors(controller, transfer);
    if (!transfer->ChainLength) {
        transfer->State = USBTRANSFER_STATE_WAITING;
        return OS_EOK;
    }

    oserr = UsbTransferDmaBegin(transfer);
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
