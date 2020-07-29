/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Universal Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
#define __TRACE

#include <os/mollenos.h>
#include <ddk/interrupt.h>
#include <ddk/utils.h>
#include <string.h>
#include <stdlib.h>
#include "../common/manager.h"
#include "uhci.h"

InterruptStatus_t
OnFastInterrupt(
        _In_ InterruptFunctionTable_t* InterruptTable,
        _In_ InterruptResourceTable_t* ResourceTable)
{
    UhciController_t* Controller = (UhciController_t*)INTERRUPT_RESOURCE(ResourceTable, 0);
    DeviceIo_t*       IoSpace    = INTERRUPT_IOSPACE(ResourceTable, 0);
    uint16_t          InterruptStatus;

    // Was the interrupt even from this controller?
    InterruptStatus = LOWORD(InterruptTable->ReadIoSpace(IoSpace, UHCI_REGISTER_STATUS, 2));
    if (!(InterruptStatus & UHCI_STATUS_INTMASK)) {
        return InterruptNotHandled;
    }
    atomic_fetch_or(&Controller->Base.InterruptStatus, InterruptStatus);

    // Clear interrupt bits
    InterruptTable->WriteIoSpace(IoSpace, UHCI_REGISTER_STATUS, InterruptStatus, 2);
    InterruptTable->EventSignal(ResourceTable->ResourceHandle);
    return InterruptHandled;
}

void
HciInterruptCallback(
    _In_ UsbManagerController_t* baseController)
{
    UhciController_t* Controller = (UhciController_t*)baseController;
    uint16_t          InterruptStatus;
    
HandleInterrupt:
    InterruptStatus = atomic_exchange(&Controller->Base.InterruptStatus, 0);
    
    // If either interrupt or error is present, it means a change happened
    // in one of our transactions
    if (InterruptStatus & (UHCI_STATUS_USBINT | UHCI_STATUS_INTR_ERROR)) {
        UhciUpdateCurrentFrame(Controller);
        UsbManagerProcessTransfers(&Controller->Base);
    }

    // The controller is telling us to perform resume
    if (InterruptStatus & UHCI_STATUS_RESUME_DETECT) {
        UhciStart(Controller, 0);
    }

    // If an host error occurs we should restart controller
    if (InterruptStatus & UHCI_STATUS_HOST_SYSERR) {
        UhciReset(Controller);
        UhciStart(Controller, 0);
    }

    // Processing error, queue stopped
    if (InterruptStatus & UHCI_STATUS_PROCESS_ERR) {
        // Clear queue and all waiting
        UhciQueueReset(Controller);
        UhciReset(Controller);
        UhciStart(Controller, 0);
    }

    // Make sure we re-handle interrupts meanwhile
    if (atomic_load(&Controller->Base.InterruptStatus) != 0) {
        goto HandleInterrupt;
    }
}
