/**
 * Copyright 2026, Philip Meulengracht
 */

#include "xhci.h"

void
XhciEventRingDrain(
    _In_ XhciController_t* controller)
{
    _CRT_UNUSED(controller);
}

void
HciInterruptCallback(
    _In_ UsbManagerController_t* baseController)
{
    XhciEventRingDrain((XhciController_t*)baseController);
    UsbManagerProcessTransfers(baseController);
}

void
HciTimerCallback(
    _In_ UsbManagerController_t* baseController)
{
    _CRT_UNUSED(baseController);
}
