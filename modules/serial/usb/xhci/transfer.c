/**
 * Copyright 2026, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 */

//#define __TRACE

#include <ddk/utils.h>
#include <string.h>
#include "xhci.h"

oserr_t
HCITransferElementsNeeded(
        _In_  UsbManagerTransfer_t*     transfer,
        _In_  uint32_t                  transferLength,
        _In_  enum USBTransferDirection direction,
        _In_  SHMSGTable_t*             sgTable,
        _In_  uint32_t                  sgTableOffset,
        _Out_ int*                      elementCountOut)
{
    _CRT_UNUSED(transfer);
    _CRT_UNUSED(transferLength);
    _CRT_UNUSED(direction);
    _CRT_UNUSED(sgTable);
    _CRT_UNUSED(sgTableOffset);

    // Endpoint contexts and transfer rings are xHCI-specific and cannot be
    // represented by the UHCI/OHCI/EHCI queue-element model. Keep transfer
    // requests from being partially built until the xHCI endpoint scheduler is
    // added on top of this controller foundation.
    *elementCountOut = 0;
    return OS_ENOTSUPPORTED;
}

void
HCITransferElementFill(
        _In_ UsbManagerTransfer_t*     transfer,
        _In_ uint32_t                  transferLength,
        _In_ enum USBTransferDirection direction,
        _In_ SHMSGTable_t*             sgTable,
        _In_ uint32_t                  sgTableOffset)
{
    _CRT_UNUSED(transfer);
    _CRT_UNUSED(transferLength);
    _CRT_UNUSED(direction);
    _CRT_UNUSED(sgTable);
    _CRT_UNUSED(sgTableOffset);
}

bool
HCIProcessElement(
        _In_ UsbManagerController_t* controller,
        _In_ uint8_t*                element,
        _In_ enum HCIProcessReason   reason,
        _In_ void*                   context)
{
    _CRT_UNUSED(controller);
    _CRT_UNUSED(element);
    _CRT_UNUSED(reason);
    _CRT_UNUSED(context);
    return false;
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

oserr_t
HCITransferFinalize(
        _In_ UsbManagerController_t* controller,
        _In_ UsbManagerTransfer_t*   transfer,
        _In_ bool                    deferredClean)
{
    _CRT_UNUSED(controller);
    _CRT_UNUSED(transfer);
    _CRT_UNUSED(deferredClean);
    return OS_EOK;
}

oserr_t
HCITransferQueue(
        _In_ UsbManagerTransfer_t* transfer)
{
    _CRT_UNUSED(transfer);
    return OS_ENOTSUPPORTED;
}

oserr_t
HCITransferQueueIsochronous(
        _In_ UsbManagerTransfer_t* transfer)
{
    _CRT_UNUSED(transfer);
    return OS_ENOTSUPPORTED;
}

oserr_t
HCITransferDequeue(
        _In_ UsbManagerTransfer_t* transfer)
{
    _CRT_UNUSED(transfer);
    return OS_ENOTSUPPORTED;
}
