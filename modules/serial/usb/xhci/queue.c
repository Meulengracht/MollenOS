/**
 * Copyright 2026, Philip Meulengracht
 */

#define __need_minmax
#include <ddk/utils.h>
#include <os/handle.h>
#include <os/shm.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "xhci.h"

static void
__XhciFreeDMA(
        _In_ OSHandle_t*   handle,
        _In_ SHMSGTable_t* table)
{
    OSHandleDestroy(handle);
    free(table->Entries);
    memset(table, 0, sizeof(SHMSGTable_t));
}

static oserr_t
__XhciAllocateDMA(
        _In_  size_t        size,
        _Out_ OSHandle_t*   handle,
        _Out_ SHMSGTable_t* table,
        _Out_ void**        bufferOut)
{
    oserr_t oserr;

    oserr = SHMCreate(
            &(SHM_t) {
                    .Flags = SHM_DEVICE | SHM_PRIVATE | SHM_CLEAN,
                    .Conformity = OSMEMORYCONFORMITY_BITS32,
                    .Size = size,
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE
            },
            handle
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = SHMGetSGTable(handle, table, -1);
    if (oserr != OS_EOK) {
        OSHandleDestroy(handle);
        memset(handle, 0, sizeof(OSHandle_t));
        memset(table, 0, sizeof(SHMSGTable_t));
        return oserr;
    }

    if (bufferOut != NULL) {
        *bufferOut = SHMBuffer(handle);
    }
    return OS_EOK;
}

static void
__XhciFreeControllerResources(
        _In_ XhciController_t* controller)
{
    __XhciFreeDMA(&controller->ErstDMA, &controller->ErstDMATable);
    __XhciFreeDMA(&controller->EventRingDMA, &controller->EventRingDMATable);
    XhciRingDestroy(&controller->CommandRing);
    __XhciFreeDMA(&controller->ScratchpadBufferDMA, &controller->ScratchpadBufferDMATable);
    __XhciFreeDMA(&controller->ScratchpadArrayDMA, &controller->ScratchpadArrayDMATable);
    __XhciFreeDMA(&controller->DCBaaDMA, &controller->DCBaaDMATable);
}

static oserr_t
__XhciFillScratchpadArray(
        _In_ XhciController_t* controller)
{
    size_t scratchpadIndex = 0;

    for (int i = 0; i < controller->ScratchpadBufferDMATable.Count && scratchpadIndex < controller->ScratchpadCount; i++) {
        uintptr_t address = controller->ScratchpadBufferDMATable.Entries[i].Address;
        size_t    length  = controller->ScratchpadBufferDMATable.Entries[i].Length;

        while (length >= XHCI_PAGE_SIZE && scratchpadIndex < controller->ScratchpadCount) {
            controller->ScratchpadArray[scratchpadIndex++] = address;
            address += XHCI_PAGE_SIZE;
            length  -= XHCI_PAGE_SIZE;
        }
    }

    return (scratchpadIndex == controller->ScratchpadCount) ? OS_EOK : OS_EUNKNOWN;
}

oserr_t
XhciQueueInitialize(
    _In_ XhciController_t* controller)
{
    size_t  dcbaaEntries = MIN(controller->SlotCount + 1, XHCI_MAX_DEVICE_CONTEXTS);
    size_t  scratchpadBytes;
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

    oserr = __XhciAllocateDMA(
            dcbaaEntries * sizeof(reg64_t),
            &controller->DCBaaDMA,
            &controller->DCBaaDMATable,
            (void**)&controller->DCBaa
    );
    if (oserr != OS_EOK) {
        UsbSchedulerDestroy(controller->Base.Scheduler);
        controller->Base.Scheduler = NULL;
        return oserr;
    }

    if (controller->ScratchpadCount > 0) {
        oserr = __XhciAllocateDMA(
                controller->ScratchpadCount * sizeof(reg64_t),
                &controller->ScratchpadArrayDMA,
                &controller->ScratchpadArrayDMATable,
                (void**)&controller->ScratchpadArray
        );
        if (oserr != OS_EOK) {
            __XhciFreeControllerResources(controller);
            UsbSchedulerDestroy(controller->Base.Scheduler);
            controller->Base.Scheduler = NULL;
            return oserr;
        }
        if (controller->ScratchpadArrayDMATable.Count != 1) {
            ERROR("XHCI-Failure: scratchpad pointer array was not physically contiguous.");
            __XhciFreeControllerResources(controller);
            UsbSchedulerDestroy(controller->Base.Scheduler);
            controller->Base.Scheduler = NULL;
            return OS_EUNKNOWN;
        }

        scratchpadBytes = controller->ScratchpadCount * XHCI_PAGE_SIZE;
        oserr = __XhciAllocateDMA(
                scratchpadBytes,
                &controller->ScratchpadBufferDMA,
                &controller->ScratchpadBufferDMATable,
                NULL
        );
        if (oserr != OS_EOK) {
            __XhciFreeControllerResources(controller);
            UsbSchedulerDestroy(controller->Base.Scheduler);
            controller->Base.Scheduler = NULL;
            return oserr;
        }

        oserr = __XhciFillScratchpadArray(controller);
        if (oserr != OS_EOK) {
            ERROR("XHCI-Failure: scratchpad buffers did not provide enough DMA pages.");
            __XhciFreeControllerResources(controller);
            UsbSchedulerDestroy(controller->Base.Scheduler);
            controller->Base.Scheduler = NULL;
            return oserr;
        }
        controller->DCBaa[0] = controller->ScratchpadArrayDMATable.Entries[0].Address;
    }

        oserr = XhciRingInitialize(&controller->CommandRing, XHCI_COMMAND_RING_ENTRIES);
    if (oserr != OS_EOK) {
        __XhciFreeControllerResources(controller);
        UsbSchedulerDestroy(controller->Base.Scheduler);
        controller->Base.Scheduler = NULL;
        return oserr;
    }

    oserr = __XhciAllocateDMA(
            XHCI_EVENT_RING_ENTRIES * sizeof(XhciTransferRequestBlock_t),
            &controller->EventRingDMA,
            &controller->EventRingDMATable,
            (void**)&controller->EventRing
    );
    if (oserr != OS_EOK) {
        __XhciFreeControllerResources(controller);
        UsbSchedulerDestroy(controller->Base.Scheduler);
        controller->Base.Scheduler = NULL;
        return oserr;
    }

    oserr = __XhciAllocateDMA(
            XHCI_ERST_ENTRIES * sizeof(XhciEventRingSegmentTableEntry_t),
            &controller->ErstDMA,
            &controller->ErstDMATable,
            (void**)&controller->Erst
    );
    if (oserr != OS_EOK) {
        __XhciFreeControllerResources(controller);
        UsbSchedulerDestroy(controller->Base.Scheduler);
        controller->Base.Scheduler = NULL;
        return oserr;
    }

    controller->Erst[0].RingSegmentBaseAddress = controller->EventRingDMATable.Entries[0].Address;
    controller->Erst[0].RingSegmentSize = XHCI_EVENT_RING_ENTRIES;
    controller->EventRingIndex = 0;
    controller->EventRingCycle = 1;
    return OS_EOK;
}

oserr_t
XhciQueueReset(
    _In_ XhciController_t* controller)
{
    UsbManagerClearTransfers(&controller->Base);
    XhciEndpointDestroyAll(controller);
    XhciDeviceDestroyAll(controller);
    XhciRingReset(&controller->CommandRing);
    memset(controller->Commands, 0, sizeof(controller->Commands));
    memset(controller->EventRing, 0, XHCI_EVENT_RING_ENTRIES * sizeof(XhciTransferRequestBlock_t));
    controller->EventRingIndex = 0;
    controller->EventRingCycle = 1;
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
    __XhciFreeControllerResources(controller);
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
