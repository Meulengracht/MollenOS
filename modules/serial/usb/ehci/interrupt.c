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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 */
//#define __TRACE

/* Includes 
 * - System */
#include <os/mollenos.h>
#include <os/utils.h>

#include "../common/manager.h"
#include "ehci.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* OnFastInterrupt
 * Is called for the sole purpose to determine if this source
 * has invoked an irq. If it has, silence and return (Handled) */
InterruptStatus_t
OnFastInterrupt(
    _In_ FastInterruptResources_t*  InterruptTable,
    _In_ void*                      NotUsed)
{
    // Variables
    EchiOperationalRegisters_t* Registers;
    EhciController_t* Controller = INTERRUPT_RESOURCE(InterruptTable, 0);
    uintptr_t RegisterAddress = INTERRUPT_IOSPACE(InterruptTable, 0)->VirtualBase;
    reg32_t InterruptStatus;
    _CRT_UNUSED(NotUsed);

    RegisterAddress    += ((EchiCapabilityRegisters_t*)RegisterAddress)->Length;
    Registers           = (EchiOperationalRegisters_t*)RegisterAddress;
    InterruptStatus     = (Registers->UsbStatus & Registers->UsbIntr);

    // Was the interrupt even from this controller?
    if (!InterruptStatus) {
        return InterruptNotHandled;
    }

    // Acknowledge the interrupt by clearing
    Registers->UsbStatus                = InterruptStatus;
    Controller->Base.InterruptStatus   |= InterruptStatus;
    return InterruptHandled;
}

/* OnInterrupt
 * Is called by external services to indicate an external interrupt.
 * This is to actually process the device interrupt */
InterruptStatus_t 
OnInterrupt(
    _In_Opt_ void*  InterruptData,
    _In_Opt_ size_t Arg0,
    _In_Opt_ size_t Arg1,
    _In_Opt_ size_t Arg2)
{
    // Variables
    EhciController_t *Controller        = NULL;
    reg32_t InterruptStatus             = 0;
    reg32_t ChangeBits                  = (reg32_t)~0;
    
    // Unused
    _CRT_UNUSED(Arg0);
    _CRT_UNUSED(Arg1);
    _CRT_UNUSED(Arg2);

    // Instantiate the pointer
    Controller                          = (EhciController_t*)InterruptData;

ProcessInterrupt:
    InterruptStatus                     = Controller->Base.InterruptStatus;
    Controller->Base.InterruptStatus    = 0;

    // Transaction update, either error or completion
    if (InterruptStatus & (EHCI_STATUS_PROCESS | EHCI_STATUS_PROCESSERROR | EHCI_STATUS_ASYNC_DOORBELL)) {
        UsbManagerProcessTransfers(&Controller->Base);
    }

    // Hub change? We should enumerate ports and detect
    // which events occured
    if (InterruptStatus & EHCI_STATUS_PORTCHANGE) {
        // Give it ~0 if it doesn't support per-port change
        if (Controller->CParameters & EHCI_CPARAM_PERPORT_CHANGE) {
            ChangeBits = (InterruptStatus >> 16);
        }
        EhciPortScan(Controller, ChangeBits);
    }

    // HC Fatal Error
    // Clear all queued, reset controller
    if (InterruptStatus & EHCI_STATUS_HOSTERROR) {
        if (EhciQueueReset(Controller) != OsSuccess) {
            ERROR("EHCI-Failure: Failed to reset queue after fatal error");
        }
        if (EhciRestart(Controller) != OsSuccess) {
            ERROR("EHCI-Failure: Failed to reset controller after fatal error");
        }
    }

    // In case an interrupt fired during processing
    if (Controller->Base.InterruptStatus != 0) {
        goto ProcessInterrupt;
    }
    return InterruptHandled;
}
