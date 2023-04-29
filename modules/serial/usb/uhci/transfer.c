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
#include <os/shm.h>
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
    TRACE("UHCITransferFinalize(Id %u)", Transfer->ID);

    UsbManagerChainEnumerate(
            controller,
            transfer->RootElement,
            USB_CHAIN_DEPTH,
            HCIPROCESS_REASON_UNLINK,
            HCIProcessElement,
            transfer
    );
    if (!deferredClean) {
        UsbManagerChainEnumerate(
                controller,
                transfer->RootElement,
                USB_CHAIN_DEPTH,
                HCIPROCESS_REASON_CLEANUP,
                HCIProcessElement,
                transfer
        );
    }
    return OS_EOK;
}

oserr_t
HCITransferDequeue(
        _In_ UsbManagerTransfer_t* transfer)
{
    TRACE("UHCITransferDequeue(transfer=%u)", transfer->ID);
    // Mark for unscheduling on next interrupt/check
    transfer->State = USBTRANSFER_STATE_UNSCHEDULE;
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
        _In_  size_t        maxPacketSize,
        _In_  SHMSGTable_t* sgTable,
        _In_  uint32_t      sgTableOffset,
        _In_  uint32_t      transferLength,
        _In_  size_t        bytesLeft,
        _Out_ uintptr_t*    dataAddressOut,
        _Out_ uint32_t*     lengthOut)
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
    size_t    leftInTD = MIN(UHCI_TD_MAXLENGTH, 0x1000 - (address % 0x1000));
    size_t    maxAllowed = MIN(leftInTD, leftInSG);

    // Number of bytes to transfer are limited by the following factors:
    // Number of bytes left in the current SG page
    // Number of bytes left on the actual page (can't cross boundaries)
    // Number of bytes specified by MPS, and lastly,
    // Number of bytes allowed by the UHCI TD.

    *dataAddressOut = address;
    *lengthOut = MIN(maxPacketSize, maxAllowed);
}

int
HCITransferElementsNeeded(
        _In_ UsbManagerTransfer_t*     transfer,
        _In_ uint32_t                  transferLength,
        _In_ enum USBTransferDirection direction,
        _In_ SHMSGTable_t*             sgTable,
        _In_ uint32_t                  sgTableOffset)
{
    uintptr_t waste;
    uint32_t  count;
    uint32_t  bytesLeft = transferLength;
    int       tdsNeeded = 0;
    TRACE("UHCITransferElementsNeeded(transfer=%u, length=%u)", transfer->ID, transferLength);

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
    TRACE("UHCITransferElementsNeeded: %i", tdsNeeded);
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
    TRACE("UHCITransferElementFill(transfer=%u, length=%u)", transfer->ID, transferLength);

    // Handle control transfers a bit different due to control transfers needing
    // some additional packets.
    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        __CalculateTransferElementMetrics(
                transfer->MaxPacketSize,
                sgTable,
                sgTableOffset,
                transferLength,
                bytesLeft,
                &transfer->Elements[ei].Data.Address,
                &transfer->Elements[ei].Length
        );

        // Override the type and length, as those are fixed for the setup packaet
        transfer->Elements[ei].Type = TRANSFERELEMENT_TYPE_SETUP;
        transfer->Elements[ei].Length = sizeof(usb_packet_t);
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
                transfer->MaxPacketSize,
                sgTable,
                sgTableOffset,
                transferLength,
                bytesLeft,
                &transfer->Elements[ei].Data.Address,
                &transfer->Elements[ei].Length
        );
        transfer->Elements[ei].Type = __TransferElement_DirectionToType(direction);
        bytesLeft -= transfer->Elements[ei].Length;
        ei++;

        // Cases left to handle:
        // Generic: adding ZLP on MPS boundary OUTs
        // Isoc:    adding ZLP
        if (bytesLeft == 0) {
            if (transfer->Type == USBTRANSFER_TYPE_ISOC) {
                transfer->Elements[ei++].Type = __TransferElement_DirectionToType(direction);
            } else if (direction == USBTRANSFER_DIRECTION_OUT &&
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
        // No bandwidth, serious.
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

        // Support headless transfers by setting that to a queue head
        if (transfer->RootElement == NULL) {
            if (!__AllocateBandwidth(controller, transfer, element)) {
                UsbSchedulerFreeElement(controller->Base.Scheduler, element);
                break;
            }

            transfer->RootElement = element;
            rootPool = UHCI_TD_POOL;
            markerPtr = &((UhciTransferDescriptor_t*)element)->Object.DepthIndex;
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
    UhciTransferDescriptor_t* td      = (UhciTransferDescriptor_t*)element;
    _CRT_UNUSED(controllerBase);
    _CRT_UNUSED(reason);

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

    oserr = __EnsureQueueHead(controller, transfer);
    if (oserr != OS_EOK) {
        return oserr;
    }

    transfer->ChainLength = __AllocateDescriptors(controller, transfer);
    if (!transfer->ChainLength) {
        transfer->State = USBTRANSFER_STATE_WAITING;
        return OS_EOK;
    }

    __PrepareTransferDescriptors(controller, transfer, transfer->ChainLength);
    __DispatchTransfer(controller, transfer);
    return OS_EOK;
}

oserr_t
HCITransferQueueIsochronous(
        _In_ UsbManagerTransfer_t* transfer)
{
    UhciController_t* controller;

    controller = (UhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (controller == NULL) {
        return OS_ENOENT;
    }

    transfer->ChainLength = __AllocateDescriptors(controller, transfer);
    if (!transfer->ChainLength) {
        transfer->State = USBTRANSFER_STATE_WAITING;
        return OS_EOK;
    }

    __PrepareTransferDescriptors(controller, transfer, transfer->ChainLength);
    __DispatchTransfer(controller, transfer);
    return OS_EOK;
}
