/* MollenOS
 *
 * Copyright 2018 Philip Meulengracht
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
 * MollenOS MCore - Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 */
//#define __TRACE

#include <ddk/interrupt.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include "../common/manager.h"
#include "ehci.h"

irqstatus_t
OnFastInterrupt(
        _In_ InterruptFunctionTable_t* InterruptTable,
        _In_ InterruptResourceTable_t* ResourceTable)
{
    EchiOperationalRegisters_t* Registers;
    EhciController_t* Controller = (EhciController_t*)INTERRUPT_RESOURCE(ResourceTable, 0);
    uintptr_t RegisterAddress = INTERRUPT_IOSPACE(ResourceTable, 0)->Access.Memory.VirtualBase;
    reg32_t InterruptStatus;

    RegisterAddress    += ((EchiCapabilityRegisters_t*)RegisterAddress)->Length;
    Registers           = (EchiOperationalRegisters_t*)RegisterAddress;
    InterruptStatus     = (Registers->UsbStatus & Registers->UsbIntr);

    // Was the interrupt even from this controller?
    if (!InterruptStatus) {
        return IRQSTATUS_NOT_HANDLED;
    }

    // Acknowledge the interrupt by clearing
    Registers->UsbStatus = InterruptStatus;
    atomic_fetch_or(&Controller->Base.InterruptStatus, InterruptStatus);

    InterruptTable->EventSignal(ResourceTable->HandleResource);
    return IRQSTATUS_HANDLED;
}

void
HciInterruptCallback(
    _In_ UsbManagerController_t* baseController)
{
    EhciController_t* controller = (EhciController_t*)baseController;
    reg32_t           changeBits = (reg32_t)~0UL;
    reg32_t           interruptStatus;

ProcessInterrupt:
    interruptStatus = atomic_exchange(&controller->Base.InterruptStatus, 0);

    // Transaction update, either error or completion
    if (interruptStatus & (EHCI_STATUS_PROCESS | EHCI_STATUS_PROCESSERROR | EHCI_STATUS_ASYNC_DOORBELL)) {
        UsbManagerProcessTransfers(&controller->Base);
    }

    // Hub change? We should enumerate ports and detect
    // which events occured
    if (interruptStatus & EHCI_STATUS_PORTCHANGE) {
        // Give it ~0 if it doesn't support per-port change
        if (controller->CParameters & EHCI_CPARAM_PERPORT_CHANGE) {
            changeBits = (interruptStatus >> 16);
        }
        EhciPortScan(controller, changeBits);
    }

    // HC Fatal Error
    // Clear all queued, reset controller
    if (interruptStatus & EHCI_STATUS_HOSTERROR) {
        if (EhciQueueReset(controller) != OsOK) {
            ERROR("EHCI-Failure: Failed to reset queue after fatal error");
        }
        if (EhciRestart(controller) != OsOK) {
            ERROR("EHCI-Failure: Failed to reset controller after fatal error");
        }
    }

    // In case an interrupt fired during processing
    if (atomic_load(&controller->Base.InterruptStatus) != 0) {
        goto ProcessInterrupt;
    }
}
