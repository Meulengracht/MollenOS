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
 * Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 */
//#define __TRACE
//#define __DIAGNOSE

#include <ddk/utils.h>
#include "ehci.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

void
EhciTransactionDispatch(
    _In_ EhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer)
{
    // Update status
    Transfer->Status = TransferInProgress;
#ifdef __TRACE
    UsbManagerDumpChain(&Controller->Base, Transfer, (uint8_t*)Transfer->EndpointDescriptor, USB_CHAIN_DEPTH);
#ifdef __DIAGNOSE
    for(;;);
#endif
#endif
    UsbManagerChainEnumerate(&Controller->Base, Transfer->EndpointDescriptor,
        USB_CHAIN_DEPTH, HCIPROCESS_REASON_LINK, HCIProcessElement, Transfer);
}

oserr_t
HciTransactionFinalize(
    _In_ UsbManagerController_t*    Controller,
    _In_ UsbManagerTransfer_t*      Transfer,
    _In_ int                        Reset)
{
    // Debug
    TRACE("EhciTransactionFinalize(Id %u)", Transfer->ID);

    // Always unlink
    UsbManagerChainEnumerate(Controller, Transfer->EndpointDescriptor,
        USB_CHAIN_DEPTH, HCIPROCESS_REASON_UNLINK, HCIProcessElement, Transfer);

    // Send notification for transfer if control/bulk immediately, but defer
    // cleanup till the doorbell has been rung
    UsbManagerSendNotification(Transfer);
    if (Reset != 0) {
        UsbManagerChainEnumerate(Controller, Transfer->EndpointDescriptor,
            USB_CHAIN_DEPTH, HCIPROCESS_REASON_CLEANUP, HCIProcessElement, Transfer);
    }
    else {
        Transfer->Flags |= TransferFlagCleanup;
        EhciRingDoorbell((EhciController_t*)Controller);
    }
    return OS_EOK;
}

oserr_t
HciDequeueTransfer(
    _In_ UsbManagerTransfer_t* Transfer)
{
    EhciController_t* Controller;

    Controller = (EhciController_t*) UsbManagerGetController(Transfer->DeviceID);
    if (!Controller) {
        return OS_EINVALPARAMS;
    }
    
    // Unschedule immediately, but keep data intact as hardware still (might) reference it.
    UsbManagerChainEnumerate(&Controller->Base, Transfer->EndpointDescriptor,
        USB_CHAIN_DEPTH, HCIPROCESS_REASON_UNLINK, HCIProcessElement, Transfer);

    // Mark transfer for cleanup and ring doorbell if async
    if (Transfer->Base.Type == USBTRANSFER_TYPE_CONTROL || Transfer->Base.Type == USB_TRANSFER_BULK) {
        Transfer->Flags |= TransferFlagCleanup;
        EhciRingDoorbell(Controller);
    }
    else {
        UsbManagerChainEnumerate(&Controller->Base, Transfer->EndpointDescriptor,
            USB_CHAIN_DEPTH, HCIPROCESS_REASON_CLEANUP, HCIProcessElement, Transfer);
    }
    
    return OS_EOK;
}
