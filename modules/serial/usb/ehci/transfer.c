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
    Transfer->State = USBTRANSFER_STATE_QUEUED;
#ifdef __TRACE
    UsbManagerDumpChain(&Controller->Base, Transfer, (uint8_t*)Transfer->EndpointDescriptor, USB_CHAIN_DEPTH);
#ifdef __DIAGNOSE
    for(;;);
#endif
#endif
    UsbManagerChainEnumerate(&Controller->Base, Transfer->RootElement,
        USB_CHAIN_DEPTH, HCIPROCESS_REASON_LINK, HCIProcessElement, Transfer);
}

oserr_t
HCITransferFinalize(
        _In_ UsbManagerController_t* controller,
        _In_ UsbManagerTransfer_t*   transfer,
        _In_ bool                    deferredClean)
{
    TRACE("EHCITransferFinalize(Id %u)", Transfer->ID);

    // Always unlink
    UsbManagerChainEnumerate(
            controller,
            transfer->RootElement,
            USB_CHAIN_DEPTH,
            HCIPROCESS_REASON_UNLINK,
            HCIProcessElement,
            transfer
    );

    // Send notification for transfer if control/bulk immediately, but defer
    // cleanup till the doorbell has been rung
    USBTransferNotify(transfer);
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
HCITransferDequeue(
    _In_ UsbManagerTransfer_t* transfer)
{
    EhciController_t* controller;

    controller = (EhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (!controller) {
        return OS_EINVALPARAMS;
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
        _In_ USBTransaction_t*       xaction,
        _In_ SHMSGTable_t*           sgTable,
        _In_ size_t                  bytesLeft,
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
            xaction->BufferOffset + (xaction->Length - bytesLeft),
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
                    xaction->BufferOffset + (xaction->Length - bytesLeft),
                    &index,
                    &offset
            );
            leftInSG = __GetBytesLeft(sgTable, index, offset);
            byteCount = MIN(bytesLeft, leftInSG);
        }
    }
    element->Length = calculatedLength;
}

int
HCITransferElementsNeeded(
        _In_ UsbManagerTransfer_t* transfer,
        _In_ USBTransaction_t      transactions[USB_TRANSACTIONCOUNT],
        _In_ SHMSGTable_t          sgTables[USB_TRANSACTIONCOUNT])
{
    int tdsNeeded = 0;

    // In regard to isochronous transfers, we must allocate an
    // additional transfer descriptor, which acts as the 'zero-td'.
    // All isochronous transfers should end with a zero td.
    if (transfer->Type == USBTRANSFER_TYPE_ISOC) {
        tdsNeeded++;
    }

    for (int i = 0; i < USB_TRANSACTIONCOUNT; i++) {
        uint8_t type = transactions[i].Type;

        if (type == USB_TRANSACTION_SETUP) {
            tdsNeeded++;
        } else if (type == USB_TRANSACTION_IN || type == USB_TRANSACTION_OUT) {
            // Special case: zero length packets
            if (transactions[i].BufferHandle == UUID_INVALID) {
                tdsNeeded++;
            } else {
                struct TransferElement element;
                uint32_t bytesLeft = transactions[i].Length;
                while (bytesLeft) {
                    __CalculateTransferElementMetrics(
                            transfer,
                            &transactions[i],
                            &sgTables[i],
                            bytesLeft,
                            &element
                    );

                    bytesLeft -= element.Length;
                    tdsNeeded++;

                    // If this was the last packet, and the packet was filled, and
                    // the transaction is an 'OUT', then we must add a ZLP.
                    if (transfer->Type != USBTRANSFER_TYPE_ISOC &&
                        bytesLeft == 0 && element.Length == transfer->MaxPacketSize) {
                        if (transactions[i].Type == USB_TRANSACTION_OUT) {
                            tdsNeeded++;
                        }
                    }
                }
            }
        }
    }
    return tdsNeeded;
}

