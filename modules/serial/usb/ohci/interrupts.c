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

#include <ds/collection.h>
#include <os/mollenos.h>
#include <os/timers.h>
#include <os/utils.h>
#include <threads.h>
#include <string.h>
#include <stdlib.h>

#include "../common/manager.h"
#include "ohci.h"

/* OnFastInterrupt
 * Is called for the sole purpose to determine if this source
 * has invoked an irq. If it has, silence and return (Handled) */
InterruptStatus_t
OnFastInterrupt(
    _In_ FastInterruptResources_t*  InterruptTable,
    _In_ void*                      NotUsed)
{
    // Variables
    OhciRegisters_t* Registers      = (OhciRegisters_t*)INTERRUPT_IOSPACE(InterruptTable, 0)->VirtualBase;
    OhciController_t* Controller    = INTERRUPT_RESOURCE(InterruptTable, 0);
    OhciHCCA_t* Hcca                = INTERRUPT_RESOURCE(InterruptTable, 1);
    reg32_t InterruptStatus;
    _CRT_UNUSED(NotUsed);

    // There are two cases where it might be, just to be sure
    // we don't miss an interrupt, if the HeadDone is set or the
    // intr is set
    if (Hcca->HeadDone != 0) {
        InterruptStatus = OHCI_PROCESS_EVENT;
        // If halted bit is set, get rest of interrupt
        if (Hcca->HeadDone & 0x1) {
            InterruptStatus |= (Registers->HcInterruptStatus & Registers->HcInterruptEnable);
        }
    }
    else {
        // Was it this Controller that made the interrupt?
        // We only want the interrupts we have set as enabled
        InterruptStatus = (Registers->HcInterruptStatus & Registers->HcInterruptEnable);
    }

    // Was the interrupt even from this controller?
    if (!InterruptStatus) {
        return InterruptNotHandled;
    }

    // Process Checks first
    // This happens if a transaction has completed
    if (InterruptStatus & OHCI_PROCESS_EVENT) {
        //reg32_t TdAddress = (Hcca->HeadDone & ~(0x00000001));
        Hcca->HeadDone = 0;
    }

    // Stage 1 of the linking/unlinking event, disable queues untill
    // after the actual unlink/link event
    if (InterruptStatus & OHCI_SOF_EVENT) {
        Registers->HcControl           &= ~(OHCI_CONTROL_ALL_ACTIVE);
        Registers->HcInterruptDisable   = OHCI_SOF_EVENT;
    }

    // Store interrupts, acknowledge and return
    Controller->Base.InterruptStatus   |= InterruptStatus;
    Registers->HcInterruptStatus        = InterruptStatus;
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
    OhciController_t *Controller        = NULL;
    reg32_t InterruptStatus             = 0;
    
    // Unusued
    _CRT_UNUSED(Arg0);
    _CRT_UNUSED(Arg1);
    _CRT_UNUSED(Arg2);

    // Instantiate the pointer
    Controller                          = (OhciController_t*)InterruptData;
    
ProcessInterrupt:
    InterruptStatus                     = Controller->Base.InterruptStatus;
    Controller->Base.InterruptStatus    = 0;

    // Process Checks
    if (InterruptStatus & OHCI_PROCESS_EVENT) {
        UsbManagerProcessTransfers(&Controller->Base);
    }

    // Root Hub Status Change
    // This occurs on disconnect/connect events
    if (InterruptStatus & OHCI_ROOTHUB_EVENT) {
        OhciPortsCheck(Controller);
    }

    // Fatal errors, reset everything
    if (InterruptStatus & (OHCI_FATAL_EVENT | OHCI_OVERRUN_EVENT)) {
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
        UsbManagerScheduleTransfers(&Controller->Base);
        Controller->Registers->HcControl |= Controller->QueuesActive;
        UsbManagerProcessTransfers(&Controller->Base);
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
