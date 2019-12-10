/**
 * MollenOS
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
 * Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */

#define __TRACE

#include <ds/collection.h>
#include <os/mollenos.h>
#include <ddk/utils.h>
#include <threads.h>
#include <string.h>
#include <stdlib.h>

#include "../common/manager.h"
#include "ohci.h"

InterruptStatus_t
OnFastInterrupt(
    _In_ FastInterruptResources_t*  InterruptTable,
    _In_ void*                      NotUsed)
{
    OhciRegisters_t*  Registers  = (OhciRegisters_t*)INTERRUPT_IOSPACE(InterruptTable, 0)->Access.Memory.VirtualBase;
    OhciController_t* Controller = (OhciController_t*)INTERRUPT_RESOURCE(InterruptTable, 0);
    OhciHCCA_t*       Hcca       = (OhciHCCA_t*)INTERRUPT_RESOURCE(InterruptTable, 1);
    reg32_t           InterruptStatus;
    _CRT_UNUSED(NotUsed);

    // There are two cases where it might be, just to be sure
    // we don't miss an interrupt, if the HeadDone is set or the intr is set
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
        Registers->HcControl          &= ~(OHCI_CONTROL_ALL_ACTIVE);
        Registers->HcInterruptDisable  = OHCI_SOF_EVENT;
    }

    // Store interrupts, acknowledge and return
    Registers->HcInterruptStatus = InterruptStatus;
    atomic_fetch_or(&Controller->Base.InterruptStatus, InterruptStatus);
    return InterruptHandled;
}

void
OnInterrupt(
    _In_     int   Signal,
    _In_Opt_ void* InterruptData)
{
    OhciController_t* Controller = (OhciController_t*)InterruptData;
    reg32_t           InterruptStatus;

ProcessInterrupt:
    InterruptStatus = atomic_exchange(&Controller->Base.InterruptStatus, 0);

    // Process Checks
    if (InterruptStatus & OHCI_PROCESS_EVENT) {
        UsbManagerProcessTransfers(&Controller->Base);
    }

    // Root Hub Status Change
    // This occurs on disconnect/connect events
    if (InterruptStatus & OHCI_ROOTHUB_EVENT) {
        OhciPortsCheck(Controller, 0);
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
        reg32_t HcControl = READ_VOLATILE(Controller->Registers->HcControl);
        UsbManagerScheduleTransfers(&Controller->Base);
        WRITE_VOLATILE(Controller->Registers->HcControl, HcControl | Controller->QueuesActive);
    }

    // Frame Overflow
    // Happens when it rolls over from 0xFFFF to 0
    if (InterruptStatus & OHCI_OVERFLOW_EVENT) {
        // Wut do?
    }

    // Did another one fire?
    if (atomic_load(&Controller->Base.InterruptStatus) != 0) {
        goto ProcessInterrupt;
    }
}
