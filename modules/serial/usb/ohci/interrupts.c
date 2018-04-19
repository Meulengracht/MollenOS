/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
//#define __TRACE

/* Includes 
 * - System */
#include <os/mollenos.h>
#include <os/timers.h>
#include <os/utils.h>

#include "../common/manager.h"
#include "ohci.h"

/* Includes
 * - Library */
#include <ds/collection.h>
#include <threads.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* OnFastInterrupt
 * Is called for the sole purpose to determine if this source
 * has invoked an irq. If it has, silence and return (Handled) */
InterruptStatus_t
OnFastInterrupt(
    _In_Opt_ void *InterruptData)
{
    // Variables
    OhciController_t *Controller = NULL;
    reg32_t InterruptStatus;

    // Instantiate the pointer
    Controller = (OhciController_t*)InterruptData;

    // There are two cases where it might be, just to be sure
    // we don't miss an interrupt, if the HeadDone is set or the
    // intr is set
    if (Controller->Hcca->HeadDone != 0) {
        InterruptStatus = OHCI_PROCESS_EVENT;
        // If halted bit is set, get rest of interrupt
        if (Controller->Hcca->HeadDone & 0x1) {
            InterruptStatus |= (Controller->Registers->HcInterruptStatus
                & Controller->Registers->HcInterruptEnable);
        }
    }
    else {
        // Was it this Controller that made the interrupt?
        // We only want the interrupts we have set as enabled
        InterruptStatus = (Controller->Registers->HcInterruptStatus
            & Controller->Registers->HcInterruptEnable);
    }

    // Trace
    TRACE("Interrupt - Status 0x%x", InterruptStatus);

    // Was the interrupt even from this controller?
    if (!InterruptStatus) {
        return InterruptNotHandled;
    }

    // Stage 1 of the linking/unlinking event
    if (InterruptStatus & OHCI_SOF_EVENT) {
        UsbManagerScheduleTransfers(&Controller->Base);
        Controller->Registers->HcInterruptDisable = OHCI_SOF_EVENT;
    }

    // Store interrupts, acknowledge and return
    Controller->Base.InterruptStatus |= InterruptStatus;
    Controller->Registers->HcInterruptStatus = InterruptStatus;
    return InterruptHandled;
}

/* OnInterrupt
 * Is called by external services to indicate an external interrupt.
 * This is to actually process the device interrupt */
InterruptStatus_t 
OnInterrupt(
    _In_Opt_ void *InterruptData,
    _In_Opt_ size_t Arg0,
    _In_Opt_ size_t Arg1,
    _In_Opt_ size_t Arg2)
{
    // Variables
    OhciController_t *Controller    = NULL;
    reg32_t InterruptStatus         = 0;
    
    // Unusued
    _CRT_UNUSED(Arg0);
    _CRT_UNUSED(Arg1);
    _CRT_UNUSED(Arg2);

    // Instantiate the pointer
    Controller = (OhciController_t*)InterruptData;
    
ProcessInterrupt:
    InterruptStatus = Controller->Base.InterruptStatus;
    Controller->Base.InterruptStatus = 0;

    // Process Checks first
    // This happens if a transaction has completed
    if (InterruptStatus & OHCI_PROCESS_EVENT) {
        //reg32_t TdAddress = (Controller->Hcca->HeadDone & ~(0x00000001));
        UsbManagerProcessTransfers(&Controller->Base);
        Controller->Hcca->HeadDone = 0;
    }

    // Root Hub Status Change
    // This occurs on disconnect/connect events
    if (InterruptStatus & OHCI_ROOTHUB_EVENT) {
        OhciPortsCheck(Controller);
    }

    // Fatal errors, reset everything
    if (InterruptStatus & OHCI_FATAL_EVENT) {
        OhciReset(Controller);
        OhciSetMode(Controller, OHCI_CONTROL_ACTIVE);
    }
    if (InterruptStatus & OHCI_OVERRUN_EVENT) {
        OhciQueueReset(Controller);
        OhciReset(Controller);
        OhciSetMode(Controller, OHCI_CONTROL_ACTIVE);
    }

    // Resume Detection? 
    // We must wait 20 ms before putting Controller to Operational
    if (InterruptStatus & OHCI_RESUMEDETECT_EVENT) {
        thrd_sleepex(20);
        OhciSetMode(Controller, OHCI_CONTROL_ACTIVE);
    }

    // Stage 2 of an linking/unlinking event
    if (InterruptStatus & OHCI_SOF_EVENT) {
        UsbManagerProcessTransfers(&Controller->Base);
        Controller->Registers->HcInterruptDisable = OHCI_SOF_EVENT;
    }

    // Frame Overflow
    // Happens when it rolls over from 0xFFFF to 0
    if (InterruptStatus & OHCI_OVERFLOW_EVENT) {
        // Wut do?
    }

    // Did another one fire?
    if (Controller->Base.InterruptStatus != 0) {
        goto ProcessInterrupt;
    }
    return InterruptHandled;
}
