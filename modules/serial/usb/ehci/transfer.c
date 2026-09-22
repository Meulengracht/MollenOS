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
#include <os/shm.h>
#include "ehci.h"
#include <string.h>
#include <stdlib.h>

#define __XACTION_COUNT(_t)   ((_t)->Type == USBTRANSFER_TYPE_ISOC) ? 8 : 5
#define __XACTION_MAXSIZE(_t) ((_t)->Type == USBTRANSFER_TYPE_ISOC) ? (1024 * MAX(3, (_t)->TData.Periodic.Bandwith)) : 0x1000

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
    Transfer->State = USBTRANSFER_STATE_QUEUED;
    UsbManagerChainEnumerate(&Controller->Base, Transfer->RootElement,
        USB_CHAIN_DEPTH, HCIPROCESS_REASON_LINK, HCIProcessElement, Transfer);
}

oserr_t
HCITransferFinalize(
        _In_ UsbManagerController_t* controller,
        _In_ UsbManagerTransfer_t*   transfer,
        _In_ bool                    deferredClean)
{
    TRACE("EHCITransferFinalize(Id %u)", transfer->ID);

    // Always unlink
    UsbManagerChainEnumerate(
            controller,
            transfer->RootElement,
            USB_CHAIN_DEPTH,
            HCIPROCESS_REASON_UNLINK,
            HCIProcessElement,
            transfer
    );

    // The common manager sends the completion notification when cleanup is finalized.
    if (!deferredClean) {
        UsbManagerChainEnumerate(
                controller,
                transfer->RootElement,
                USB_CHAIN_DEPTH,
                HCIPROCESS_REASON_CLEANUP,
                HCIProcessElement,
                transfer
        );
    } else {
        transfer->State = USBTRANSFER_STATE_CLEANUP;
        EhciRingDoorbell((EhciController_t*)controller);
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

    // Mark the transfer cancelled before unlinking it. Async transfers keep
    // their descriptors until the EHCI async-advance doorbell is observed.
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

    // Mark transfers for this USB address as cancelled before dequeueing them.
    // HCITransferDequeue owns the unlink and deferred descriptor cleanup
    // sequence for descriptors that may still be visible to the controller.
    UsbManagerIterateTransfers(
            controller,
            __DetachTransfer,
            &context
    );

    // Periodic transfers can be cleaned immediately. Asynchronous transfers
    // remain in CLEANUP until the controller acknowledges the doorbell.
    UsbManagerProcessTransfers(controller);
    return OS_EOK;
}

oserr_t
HCITransferDequeue(
    _In_ UsbManagerTransfer_t* transfer)
{
    EhciController_t* controller;

    controller = (EhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (!controller) {
        return OS_EINVALPARAMS;
    }

    // Waiting transfers may not have a queue head yet. Mark them for the
    // common cleanup path without attempting to walk a null descriptor chain.
    if (transfer->RootElement == NULL) {
        transfer->State = USBTRANSFER_STATE_CLEANUP;
        return OS_EOK;
    }
    
    // Unschedule immediately, but keep data intact as hardware still (might) reference it.
    UsbManagerChainEnumerate(
            &controller->Base,
            transfer->RootElement,
            USB_CHAIN_DEPTH,
            HCIPROCESS_REASON_UNLINK,
            HCIProcessElement,
            transfer
    );

    // Mark transfer for cleanup and ring doorbell if async
    if (__Transfer_IsAsync(transfer)) {
        transfer->State = USBTRANSFER_STATE_CLEANUP;
        EhciRingDoorbell(controller);
    } else {
        UsbManagerChainEnumerate(
                &controller->Base,
                transfer->RootElement,
                USB_CHAIN_DEPTH,
                HCIPROCESS_REASON_CLEANUP,
                HCIProcessElement,
                transfer
        );
        // Descriptors are already freed above, so null the root now. Otherwise
        // __ProcessCleanup would enumerate and free the same (now stale) chain again.
        transfer->RootElement = NULL;
        transfer->State = USBTRANSFER_STATE_CLEANUP;
    }
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
        _In_ UsbManagerTransfer_t*   transfer,
        _In_ SHMSGTable_t*           sgTable,
        _In_ uint32_t                sgTableOffset,
        _In_ uint32_t                transferLength,
        _In_ uint32_t                bytesLeft,
        _In_ struct TransferElement* element)
{
    int    transactionCount = __XACTION_COUNT(transfer);
    size_t transactionMaxSize = __XACTION_MAXSIZE(transfer);
    int    index;
    size_t offset;

    // retrieve the initial index and offset into the SG table, based on the
    // start offset, and the progress into the transaction.
    SHMSGTableOffset(
            sgTable,
            sgTableOffset + (transferLength - bytesLeft),
            &index,
            &offset
    );
    // with the index and offset we can calculate the number of bytes available
    // in the current SG frame, and then we further correct this by the number
    // of bytes we actually want to transfer.
    size_t leftInSG = __GetBytesLeft(sgTable, index, offset);
    size_t byteCount = MIN(bytesLeft, leftInSG);
    size_t calculatedLength = 0;

    for (int i = 0; i < transactionCount; i++) {
        // correct again for the actual number of bytes per transaction
        size_t transactionSize = MIN(byteCount, transactionMaxSize);
        transactionSize = MIN(transactionSize, byteCount);

        // store the transfer metrics
        element->Data.EHCI.Addresses[i] = __GetAddress(sgTable, index, offset);
        element->Data.EHCI.Lengths[i] = transactionSize;

        // correct all our state values, either we run out of space in the current
        // SG frame, or we run out of bytes to transfer.
        calculatedLength += transactionSize;
        leftInSG -= transactionSize;
        bytesLeft -= transactionSize;
        byteCount -= transactionSize;

        // if we run out of space in the SG frame, and we still have bytes left
        // to transfers, then we need to switch SG frame, update our index and
        // offset, and recalculate how many bytes to transfer for this frame.
        if (!leftInSG && bytesLeft) {
            SHMSGTableOffset(
                    sgTable,
                    sgTableOffset + (transferLength - bytesLeft),
                    &index,
                    &offset
            );
            leftInSG = __GetBytesLeft(sgTable, index, offset);
            byteCount = MIN(bytesLeft, leftInSG);
        }
    }
    element->Length = calculatedLength;
}

oserr_t
HCITransferElementsNeeded(
        _In_  UsbManagerTransfer_t*     transfer,
        _In_  uint32_t                  transferLength,
        _In_  enum USBTransferDirection direction,
        _In_  SHMSGTable_t*             sgTable,
        _In_  uint32_t                  sgTableOffset,
        _Out_ int*                      elementCountOut)
{
    struct TransferElement element;
    uint32_t               bytesLeft = transferLength;
    int                    tdsNeeded = 0;
    TRACE("EHCITransferElementsNeeded(transfer=%u, length=%u)", transfer->ID, transferLength);

    // Handle control transfers a bit different due to control transfers needing
    // some additional packets.
    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        if (transferLength < sizeof(usb_packet_t)) {
            return OS_EINVALPARAMS;
        }
        bytesLeft -= sizeof(usb_packet_t);

        // add two additional packets, one for SETUP, and one for ACK
        tdsNeeded += 2;
    }

    while (bytesLeft) {
        __CalculateTransferElementMetrics(
                transfer,
                sgTable,
                sgTableOffset,
                transferLength,
                bytesLeft,
                &element
        );

        bytesLeft -= element.Length;
        tdsNeeded++;

        // If this was the last packet, and the packet was filled, and
        // the transaction is an 'OUT', then we must add a ZLP.
        if (transfer->Type != USBTRANSFER_TYPE_ISOC &&
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
        _In_ enum USBTransferDirection direction,
        _In_ SHMSGTable_t*             sgTable,
        _In_ uint32_t                  sgTableOffset)
{
    enum TransferElementType ackType   = TRANSFERELEMENT_TYPE_IN;
    uint32_t                 bytesLeft = transferLength;
    int                      ei = 0;
    TRACE("EHCITransferElementFill(transfer=%u, length=%u)", transfer->ID, transferLength);

    // Handle control transfers a bit different due to control transfers needing
    // some additional packets.
    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        __CalculateTransferElementMetrics(
                transfer,
                sgTable,
                sgTableOffset,
                transferLength,
                bytesLeft,
                &transfer->Elements[ei]
        );

        // Override the type and length, as those are fixed for the setup packaet
        transfer->Elements[ei].Type = TRANSFERELEMENT_TYPE_SETUP;
        transfer->Elements[ei].Length = sizeof(usb_packet_t);
        transfer->Elements[ei].Data.EHCI.Lengths[0] = sizeof(usb_packet_t);
        for (int i = 1; i < 5; i++) {
            transfer->Elements[ei].Data.EHCI.Lengths[i] = 0;
        }
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
                transfer,
                sgTable,
                sgTableOffset,
                transferLength,
                bytesLeft,
                &transfer->Elements[ei]
        );
        transfer->Elements[ei].Type = __TransferElement_DirectionToType(direction);
        bytesLeft -= transfer->Elements[ei].Length;
        ei++;

        // Cases left to handle:
        // Generic: adding ZLP on MPS boundary OUTs
        if (transfer->Type != USBTRANSFER_TYPE_ISOC && bytesLeft == 0) {
            if (direction == USBTRANSFER_DIRECTION_OUT &&
                    transfer->Elements[ei - 1].Length == transfer->MaxPacketSize) {
                transfer->Elements[ei++].Type = TRANSFERELEMENT_TYPE_OUT;
            }
        }
    }

    // Finally, handle the ACK stage of control transfers
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
        // No bandwidth, serious.
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

    // High-speed iTDs use one microframe-local transaction model. Full/low
    // speed isochronous endpoints instead require an siTD and two-phase TT
    // bandwidth accounting, so select the additive split allocator here.
    if (transfer->Type == USBTRANSFER_TYPE_ISOC && transfer->Speed != USBSPEED_HIGH) {
        uint8_t startMask;
        uint8_t completeMask;
        uint8_t completionOffset;
        bool    isOut = __Transfer_Direction(transfer) == USBTRANSFER_DIRECTION_OUT;
        // An OUT split moves the payload as consecutive 188-byte start-split
        // bus transactions and never uses a complete-split; an IN split is a
        // single start-split followed by a complete-split window.
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
            // Save the scheduler's exact masks and costs in the siTD. Cleanup
            // later uses these values to release both phases symmetrically.
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
        // Async transfers root at a QH, while periodic transfers root at the
        // iTD or siTD pool selected by the transfer speed.
        rootPool = __Transfer_IsAsync(transfer) ? EHCI_QH_POOL : descriptorPool;
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

        // Support headless transfers by making the first descriptor the chain
        // root. Periodic roots are iTDs or siTDs; asynchronous roots are QHs.
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

    // A transfer must be prepared as one complete descriptor chain. Retaining
    // a partial chain would make the next retry disagree with ChainLength and
    // the already-completed element count.
    if (tdsAllocated != tdsRemaining) {
        __DestroyDescriptors(controller, transfer);
        transfer->RootElement = NULL;
        return 0;
    }
    return tdsAllocated;
}

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

    // The scheduler chain and transfer element array must describe the same
    // number of descriptors. Stop preparation if that invariant is broken so
    // malformed input cannot read past the transfer metadata.
    if (context->TDIndex < 0 || context->TDIndex >= context->Transfer->ElementCount) {
        context->Failed = true;
        return false;
    }

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
            EHCITDSetup(
                    (EhciController_t*)controllerBase,
                    td,
                    context->Transfer->Elements[context->TDIndex].Data.Address
            );
        } break;
        case TRANSFERELEMENT_TYPE_IN: {
            EHCITDData(
                    (EhciController_t*)controllerBase,
                    td,
                    EHCI_TD_IN,
                    context->Transfer->Elements[context->TDIndex].Data.Address,
                    context->Transfer->Elements[context->TDIndex].Length,
                    context->Toggle
            );
        } break;
        case TRANSFERELEMENT_TYPE_OUT: {
            EHCITDData(
                    (EhciController_t*)controllerBase,
                    td,
                    EHCI_TD_OUT,
                    context->Transfer->Elements[context->TDIndex].Data.Address,
                    context->Transfer->Elements[context->TDIndex].Length,
                    context->Toggle
            );
        } break;
    }

    if (context->TDIndex == context->LastTDIndex) {
        td->Token         |= EHCI_TD_IOC;
        td->OriginalToken |= EHCI_TD_IOC;
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
    struct __PrepareContext* context = userContext;
    EhciSplitIsochronousDescriptor_t* siTD = (EhciSplitIsochronousDescriptor_t*)element;
    enum TransferElementType type;
    uint32_t pid;

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

    oserr = __EnsureQueueHead(controller, transfer);
    if (oserr != OS_EOK) {
        return oserr;
    }

    transfer->ChainLength = __AllocateDescriptors(controller, transfer, EHCI_TD_POOL);
    if (!transfer->ChainLength) {
        transfer->State = USBTRANSFER_STATE_WAITING;
        return OS_EOK;
    }

    if (!__PrepareTransferDescriptors(controller, transfer, transfer->ChainLength)) {
        __DestroyDescriptors(controller, transfer);
        transfer->RootElement = NULL;
        transfer->State = USBTRANSFER_STATE_WAITING;
        return OS_EINVALPARAMS;
    }
    __DispatchTransfer(controller, transfer);
    return OS_EOK;
}

oserr_t
HCITransferQueueIsochronous(
        _In_ UsbManagerTransfer_t* transfer)
{
    EhciController_t* controller;
    int descriptorPool;


    controller = (EhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (controller == NULL) {
        return OS_ENOENT;
    }

    descriptorPool = transfer->Speed == USBSPEED_HIGH ? EHCI_iTD_POOL : EHCI_siTD_POOL;
    transfer->ChainLength = __AllocateDescriptors(controller, transfer, descriptorPool);
    if (!transfer->ChainLength) {
        transfer->State = USBTRANSFER_STATE_WAITING;
        return OS_EOK;
    }

    if (!__PrepareTransferDescriptors(controller, transfer, transfer->ChainLength)) {
        __DestroyDescriptors(controller, transfer);
        transfer->RootElement = NULL;
        transfer->State = USBTRANSFER_STATE_WAITING;
        return OS_EINVALPARAMS;
    }
    __DispatchTransfer(controller, transfer);
    return OS_EOK;
}