void
HCITransferElementFill(
        _In_ UsbManagerTransfer_t* transfer,
        _In_ USBTransaction_t      transactions[USB_TRANSACTIONCOUNT],
        _In_ SHMSGTable_t          sgTables[USB_TRANSACTIONCOUNT])
{
    for (int i = 0, ei = 0; i < USB_TRANSACTIONCOUNT; i++) {
        uint8_t type = transactions[i].Type;

        if (type == USB_TRANSACTION_SETUP) {
            transfer->Elements[ei].Type = USB_TRANSACTION_SETUP;
            __CalculateTransferElementMetrics(
                    transfer,
                    &transactions[i],
                    &sgTables[i],
                    transactions[i].Length,
                    &transfer->Elements[ei]
            );
            ei++;
        } else if (type == USB_TRANSACTION_IN || type == USB_TRANSACTION_OUT) {
            // Special case: zero length packets
            if (transactions[i].BufferHandle == UUID_INVALID) {
                transfer->Elements[ei++].Type = type;
            } else {
                size_t bytesLeft = transactions[i].Length;
                while (bytesLeft) {
                    __CalculateTransferElementMetrics(
                            transfer,
                            &transactions[i],
                            &sgTables[i],
                            bytesLeft,
                            &transfer->Elements[ei]
                    );

                    bytesLeft -= transfer->Elements[ei++].Length;

                    // Cases left to handle:
                    // Generic: adding ZLP on MPS boundary OUTs
                    // Isoc:    adding ZLP
                    if (bytesLeft == 0) {
                        if (transfer->Type == USBTRANSFER_TYPE_ISOC) {
                            transfer->Elements[ei].Type = type;
                        } else if (transactions[i].Type == USB_TRANSACTION_OUT && transfer->Elements[ei].Length == transfer->MaxPacketSize) {
                            transfer->Elements[ei].Type = USB_TRANSACTION_OUT;
                        }
                    }
                }
            }
        }
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
        markerPtr = &((EhciQueueHead_t*)transfer->RootElement)->Object.DepthIndex;
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

        // Support headless transfers by setting that to a queue head
        if (transfer->RootElement == NULL) {
            if (!__AllocateBandwidth(controller, transfer, element)) {
                UsbSchedulerFreeElement(controller->Base.Scheduler, element);
                break;
            }

            transfer->RootElement = element;
            rootPool = descriptorPool;
            markerPtr = __MarkerPointer(element, descriptorPool);
            continue;
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
    EhciTransferDescriptor_t* td      = (EhciTransferDescriptor_t*)element;
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
        case USB_TRANSACTION_SETUP: {
            EHCITDSetup(
                    (EhciController_t*)controllerBase,
                    td,
                    context->Transfer->Elements[context->TDIndex].Data.Address
            );
        } break;
        case USB_TRANSACTION_IN: {
            EHCITDData(
                    (EhciController_t*)controllerBase,
                    td,
                    EHCI_TD_IN,
                    context->Transfer->Elements[context->TDIndex].Data.Address,
                    context->Transfer->Elements[context->TDIndex].Length,
                    context->Toggle
            );
        } break;
        case USB_TRANSACTION_OUT: {
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

    switch (context->Transfer->Elements[context->TDIndex].Type) {
        case USB_TRANSACTION_IN: {
            EHCITDIsochronous(
                    (EhciController_t*)controllerBase,
                    context->Transfer,
                    iTD,
                    EHCI_iTD_IN,
                    context->Transfer->Elements[context->TDIndex].Data.EHCI.Addresses,
                    context->Transfer->Elements[context->TDIndex].Data.EHCI.Lengths
            );
        } break;
        case USB_TRANSACTION_OUT: {
            EHCITDIsochronous(
                    (EhciController_t*)controllerBase,
                    context->Transfer,
                    iTD,
                    EHCI_iTD_OUT,
                    context->Transfer->Elements[context->TDIndex].Data.EHCI.Addresses,
                    context->Transfer->Elements[context->TDIndex].Data.EHCI.Lengths
            );
        } break;
        default:
            return false;
    }

    if (context->TDIndex == context->LastTDIndex) {
        for (int i = 0; i < 8; i++) {
            if (!iTD->Transactions[i]) {
                iTD->Transactions[i - 1] |= EHCI_iTD_IOC;
                iTD->TransactionsCopy[i - 1] |= EHCI_iTD_IOC;
                break;
            }
        }
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
    EhciController_t* controller;
    oserr_t           oserr;
    int               tdsReady;

    controller = (EhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (controller == NULL) {
        return OS_ENOENT;
    }

    oserr = __EnsureQueueHead(controller, transfer);
    if (oserr != OS_EOK) {
        return oserr;
    }

    tdsReady = __AllocateDescriptors(controller, transfer, EHCI_TD_POOL);
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
    EhciController_t* controller;
    int               tdsReady;

    controller = (EhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (controller == NULL) {
        return OS_ENOENT;
    }

    tdsReady = __AllocateDescriptors(controller, transfer, EHCI_iTD_POOL);
    if (!tdsReady) {
        transfer->State = USBTRANSFER_STATE_WAITING;
        return OS_EOK;
    }

    __PrepareTransferDescriptors(controller, transfer, tdsReady);
    __DispatchTransfer(controller, transfer);
    return OS_EOK;
}
