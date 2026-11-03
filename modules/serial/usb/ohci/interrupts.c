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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Open Host Controller Interface Driver
 */

//#define __TRACE

#include <ddk/interrupts/interrupt.h>
#include <ddk/utils.h>
#include <threads.h>
#include <stdlib.h>

#include "../common/manager.h"
#include "ohci.h"

irqstatus_t
OnFastInterrupt(
        _In_ InterruptFunctionTable_t* InterruptTable,
        _In_ InterruptResourceTable_t* ResourceTable)
{
    OhciRegisters_t*  Registers  = (OhciRegisters_t*)INTERRUPT_IOSPACE(ResourceTable, 0)->Access.Memory.VirtualBase;
    OhciController_t* Controller = (OhciController_t*)INTERRUPT_RESOURCE(ResourceTable, 0);
    OhciHCCA_t*       Hcca       = (OhciHCCA_t*)INTERRUPT_RESOURCE(ResourceTable, 1);
    reg32_t           InterruptStatus;

    // Check both HeadDone and the interrupt status register so completed
    // transfers are processed even when only HeadDone is set.
    if (Hcca->HeadDone != 0) {
        InterruptStatus = OHCI_PROCESS_EVENT;
        // If halted bit is set, get rest of interrupt
        if (Hcca->HeadDone & 0x1) {
            InterruptStatus |= (Registers->HcInterruptStatus & Registers->HcInterruptEnable);
        }
    }
    else {
        // Only process enabled interrupts from this controller.
        InterruptStatus = (Registers->HcInterruptStatus & Registers->HcInterruptEnable);
    }

    // No pending interrupt from this controller.
    if (!InterruptStatus) {
        return IRQSTATUS_NOT_HANDLED;
    }
    
    // Clear HeadDone when a transaction has completed.
    if (InterruptStatus & OHCI_PROCESS_EVENT) {
        //reg32_t TdAddress = (Hcca->HeadDone & ~(0x00000001));
        Hcca->HeadDone = 0;
    }

    // Stage 1: Disable queues until the link/unlink changes are complete.
    if (InterruptStatus & OHCI_SOF_EVENT) {
        Registers->HcControl          &= ~(OHCI_CONTROL_ALL_ACTIVE);
        Registers->HcInterruptDisable  = OHCI_SOF_EVENT;
    }

    // Acknowledge interrupts and save them for the callback.
    Registers->HcInterruptStatus = InterruptStatus;
    atomic_fetch_or(&Controller->Base.InterruptStatus, InterruptStatus);
    
    InterruptTable->EventSignal(ResourceTable->HandleResource);
    return IRQSTATUS_HANDLED;
}

void
HciInterruptCallback(
    _In_ UsbManagerController_t* baseController)
{
    OhciController_t* Controller = (OhciController_t*)baseController;
    reg32_t           InterruptStatus;

    TRACE("HciInterruptCallback(baseController=0x%" PRIxIN ")", baseController);
    InterruptStatus = atomic_exchange(&Controller->Base.InterruptStatus, 0);
ProcessInterrupt:
    // Process completed transfers.
    if (InterruptStatus & OHCI_PROCESS_EVENT) {
        UsbManagerProcessTransfers(&Controller->Base);
    }

    // Check root hub ports after a connect or disconnect event.
    if (InterruptStatus & OHCI_ROOTHUB_EVENT) {
        OHCICheckPorts(Controller, 0);
    }

    // Reset the queues and controller after a fatal error or overrun.
    if (InterruptStatus & (OHCI_FATAL_EVENT | OHCI_OVERRUN_EVENT)) {
        ERROR("OHCI: controller recovery event 0x%x, stopping queued transfers", InterruptStatus);
        if (OHCIQueueReset(Controller) != OS_EOK || OHCIReset(Controller) != OS_EOK) {
            // If stop or cleanup fails, keep the DMA resources allocated.
            // The teardown worker must retry before pools can be freed or reused.
            UsbManagerDeferControllerDestroy(baseController);
            return;
        }
        OHCISetMode(Controller, OHCI_CONTROL_ACTIVE);
    }
    // On resume, wait 20 ms before returning the controller to Operational.
    if (InterruptStatus & OHCI_RESUMEDETECT_EVENT) {
        thrd_sleep(&(struct timespec) { .tv_nsec = 20 * NSEC_PER_MSEC }, NULL);
        OHCISetMode(Controller, OHCI_CONTROL_ACTIVE);
    }

    // Stage 2: Apply link/unlink changes before enabling queues.
    if (InterruptStatus & OHCI_SOF_EVENT) {
        reg32_t HcControl = READ_VOLATILE(Controller->Registers->HcControl);
        
        // Retry retired batches at the frame boundary even if no TD completed.
        // This must finish before linking the next pending batch.
        UsbManagerProcessTransfers(&Controller->Base);
        UsbManagerScheduleTransfers(&Controller->Base);
        WRITE_VOLATILE(Controller->Registers->HcControl, HcControl | Controller->QueuesActive);
    }

    // The frame counter wraps from 0xFFFF to 0.
    if (InterruptStatus & OHCI_OVERFLOW_EVENT) {
        // TODO: Handle frame counter overflow.
    }

    // Check for interrupts received while processing this batch.
    InterruptStatus = atomic_exchange(&Controller->Base.InterruptStatus, 0);
    if (InterruptStatus != 0) {
        goto ProcessInterrupt;
    }
}
