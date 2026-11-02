/**
 * Copyright, Philip Meulengracht
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

#define __need_minmax
#include <ddk/utils.h>
#include <os/handle.h>
#include <os/shm.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "xhci.h"

static oserr_t
__XhciFreeControllerResources(
    _In_ XhciController_t* controller)
{
    oserr_t status;

    status = DMAFree(&controller->ErstDMA);
    if (status != OS_EOK) {
        return status;
    }
    status = DMAFree(&controller->EventRingDMA);
    if (status != OS_EOK) {
        return status;
    }
    status = XhciRingDestroy(&controller->CommandRing);
    if (status != OS_EOK) {
        return status;
    }
    status = XhciScratchpadsFree(&controller->Scratchpads, controller->ScratchpadCount);
    if (status != OS_EOK) {
        return status;
    }
    status = DMAFree(&controller->ScratchpadArrayDMA);
    if (status != OS_EOK) {
        return status;
    }
    status = DMAFree(&controller->DCBaaDMA);
    if (status != OS_EOK) {
        return status;
    }
    return OS_EOK;
}

oserr_t
XhciQueueInitialize(
    _In_ XhciController_t* controller)
{
    size_t  dcbaaEntries = MIN(controller->SlotCount + 1, XHCI_MAX_DEVICE_CONTEXTS);
    UsbSchedulerSettings_t settings;
    oserr_t                oserr;

    // Hardware reads the separate TRB rings, never these software descriptors.
    UsbSchedulerSettingsCreate(&settings, 1, 1, 0, USB_SCHEDULER_SOFTWARE_ELEMENTS);
    UsbSchedulerSettingsAddPool(
            &settings,
            sizeof(XhciTransferDescriptor_t),
            XHCI_TD_ALIGNMENT,
            XHCI_TD_COUNT,
            0,
            offsetof(XhciTransferDescriptor_t, BreadthLink),
            offsetof(XhciTransferDescriptor_t, DepthLink),
            offsetof(XhciTransferDescriptor_t, Object)
    );

    oserr = UsbSchedulerInitialize(&settings, &controller->Base.Scheduler);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = XhciDmaAllocate(controller->Base.DmaContext,
            dcbaaEntries * sizeof(reg64_t),
            &controller->DCBaaDMA,
            &controller->DCBaaDeviceAddress,
            (void**)&controller->DCBaa
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    if (controller->ScratchpadCount > 0) {
        oserr = XhciDmaAllocate(controller->Base.DmaContext,
                controller->ScratchpadCount * sizeof(reg64_t),
                &controller->ScratchpadArrayDMA,
                &controller->ScratchpadArrayDeviceAddress,
                (void**)&controller->ScratchpadArray
        );
        if (oserr != OS_EOK) {
            return oserr;
        }
        oserr = XhciScratchpadsAllocate(
            controller->Base.DmaContext,
            controller->ScratchpadCount,
            controller->ScratchpadArray,
            &controller->Scratchpads
        );
        if (oserr != OS_EOK) {
            return oserr;
        }
        controller->DCBaa[0] = controller->ScratchpadArrayDeviceAddress;
    }

    oserr = XhciRingInitialize(controller->Base.DmaContext, &controller->CommandRing, XHCI_COMMAND_RING_ENTRIES);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = XhciDmaAllocate(controller->Base.DmaContext,
            XHCI_EVENT_RING_ENTRIES * sizeof(XhciTransferRequestBlock_t),
            &controller->EventRingDMA,
            &controller->EventRingDeviceAddress,
            (void**)&controller->EventRing
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = XhciDmaAllocate(controller->Base.DmaContext,
            XHCI_ERST_ENTRIES * sizeof(XhciEventRingSegmentTableEntry_t),
            &controller->ErstDMA,
            &controller->ErstDeviceAddress,
            (void**)&controller->Erst
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    controller->Erst[0].RingSegmentBaseAddress = controller->EventRingDeviceAddress;
    controller->Erst[0].RingSegmentSize = XHCI_EVENT_RING_ENTRIES;
    controller->EventRingIndex = 0;
    controller->EventRingCycle = 1;
    return OS_EOK;
}

oserr_t
XhciQueueReset(
    _In_ XhciController_t* controller)
{
    oserr_t status;

    UsbManagerClearTransfers(&controller->Base);
    if (controller->Base.TransactionList.head != NULL) {
        return OS_EBUSY;
    }
    status = XhciEndpointDestroyAll(controller);
    if (status != OS_EOK) {
        return status;
    }
    status = XhciDeviceDestroyAll(controller);
    if (status != OS_EOK) {
        return status;
    }
    XhciRingReset(&controller->CommandRing);
    memset(controller->Commands, 0, sizeof(controller->Commands));
    if (controller->EventRing != NULL) {
        memset(controller->EventRing, 0, XHCI_EVENT_RING_ENTRIES * sizeof(XhciTransferRequestBlock_t));
    }
    controller->EventRingIndex = 0;
    controller->EventRingCycle = 1;
    if (controller->Base.Scheduler != NULL) {
        return UsbSchedulerResetInternalData(controller->Base.Scheduler, 1, 0);
    }
    return OS_EOK;
}

oserr_t
XhciQueueDestroy(
    _In_ XhciController_t* controller)
{
    oserr_t status;

    UsbManagerClearTransfers(&controller->Base);
    if (controller->Base.TransactionList.head != NULL) {
        return OS_EBUSY;
    }
    status = XhciEndpointDestroyAll(controller);
    if (status != OS_EOK) {
        return status;
    }
    status = XhciDeviceDestroyAll(controller);
    if (status != OS_EOK) {
        return status;
    }
    status = __XhciFreeControllerResources(controller);
    if (status != OS_EOK) {
        return status;
    }
    if (controller->Base.Scheduler != NULL) {
        UsbSchedulerDestroy(controller->Base.Scheduler);
        controller->Base.Scheduler = NULL;
    }
    return OS_EOK;
}

static bool
__ElementIsRoot(
    _In_ UsbManagerTransfer_t* transfer,
    _In_ uint8_t*              element)
{
    return transfer->RootElement == element;
}

bool
HCIProcessElement(
    _In_ UsbManagerController_t* controllerBase,
    _In_ uint8_t*                element,
    _In_ enum HCIProcessReason   reason,
    _In_ void*                   context)
{
    XhciTransferDescriptor_t* descriptor = (XhciTransferDescriptor_t*)element;

    switch (reason) {
        case HCIPROCESS_REASON_SCAN: {
            struct HCIProcessReasonScanContext* scanContext = context;

            if (descriptor->Flags & XHCI_TD_FLAG_COMPLETED) {
                scanContext->ElementsExecuted++;
                scanContext->ElementsProcessed++;
                scanContext->BytesTransferred += descriptor->BytesTransferred;
                if (descriptor->CompletionCode == XHCI_TRB_COMPLETION_SHORT_PACKET) {
                    scanContext->Short = true;
                }
            }
            if (descriptor->Flags & XHCI_TD_FLAG_FAILED) {
                switch (descriptor->CompletionCode) {
                    case 2:
                        scanContext->Result = USBTRANSFERCODE_BUFFERERROR;
                        break;
                    case 3:
                        scanContext->Result = USBTRANSFERCODE_BABBLE;
                        break;
                    case 4:
                        scanContext->Result = USBTRANSFERCODE_NORESPONSE;
                        break;
                    case 6:
                        scanContext->Result = USBTRANSFERCODE_STALL;
                        break;
                    default:
                        scanContext->Result = USBTRANSFERCODE_INVALID;
                        break;
                }
                return false;
            }
        } break;

        case HCIPROCESS_REASON_RESET:
            descriptor->CompletionCode = 0;
            descriptor->BytesTransferred = 0;
            descriptor->Flags &= ~(XHCI_TD_FLAG_COMPLETED | XHCI_TD_FLAG_FAILED | XHCI_TD_FLAG_CANCELLED);
            if (__ElementIsRoot((UsbManagerTransfer_t*)context, element) && descriptor->Endpoint != NULL) {
                if (XhciTransferSubmit(
                            (XhciController_t*)controllerBase,
                            descriptor->Endpoint,
                            (UsbManagerTransfer_t*)context
                    ) != OS_EOK)
                {
                    return false;
                }
            }
            break;

        case HCIPROCESS_REASON_UNLINK:
            descriptor->Flags |= XHCI_TD_FLAG_CANCELLED;
            if (__ElementIsRoot((UsbManagerTransfer_t*)context, element) && descriptor->Endpoint != NULL) {
                XhciEndpointDequeueTransfer(descriptor->Endpoint, descriptor);
            }
            break;

        case HCIPROCESS_REASON_CLEANUP:
            UsbSchedulerFreeElement(controllerBase->Scheduler, element);
            break;

        case HCIPROCESS_REASON_LINK:
        case HCIPROCESS_REASON_DUMP:
        case HCIPROCESS_REASON_NONE:
        default:
            break;
    }
    return true;
}

void
HCIProcessEvent(
    _In_ UsbManagerController_t* controller,
    _In_ enum HCIProcessEvent    event,
    _In_ void*                   context)
{
    _CRT_UNUSED(controller);
    _CRT_UNUSED(event);
    _CRT_UNUSED(context);
}
