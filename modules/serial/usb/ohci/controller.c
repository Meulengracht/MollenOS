/**
 * Copyright 2023, Philip Meulengracht
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
#define __need_quantity

#include <os/handle.h>
#include <os/mollenos.h>
#include <ddk/interrupts/interrupt.h>
#include <ddk/utils.h>
#include <ddk/barrier.h>
#include <os/device.h>
#include "ohci.h"
#include <threads.h>
#include <string.h>
#include <stdlib.h>

oserr_t     OHCISetup(OhciController_t *controller);
irqstatus_t OnFastInterrupt(InterruptFunctionTable_t*, InterruptResourceTable_t*);

UsbManagerController_t*
HCIControllerCreate(
    _In_ Device_t* description)
{
    BusDevice_t*             device = (BusDevice_t*)description;
    const struct DMASegment* segments;
    uint32_t                 segmentCount;
    DeviceIo_t*              ioBase  = NULL;
    OhciController_t*        controller;
    oserr_t                  oserr;

    if (description->Length != sizeof(BusDevice_t)) {
        return NULL;
    }

    controller = (OhciController_t*)UsbManagerCreateController(&device->Base, USBCONTROLLER_KIND_OHCI, sizeof(OhciController_t));
    if (!controller) {
        return NULL;
    }
    
    // OHCI registers use the first MMIO space.
    for (int i = 0; i < __DEVICEMANAGER_MAX_IOSPACES; i++) {
        if (((BusDevice_t*)controller->Base.Device)->IoSpaces[i].Type == DeviceIoMemoryBased) {
            ioBase = &((BusDevice_t*)controller->Base.Device)->IoSpaces[i];
            break;
        }
    }

    // A register space is required before setup can continue.
    if (ioBase == NULL) {
        ERROR("No memory space found for ohci-controller");
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }

    // Trace
    TRACE("Found Io-Space (Type %u, Physical 0x%" PRIxIN ", Size 0x%" PRIxIN ")",
          ioBase->Type, ioBase->Access.Memory.PhysicalBase, ioBase->Access.Memory.Length);

    // Acquire the io-space
    if (AcquireDeviceIo(ioBase) != OS_EOK) {
        ERROR("Failed to create and acquire the io-space for ohci-controller");
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }

    controller->Base.IoBase = ioBase;

    // Trace
    TRACE("Io-Space was assigned virtual address 0x%" PRIxIN,
          ioBase->Access.Memory.VirtualBase);

    // Map the registers and disable device interrupts before setup.
    controller->Registers = (OhciRegisters_t*)ioBase->Access.Memory.VirtualBase;
    WRITE_VOLATILE(controller->Registers->HcInterruptEnable, 0);
    WRITE_VOLATILE(controller->Registers->HcInterruptDisable, OHCI_MASTER_INTERRUPT);

    // Keep the HCCA mapped for DMA until the controller is destroyed.
    // Keep the existing 2 GB address limit for older OHCI implementations.
    oserr = DMAOpen(controller->Base.Device->Id, INT32_MAX, &controller->Base.DmaContext);
    if (oserr == OS_EOK) {
        oserr = DMAAllocate(controller->Base.DmaContext,
            &(struct DMARequirements) { .Length = 4096, .Alignment = 256, .Contiguous = true },
            &controller->HccaDMA);
    }
    if (oserr != OS_EOK) {
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }

    segments = DMABufferSegments(controller->HccaDMA, &segmentCount);
    controller->HccaDeviceAddress = (uintptr_t)segments[0].Address;
    controller->Hcca = DMABufferData(controller->HccaDMA);

    // Store the interrupt allocation in the controller. All handler mappings
    // must be released before freeing the controller memory.
    oserr = UsbManagerConfigureInterrupt(
        &controller->Base,
        (InterruptHandler_t)OnFastInterrupt,
        sizeof(OhciController_t)
    );
    if (oserr == OS_EOK) {
        oserr = DeviceInterruptAddMemoryResource(
            &controller->Base.Interrupt,
            0,
            (uintptr_t)controller->Hcca,
            0x1000,
            INTERRUPT_RESOURCE_DISABLE_CACHE
        );
    }
    if (oserr == OS_EOK) {
        oserr = DeviceInterruptEnable(&controller->Base.Interrupt);
    }
    if (oserr != OS_EOK) {
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }

    // Enable device
    TRACE("... enabling device");
    oserr = OSDeviceIOCtl(
            controller->Base.Device->Id,
            OSIOCTLREQUEST_BUS_CONTROL,
            &(struct OSIOCtlBusControl) {
                    .Flags = (__DEVICEMANAGER_IOCTL_ENABLE | __DEVICEMANAGER_IOCTL_MMIO_ENABLE |
                              __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE)
            }, sizeof(struct OSIOCtlBusControl)
    );
    if (oserr != OS_EOK) {
        ERROR("Failed to enable the ohci-controller");
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }

    TRACE("... initializing device");
    if (OHCISetup(controller) == OS_EOK &&
        UsbManagerCompleteControllerInitialization(&controller->Base)) {
        return &controller->Base;
    }
    else {
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }
}

oserr_t
HCIControllerDestroy(
    _In_ UsbManagerController_t* base)
{
    OhciController_t* controller = (OhciController_t*)base;
    oserr_t           status;

    if (!UsbManagerBeginControllerDestroy(base)) {
        return OS_EBUSY;
    }
    if (controller->Registers != NULL) {
        WRITE_VOLATILE(controller->Registers->HcInterruptDisable, OHCI_MASTER_INTERRUPT);
        status = OHCIStop(controller);
        if (status != OS_EOK) {
            goto retry;
        }
        base->State.DmaStopped = true;
    }
    
    // Stop the controller and disconnect the handler before freeing their memory.
    // If cleanup fails, leave the controller registered so we can retry.
    // Cleared pointers mark the resources already released.
    status = DeviceInterruptDestroy(&base->Interrupt);
    if (status != OS_EOK) {
        goto retry;
    }
    
    status = OHCIQueueDestroy(controller);
    if (status != OS_EOK) {
        goto retry;
    }
    
    status = DMAFree(&controller->HccaDMA);
    if (status != OS_EOK) {
        goto retry;
    }
    
    controller->Hcca = NULL;
    status = DMAClose(&base->DmaContext);
    if (status != OS_EOK) {
        goto retry;
    }
    
    if (base->IoBase != NULL) {
        status = ReleaseDeviceIo(base->IoBase);
        if (status != OS_EOK) {
            goto retry;
        }
        base->IoBase = NULL;
    }
    
    UsbManagerDestroyController(base);
    free(base->Device);
    free(base);
    return OS_EOK;

retry:
    WARNING("OHCI: controller cleanup failed (%u), will retry", status);
    UsbManagerRetryControllerDestroy(base);
    return status;
}

void
HciTimerCallback(
    _In_ UsbManagerController_t* baseController)
{
    UsbManagerProcessTransfers(baseController);
}

void
OHCISetMode(
    _In_ OhciController_t* controller,
    _In_ reg32_t           mode)
{
    // Preserve the other control bits when changing mode.
    reg32_t value = READ_VOLATILE(controller->Registers->HcControl);
    value &= ~OHCI_CONTROL_STATE_MASK;
    if (mode == OHCI_CONTROL_ACTIVE) {
        value |= controller->QueuesActive;
        controller->Base.State.DmaStopped = false;
        dma_wmb();
    }
    value |= mode;
    WRITE_VOLATILE(controller->Registers->HcControl, value);
}

oserr_t
OHCITakeControl(
    _In_ OhciController_t* Controller)
{
    reg32_t  HcControl;
    uint32_t Temp = 0;
    int      i    = 0;

    // Trace
    TRACE("OHCITakeControl()");

    // If SMM controls the chip, request control from firmware.
    HcControl = READ_VOLATILE(Controller->Registers->HcControl);
    if (HcControl & OHCI_CONTROL_IR) {
        Temp = READ_VOLATILE(Controller->Registers->HcCommandStatus);
        Temp |= OHCI_COMMAND_OWNERSHIP;
        WRITE_VOLATILE(Controller->Registers->HcCommandStatus, Temp);

        // Wait for firmware to clear the interrupt routing bit.
        WaitForConditionWithFault(i, (READ_VOLATILE(Controller->Registers->HcControl) & OHCI_CONTROL_IR) == 0, 250, 10);

        if (i != 0) {
            // Firmware timed out. Try clearing the IR bit directly.
            Temp = READ_VOLATILE(Controller->Registers->HcControl);
            Temp &= ~OHCI_CONTROL_IR;
            WRITE_VOLATILE(Controller->Registers->HcControl, Temp);
            WaitForConditionWithFault(i, (READ_VOLATILE(Controller->Registers->HcControl) & OHCI_CONTROL_IR) == 0, 250, 10);
            if (i != 0) {
                ERROR("failed to clear routing bit");
                ERROR("SMM Won't give us the Controller, we're backing down >(");
                return OS_EUNKNOWN;
            }
        }
    } else if ((HcControl & OHCI_CONTROL_STATE_MASK) != 0) { // BIOS left the controller configured.
        // If it's suspended, resume and wait 10 ms
        if ((HcControl & OHCI_CONTROL_STATE_MASK) != OHCI_CONTROL_ACTIVE) {
            OHCISetMode(Controller, OHCI_CONTROL_ACTIVE);
            thrd_sleep(&(struct timespec) { .tv_nsec = 10 * NSEC_PER_MSEC }, NULL);
        }
    } else {
        // Cold boot, wait 10 ms
        thrd_sleep(&(struct timespec) { .tv_nsec = 10 * NSEC_PER_MSEC }, NULL);
    }
    return OS_EOK;
}

oserr_t
OHCIStop(
    _In_ OhciController_t* controller)
{
    reg32_t control = READ_VOLATILE(controller->Registers->HcControl);

    // Suspend stops descriptor fetches for the next frame. The current 1 ms
    // frame must finish before the CPU can reuse the DMA memory.
    WRITE_VOLATILE(controller->Registers->HcControl,
        (control & ~(OHCI_CONTROL_STATE_MASK | OHCI_CONTROL_ALL_ACTIVE)) | OHCI_CONTROL_SUSPEND);
    thrd_sleep(&(struct timespec) { .tv_nsec = 2 * NSEC_PER_MSEC }, NULL);
    
    control = READ_VOLATILE(controller->Registers->HcControl);
    if ((control & OHCI_CONTROL_STATE_MASK) != OHCI_CONTROL_SUSPEND) {
        WARNING("OHCI: controller did not suspend; keeping DMA resources");
        return OS_ETIMEOUT;
    }
    
    controller->Base.State.DmaStopped = true;
    return OS_EOK;
}

oserr_t
OHCIReset(
    _In_ OhciController_t* Controller)
{
    reg32_t tempValue;
    reg32_t fmInterval;
    int     i = 0;

    TRACE("OHCIReset()");

    // Verify HcFmInterval and store the original value
    fmInterval = READ_VOLATILE(Controller->Registers->HcFmInterval);

    // Fill in the maximum packet size if firmware left it unset.
    if (OHCI_FMINTERVAL_GETFSMP(fmInterval) == 0) {
        fmInterval |= OHCI_FMINTERVAL_FSMP(fmInterval) << 16;
    }

    // Use the default frame interval if firmware left it unset.
    if ((fmInterval & OHCI_FMINTERVAL_FIMASK) == 0) {
        fmInterval |= OHCI_FMINTERVAL_FI;
    }

    // TODO: Check RemoteWakeupConnected in HcControl before marking the
    // device as capable of remote wakeup.
    // Disable interrupts during this reset
    TRACE("OHCIReset suspending controller");
    WRITE_VOLATILE(Controller->Registers->HcInterruptDisable, OHCI_MASTER_INTERRUPT);

    // The controller must be suspended before reset.
    OHCISetMode(Controller, OHCI_CONTROL_SUSPEND);
    thrd_sleep(&(struct timespec) { .tv_nsec = 10 * NSEC_PER_MSEC }, NULL);

    // Set bit 0 to start a reset.
    tempValue = READ_VOLATILE(Controller->Registers->HcCommandStatus);
    tempValue |= OHCI_COMMAND_RESET;
    WRITE_VOLATILE(Controller->Registers->HcCommandStatus, tempValue);

    // Reset should finish within 10 ms. Allow up to 50 ms for slow controllers.
    TRACE(" > Resetting controller");
    WaitForConditionWithFault(i, (READ_VOLATILE(Controller->Registers->HcCommandStatus) & OHCI_COMMAND_RESET) == 0, 50, 1);

    // Stop setup if reset timed out.
    if (i != 0) {
        ERROR("Controller failed to reboot");
        ERROR("Reset Timeout :(");
        return OS_EUNKNOWN;
    }

    //**************************************
    // We now have 2 ms to complete setup
    // and put it in Operational Mode
    //**************************************

    // Restore the fmInterval and toggle the FIT
    fmInterval ^= 0x80000000;
    WRITE_VOLATILE(Controller->Registers->HcFmInterval, fmInterval);

    // Setup the Hcca Address and initiate some members of the HCCA
    TRACE("... hcca address 0x%" PRIxIN, Controller->HccaDeviceAddress);
    WRITE_VOLATILE(Controller->Registers->HcHCCA, Controller->HccaDeviceAddress);
    Controller->Hcca->CurrentFrame = 0;
    Controller->Hcca->HeadDone     = 0;

    // Setup initial ED queues
    WRITE_VOLATILE(Controller->Registers->HcControlHeadED, 0);
    WRITE_VOLATILE(Controller->Registers->HcControlCurrentED, 0);
    WRITE_VOLATILE(Controller->Registers->HcBulkHeadED, 0);
    WRITE_VOLATILE(Controller->Registers->HcBulkCurrentED, 0);

    // Disable SOF, root hub and control change interrupts.
    WRITE_VOLATILE(Controller->Registers->HcInterruptDisable, OHCI_SOF_EVENT | 
        OHCI_ROOTHUB_EVENT | OHCI_OWNERSHIP_EVENT);

    // Clear out INTR state and initialize the interrupt enable
    WRITE_VOLATILE(Controller->Registers->HcInterruptStatus, 
        READ_VOLATILE(Controller->Registers->HcInterruptStatus));
    WRITE_VOLATILE(Controller->Registers->HcInterruptEnable, OHCI_OVERRUN_EVENT 
        | OHCI_PROCESS_EVENT | OHCI_RESUMEDETECT_EVENT | OHCI_FATAL_EVENT
        | OHCI_OVERFLOW_EVENT | OHCI_MASTER_INTERRUPT);

    // Set HcPeriodicStart to a value that is 90% of 
    // FrameInterval in HcFmInterval
    tempValue = (fmInterval & OHCI_FMINTERVAL_FIMASK);
    WRITE_VOLATILE(Controller->Registers->HcPeriodicStart, (tempValue / 10U) * 9U);

    // Clear Lists, Mode, Ratio and IR
    tempValue = READ_VOLATILE(Controller->Registers->HcControl);
    tempValue &= ~(OHCI_CONTROL_ALL_ACTIVE | OHCI_CONTROL_SUSPEND | OHCI_CONTROL_RATIO_MASK | OHCI_CONTROL_IR);

    // Set Ratio (4:1) and Mode (Operational)
    tempValue |= (OHCI_CONTROL_RATIO_MASK | OHCI_CONTROL_ACTIVE);
    tempValue |= OHCI_CONTROL_PERIODIC_ACTIVE;
    tempValue |= OHCI_CONTROL_ISOC_ACTIVE;
    tempValue |= OHCI_CONTROL_REMOTEWAKE;
    WRITE_VOLATILE(Controller->Registers->HcControl, tempValue);
    
    Controller->Base.State.DmaStopped = false;
    Controller->QueuesActive = OHCI_CONTROL_PERIODIC_ACTIVE | OHCI_CONTROL_ISOC_ACTIVE;
    TRACE(" > Wrote control 0x%x to controller", tempValue);
    return OS_EOK;
}

oserr_t
OHCISetup(
    _In_ OhciController_t* controller)
{
    reg32_t tempValue;
    int     i;

    // Trace
    TRACE("OHCISetup()");

    // Retrieve the revision of the controller, we support 0x10 && 0x11
    tempValue = READ_VOLATILE(controller->Registers->HcRevision) & 0xFF;
    if (tempValue != OHCI_REVISION1 && tempValue != OHCI_REVISION11) {
        ERROR("Invalid OHCI Revision (0x%x)", tempValue);
        return OS_EUNKNOWN;
    }

    // If setup fails, leave the scheduler attached for controller cleanup.
    if (OHCIQueueInitialize(controller) != OS_EOK) {
        return OS_EOOM;
    }

    // All resources must be allocated before taking control from firmware
    // and resetting the controller.
    if (OHCITakeControl(controller) != OS_EOK || OHCIReset(controller) != OS_EOK) {
        ERROR("Failed to initialize the ohci-controller");
        return OS_EUNKNOWN;
    }
    
    // Controller should now be in a running state
    TRACE("Controller %u Started, Control 0x%x, Ints 0x%x, FmInterval 0x%x",
          controller->Base.Id, controller->Registers->HcControl,
          controller->Registers->HcInterruptEnable, controller->Registers->HcFmInterval);

    // Read the root hub power mode and port count.
    if (READ_VOLATILE(controller->Registers->HcRhDescriptorA) & (1 << 9)) {
        // Power is always on
        WRITE_VOLATILE(controller->Registers->HcRhStatus, OHCI_STATUS_POWER_ENABLED);
        WRITE_VOLATILE(controller->Registers->HcRhDescriptorB, 0);
        controller->PowerMode = OHCIPOWERMODE_ALWAYSON;
    }
    else {
        // Check whether the ports support individual power control.
        if (READ_VOLATILE(controller->Registers->HcRhDescriptorA) & (1 << 8)) {
            // Enable power control for each port.
            WRITE_VOLATILE(controller->Registers->HcRhDescriptorB, 0xFFFF0000);
            controller->PowerMode = OHCIPOWERMODE_PORTCONTROL;
        } else {
            // All ports share a single power control.
            WRITE_VOLATILE(controller->Registers->HcRhDescriptorB, 0);
            WRITE_VOLATILE(controller->Registers->HcRhStatus, OHCI_STATUS_POWER_ENABLED);
            controller->PowerMode = OHCIPOWERMODE_GLOBALCONTROL;
        }
    }

    // Get Port count from (DescriptorA & 0x7F)
    controller->Base.PortCount = READ_VOLATILE(controller->Registers->HcRhDescriptorA) & 0x7F;
    if (controller->Base.PortCount > OHCI_MAXPORTS) {
        controller->Base.PortCount = OHCI_MAXPORTS;
    }

    // Clear the device type bit in root hub descriptor A.
    tempValue = READ_VOLATILE(controller->Registers->HcRhDescriptorA);
    tempValue &= ~(0x00000000 | OHCI_DESCRIPTORA_DEVICETYPE);
    WRITE_VOLATILE(controller->Registers->HcRhDescriptorA, tempValue);

    // Before accessing a powered port, wait for PowerOnToPowerGoodTime.
    // Bits 24-31 give this controller-specific delay in units of 2 ms.
    tempValue = READ_VOLATILE(controller->Registers->HcRhDescriptorA);
    tempValue >>= 24;
    tempValue &= 0x000000FF;
    tempValue *= 2;

    // Wait at least 100 ms for port power to settle.
    if (tempValue < 100) {
        tempValue = 100;
    }
    controller->PowerOnDelayMs = tempValue;

    TRACE("Ports %u (power mode %u, power delay %u)",
          controller->Base.PortCount, controller->PowerMode, tempValue);
    
    // Register the controller before enabling root hub events.
    if (UsbManagerRegisterController(&controller->Base) != OS_EOK) {
        ERROR("Failed to register uhci controller with the system.");
    }

    // Now we can enable hub events (and clear interrupts)
    WRITE_VOLATILE(controller->Registers->HcInterruptStatus,
                   READ_VOLATILE(controller->Registers->HcInterruptStatus));
    WRITE_VOLATILE(controller->Registers->HcInterruptEnable,
                   READ_VOLATILE(controller->Registers->HcInterruptEnable) | OHCI_ROOTHUB_EVENT);
    
    // Power each port when individual power control is enabled.
    if (controller->PowerMode == OHCIPOWERMODE_PORTCONTROL) {
        for (i = 0; i < (int)controller->Base.PortCount; i++) {
            reg32_t PortStatus = READ_VOLATILE(controller->Registers->HcRhPortStatus[i]);
            if (!(PortStatus & OHCI_PORT_POWER)) {
                WRITE_VOLATILE(controller->Registers->HcRhPortStatus[i], OHCI_PORT_POWER);
            }
        }
    }
    
    // Wait for ports to power up in any case, even if power is always on/global
    thrd_sleep(&(struct timespec) { .tv_nsec = controller->PowerOnDelayMs * NSEC_PER_MSEC }, NULL);
    return OHCICheckPorts(controller, 1);
}
