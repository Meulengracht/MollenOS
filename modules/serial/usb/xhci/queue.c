/**
 * Copyright 2026, Philip Meulengracht
 */

#include <stddef.h>
#include <string.h>
#include "xhci.h"

oserr_t
XhciQueueInitialize(
    _In_ XhciController_t* controller)
{
    UsbSchedulerSettings_t settings;
    oserr_t                oserr;

    UsbSchedulerSettingsCreate(&settings, 1, 1, 0, 0);
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

    oserr = XhciRingInitialize(&controller->CommandRing, XHCI_RING_TRB_COUNT);
    if (oserr != OS_EOK) {
        UsbSchedulerDestroy(controller->Base.Scheduler);
        controller->Base.Scheduler = NULL;
        return oserr;
    }

    oserr = XhciRingInitialize(&controller->EventRing, XHCI_RING_TRB_COUNT);
    if (oserr != OS_EOK) {
        XhciRingDestroy(&controller->CommandRing);
        UsbSchedulerDestroy(controller->Base.Scheduler);
        controller->Base.Scheduler = NULL;
        return oserr;
    }
    return OS_EOK;
}

oserr_t
XhciQueueReset(
    _In_ XhciController_t* controller)
{
    UsbManagerClearTransfers(&controller->Base);
    XhciEndpointDestroyAll(controller);
    XhciRingReset(&controller->CommandRing);
    XhciRingReset(&controller->EventRing);
    if (controller->Base.Scheduler != NULL) {
        return UsbSchedulerResetInternalData(controller->Base.Scheduler, 1, 0);
    }
    return OS_EOK;
}

void
XhciQueueDestroy(
    _In_ XhciController_t* controller)
{
    if (controller == NULL) {
        return;
    }

    XhciQueueReset(controller);
    XhciRingDestroy(&controller->CommandRing);
    XhciRingDestroy(&controller->EventRing);
    UsbSchedulerDestroy(controller->Base.Scheduler);
    controller->Base.Scheduler = NULL;
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
            }
            if (descriptor->Flags & XHCI_TD_FLAG_FAILED) {
                scanContext->Result = USBTRANSFERCODE_INVALID;
                return false;
            }
        } break;

        case HCIPROCESS_REASON_RESET:
            descriptor->CompletionCode = 0;
            descriptor->BytesTransferred = 0;
            descriptor->Flags &= ~(XHCI_TD_FLAG_COMPLETED | XHCI_TD_FLAG_FAILED | XHCI_TD_FLAG_CANCELLED);
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
