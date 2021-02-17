/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - Universal Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
//#define __TRACE
//#define __DIAGNOSE

#include <ddk/utils.h>
#include "uhci.h"

UsbTransferStatus_t
UhciTransactionDispatch(
    _In_ UhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer)
{
    // Update status
    Transfer->Status = TransferQueued;
    UhciUpdateCurrentFrame(Controller);
    
#ifdef __TRACE
    UsbManagerDumpChain(&Controller->Base, Transfer, (uint8_t*)Transfer->EndpointDescriptor, USB_CHAIN_DEPTH);
#ifdef __DIAGNOSE
    for(;;);
#endif
#endif

    UsbManagerIterateChain(&Controller->Base, Transfer->EndpointDescriptor, 
        USB_CHAIN_DEPTH, USB_REASON_LINK, HciProcessElement, Transfer);
    return TransferQueued;
}

OsStatus_t
HciTransactionFinalize(
    _In_ UsbManagerController_t* Controller,
    _In_ UsbManagerTransfer_t*   Transfer,
    _In_ int                     Reset)
{
    // Debug
    TRACE("UhciTransactionFinalize(Id %u)", Transfer->Id);

    UsbManagerIterateChain(Controller, Transfer->EndpointDescriptor, 
        USB_CHAIN_DEPTH, USB_REASON_UNLINK, HciProcessElement, Transfer);
    UsbManagerIterateChain(Controller, Transfer->EndpointDescriptor, 
        USB_CHAIN_DEPTH, USB_REASON_CLEANUP, HciProcessElement, Transfer);
    return OsSuccess;
}

OsStatus_t
HciDequeueTransfer(
    _In_ UsbManagerTransfer_t* Transfer)
{
    // Mark for unscheduling on next interrupt/check
    Transfer->Flags |= TransferFlagUnschedule;
    return OsSuccess;
}
