/**
 * MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 *
 *
 * Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */

//#define __TRACE
#define __need_minmax
#include <ddk/utils.h>
#include "ohci.h"
#include <assert.h>
#include <stdlib.h>

struct __PrepareContext {
    UsbManagerTransfer_t* Transfer;
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
    OhciTransferDescriptor_t* td      = (OhciTransferDescriptor_t*)element;
    _CRT_UNUSED(controllerBase);
    _CRT_UNUSED(reason);

    switch (context->Transfer->Elements[context->TDIndex].Type) {
        case USB_TRANSACTION_SETUP: {
            OHCITDSetup(td, context->Transfer->Elements[context->TDIndex].DataAddress);
        } break;
        case USB_TRANSACTION_IN: {
            OHCITDData(
                    td,
                    context->Transfer->Type,
                    OHCI_TD_IN,
                    context->Transfer->Elements[context->TDIndex].DataAddress,
                    context->Transfer->Elements[context->TDIndex].Length
            );
        } break;
        case USB_TRANSACTION_OUT: {
            OHCITDData(
                    td,
                    context->Transfer->Type,
                    OHCI_TD_OUT,
                    context->Transfer->Elements[context->TDIndex].DataAddress,
                    context->Transfer->Elements[context->TDIndex].Length
            );
        } break;
    }

    if (context->TDIndex == context->LastTDIndex) {
        td->Flags         &= ~(OHCI_TD_IOC_NONE);
        td->OriginalFlags = td->Flags;
    }
    context->TDIndex++;
    return true;
}


static void
__PrepareTransferDescriptors(
        _In_ OhciController_t*     controller,
        _In_ UsbManagerTransfer_t* transfer,
        _In_ int                   count)
{
    struct __PrepareContext context = {
            .Transfer = transfer,
            .TDIndex = transfer->ElementsCompleted,
            .LastTDIndex = (transfer->ElementsCompleted + count - 1)
    };
    UsbManagerChainEnumerate(
            &controller->Base,
            transfer->EndpointDescriptor,
            USB_CHAIN_DEPTH,
            HCIPROCESS_REASON_NONE,
            __PrepareDescriptor,
            &context
    );
}

oserr_t
HCITransferQueue(
    _In_ UsbManagerTransfer_t* transfer)
{
    OhciController_t* controller;
    oserr_t           oserr;
    int               tdsReady;

    controller = (OhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (controller == NULL) {
        return OS_ENOENT;
    }

    oserr = OHCITransferEnsureQueueHead(controller, transfer);
    if (oserr != OS_EOK) {
        return oserr;
    }

    tdsReady = OHCITransferAllocateDescriptors(controller, transfer, OHCI_TD_POOL);
    if (!tdsReady) {
        transfer->State = USBTRANSFER_STATE_WAITING;
        return OS_EOK;
    }

    __PrepareTransferDescriptors(controller, transfer, tdsReady);
    OhciTransactionDispatch(controller, transfer);
    return OS_EOK;
}
