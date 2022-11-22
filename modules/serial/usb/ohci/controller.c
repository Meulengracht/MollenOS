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
 * TODO:
 *    - Power Management
 */

//#define __TRACE

#include <os/mollenos.h>
#include <ddk/interrupt.h>
#include <ddk/utils.h>
#include "ohci.h"
#include <threads.h>
#include <string.h>
#include <stdlib.h>

oserr_t        OhciSetup(OhciController_t *Controller);
irqstatus_t OnFastInterrupt(InterruptFunctionTable_t*, InterruptResourceTable_t*);

UsbManagerController_t*
HciControllerCreate(
    _In_ BusDevice_t* Device)
{
    DMABuffer_t       dmaBuffer;
    DeviceIo_t*       ioBase  = NULL;
    DeviceInterrupt_t interrupt;
    OhciController_t* controller;
    oserr_t           oserr;
    int i;

    controller = (OhciController_t*)UsbManagerCreateController(Device, UsbOHCI, sizeof(OhciController_t));
    if (!controller) {
        return NULL;
    }
    
    // Get I/O Base, and for OHCI it'll be the first address we encounter
    // of type MMIO
    for (i = 0; i < __DEVICEMANAGER_MAX_IOSPACES; i++) {
        if (controller->Base.Device->IoSpaces[i].Type == DeviceIoMemoryBased) {
            ioBase = &controller->Base.Device->IoSpaces[i];
            break;
        }
    }

    // Sanitize that we found the io-space
    if (ioBase == NULL) {
        ERROR("No memory space found for ohci-controller");
        free(controller);
        return NULL;
    }

    // Trace
    TRACE("Found Io-Space (Type %u, Physical 0x%" PRIxIN ", Size 0x%" PRIxIN ")",
          ioBase->Type, ioBase->Access.Memory.PhysicalBase, ioBase->Access.Memory.Length);

    // Allocate the HCCA-space in low memory as controllers
    // have issues with higher memory (<2GB)
    dmaBuffer.length   = 0x1000;
    dmaBuffer.capacity = 0x1000;
    dmaBuffer.flags    = DMA_UNCACHEABLE | DMA_CLEAN;
    dmaBuffer.type     = DMA_TYPE_DRIVER_32LOW;

    oserr = DmaCreate(&dmaBuffer, &controller->HccaDMA);
    if (oserr != OS_EOK) {
        ERROR("Failed to allocate space for HCCA");
        free(controller);
        return NULL;
    }
    
    // Retrieve the physical location of the HCCA
    (void) DmaGetSGTable(&controller->HccaDMA, &controller->HccaDMATable, -1);

    controller->Hcca        = (OhciHCCA_t*)controller->HccaDMA.buffer;
    controller->Base.IoBase = ioBase;
    
    // Acquire the io-space
    if (AcquireDeviceIo(ioBase) != OS_EOK) {
        ERROR("Failed to create and acquire the io-space for ohci-controller");
        free(controller);
        return NULL;
    }

    // Trace
    TRACE("Io-Space was assigned virtual address 0x%" PRIxIN,
          ioBase->Access.Memory.VirtualBase);

    // Instantiate the register-access and disable interrupts on device
    controller->Registers = (OhciRegisters_t*)ioBase->Access.Memory.VirtualBase;
    WRITE_VOLATILE(controller->Registers->HcInterruptEnable, 0);
    WRITE_VOLATILE(controller->Registers->HcInterruptDisable, OHCI_MASTER_INTERRUPT);

    // Initialize the interrupt settings
    DeviceInterruptInitialize(&interrupt, Device);
    RegisterInterruptDescriptor(&interrupt, controller->Base.event_descriptor);
    RegisterFastInterruptHandler(&interrupt, (InterruptHandler_t)OnFastInterrupt);
    RegisterFastInterruptIoResource(&interrupt, ioBase);
    RegisterFastInterruptMemoryResource(&interrupt, (uintptr_t)controller, sizeof(OhciController_t), 0);
    RegisterFastInterruptMemoryResource(&interrupt, (uintptr_t)controller->Hcca, 0x1000, INTERRUPT_RESOURCE_DISABLE_CACHE);

    // Register interrupt
    TRACE("... register interrupt");
    controller->Base.Interrupt = RegisterInterruptSource(&interrupt, 0);

    // Enable device
    TRACE("... enabling device");
    oserr = IoctlDevice(controller->Base.Device->Base.Id, __DEVICEMANAGER_IOCTL_BUS,
                        (__DEVICEMANAGER_IOCTL_ENABLE | __DEVICEMANAGER_IOCTL_MMIO_ENABLE
            | __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE));
    if (oserr != OS_EOK) {
        ERROR("Failed to enable the ohci-controller");
        UnregisterInterruptSource(controller->Base.Interrupt);
        ReleaseDeviceIo(controller->Base.IoBase);
        free(controller);
        return NULL;
    }

    TRACE("... initializing device");
    if (OhciSetup(controller) == OS_EOK) {
        return &controller->Base;
    }
    else {
        HciControllerDestroy(&controller->Base);
        return NULL;
    }
}

oserr_t
HciControllerDestroy(
    _In_ UsbManagerController_t* Controller)
{
    // Unregister us with service
    UsbManagerDestroyController(Controller);

    // Cleanup scheduler
    OhciQueueDestroy((OhciController_t*)Controller);

    // Free resources
    if (((OhciController_t*)Controller)->Hcca != NULL) {
        DmaAttachmentUnmap(&((OhciController_t *) Controller)->HccaDMA);
        DmaDetach(&((OhciController_t *) Controller)->HccaDMA);
    }

    // Unregister the interrupt
    UnregisterInterruptSource(Controller->Interrupt);

    // Release the io-space
    ReleaseDeviceIo(Controller->IoBase);
    
    free(Controller);
    return OS_EOK;
}

void
HciTimerCallback(
    _In_ UsbManagerController_t* baseController)
{
    // do nothing
}

void
OhciSetMode(
    _In_ OhciController_t* Controller, 
    _In_ reg32_t           Mode)
{
    // Read in value, mask it off, then update
    reg32_t Value = READ_VOLATILE(Controller->Registers->HcControl);
    Value                            &= ~(OHCI_CONTROL_SUSPEND);
    Value                            |= Mode;
    WRITE_VOLATILE(Controller->Registers->HcControl, Value);
}

oserr_t
OhciTakeControl(
    _In_ OhciController_t* Controller)
{
    reg32_t  HcControl;
    uint32_t Temp = 0;
    int      i    = 0;

    // Trace
    TRACE("OhciTakeControl()");

    // Does SMM hold control of chip? Then ask for it back
    HcControl = READ_VOLATILE(Controller->Registers->HcControl);
    if (HcControl & OHCI_CONTROL_IR) {
        Temp = READ_VOLATILE(Controller->Registers->HcCommandStatus);
        Temp |= OHCI_COMMAND_OWNERSHIP;
        WRITE_VOLATILE(Controller->Registers->HcCommandStatus, Temp);

        // Now we wait for the bit to clear
        WaitForConditionWithFault(i, (READ_VOLATILE(Controller->Registers->HcControl) & OHCI_CONTROL_IR) == 0, 250, 10);

        if (i != 0) {
            // Didn't work, we try to clear the IR bit
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
    }
    // Did BIOS play tricks on us?
    else if ((HcControl & OHCI_CONTROL_STATE_MASK) != 0) {
        // If it's suspended, resume and wait 10 ms
        if ((HcControl & OHCI_CONTROL_STATE_MASK) != OHCI_CONTROL_ACTIVE) {
            OhciSetMode(Controller, OHCI_CONTROL_ACTIVE);
            thrd_sleep2(10);
        }
    }
    else {
        // Cold boot, wait 10 ms
        thrd_sleep2(10);
    }
    return OS_EOK;
}

oserr_t
OhciReset(
    _In_ OhciController_t* Controller)
{
    reg32_t Temporary = 0;
    reg32_t FmInt     = 0;
    int     i         = 0;

    TRACE("OhciReset()");

    // Verify HcFmInterval and store the original value
    FmInt = READ_VOLATILE(Controller->Registers->HcFmInterval);

    // What the hell? You are supposed to be able 
    // to set this yourself!
    if (OHCI_FMINTERVAL_GETFSMP(FmInt) == 0) {
        FmInt |= OHCI_FMINTERVAL_FSMP(FmInt) << 16;
    }

    // Sanitize the FI.. this should also be set but there
    // are cases it isn't....
    if ((FmInt & OHCI_FMINTERVAL_FIMASK) == 0) {
        FmInt |= OHCI_FMINTERVAL_FI;
    }

    // We should check here if HcControl has RemoteWakeup Connected 
    // and then set device to remote wake capable
    // Disable interrupts during this reset
    TRACE(" > Suspending controller");
    WRITE_VOLATILE(Controller->Registers->HcInterruptDisable, OHCI_MASTER_INTERRUPT);

    // Suspend the controller just in case it's running
    // and wait for it to suspend
    OhciSetMode(Controller, OHCI_CONTROL_SUSPEND);
    thrd_sleep2(10);

    // Toggle bit 0 to initiate a reset
    Temporary = READ_VOLATILE(Controller->Registers->HcCommandStatus);
    Temporary |= OHCI_COMMAND_RESET;
    WRITE_VOLATILE(Controller->Registers->HcCommandStatus, Temporary);

    // Wait for reboot (takes maximum of 10 ms)
    // But the world is not perfect, given it up to 50
    TRACE(" > Resetting controller");
    WaitForConditionWithFault(i, (READ_VOLATILE(Controller->Registers->HcCommandStatus) & OHCI_COMMAND_RESET) == 0, 50, 1);

    // Sanitize the fault variable
    if (i != 0) {
        ERROR("Controller failed to reboot");
        ERROR("Reset Timeout :(");
        return OS_EUNKNOWN;
    }

    //**************************************
    // We now have 2 ms to complete setup
    // and put it in Operational Mode
    //**************************************

    // Restore the FmInt and toggle the FIT
    FmInt ^= 0x80000000;
    WRITE_VOLATILE(Controller->Registers->HcFmInterval, FmInt);

    // Setup the Hcca Address and initiate some members of the HCCA
    TRACE("... hcca address 0x%" PRIxIN, Controller->HccaDMATable.entries[0].address);
    WRITE_VOLATILE(Controller->Registers->HcHCCA, Controller->HccaDMATable.entries[0].address);
    Controller->Hcca->CurrentFrame = 0;
    Controller->Hcca->HeadDone     = 0;

    // Setup initial ED queues
    WRITE_VOLATILE(Controller->Registers->HcControlHeadED, 0);
    WRITE_VOLATILE(Controller->Registers->HcControlCurrentED, 0);
    WRITE_VOLATILE(Controller->Registers->HcBulkHeadED, 0);
    WRITE_VOLATILE(Controller->Registers->HcBulkCurrentED, 0);

    // Set HcEnableInterrupt to all except SOF and OC
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
    Temporary = (FmInt & OHCI_FMINTERVAL_FIMASK);
    WRITE_VOLATILE(Controller->Registers->HcPeriodicStart, (Temporary / 10U) * 9U);

    // Clear Lists, Mode, Ratio and IR
    Temporary = READ_VOLATILE(Controller->Registers->HcControl);
    Temporary &= ~(OHCI_CONTROL_ALL_ACTIVE | OHCI_CONTROL_SUSPEND | OHCI_CONTROL_RATIO_MASK | OHCI_CONTROL_IR);

    // Set Ratio (4:1) and Mode (Operational)
    Temporary |= (OHCI_CONTROL_RATIO_MASK | OHCI_CONTROL_ACTIVE);
    Temporary |= OHCI_CONTROL_PERIODIC_ACTIVE;
    Temporary |= OHCI_CONTROL_ISOC_ACTIVE;
    Temporary |= OHCI_CONTROL_REMOTEWAKE;
    WRITE_VOLATILE(Controller->Registers->HcControl, Temporary);
    
    Controller->QueuesActive = OHCI_CONTROL_PERIODIC_ACTIVE | OHCI_CONTROL_ISOC_ACTIVE;
    TRACE(" > Wrote control 0x%x to controller", Temporary);
    return OS_EOK;
}

oserr_t
OhciSetup(
    _In_ OhciController_t* Controller)
{
    reg32_t Temporary;
    int     i;

    // Trace
    TRACE("OhciSetup()");

    // Retrieve the revision of the controller, we support 0x10 && 0x11
    Temporary = READ_VOLATILE(Controller->Registers->HcRevision) & 0xFF;
    if (Temporary != OHCI_REVISION1 && Temporary != OHCI_REVISION11) {
        ERROR("Invalid OHCI Revision (0x%x)", Temporary);
        return OS_EUNKNOWN;
    }

    // Initialize the queue system
    OhciQueueInitialize(Controller);

    // Last step is to take ownership, reset the controller and initialize
    // the registers, all resource must be allocated before this
    if (OhciTakeControl(Controller) != OS_EOK || OhciReset(Controller) != OS_EOK) {
        ERROR("Failed to initialize the ohci-controller");
        return OS_EUNKNOWN;
    }
    
    // Controller should now be in a running state
    TRACE("Controller %u Started, Control 0x%x, Ints 0x%x, FmInterval 0x%x",
        Controller->Base.Id, Controller->Registers->HcControl, 
        Controller->Registers->HcInterruptEnable, Controller->Registers->HcFmInterval);

    // We are not completely done yet, we need to figure out the
    // power-mode of the controller, and we need the port-count
    if (READ_VOLATILE(Controller->Registers->HcRhDescriptorA) & (1 << 9)) {
        // Power is always on
        WRITE_VOLATILE(Controller->Registers->HcRhStatus, OHCI_STATUS_POWER_ENABLED);
        WRITE_VOLATILE(Controller->Registers->HcRhDescriptorB, 0);
        Controller->PowerMode = AlwaysOn;
    }
    else {
        // Ports have individual power
        if (READ_VOLATILE(Controller->Registers->HcRhDescriptorA) & (1 << 8)) {
            // We prefer this, we can control each port's power
            WRITE_VOLATILE(Controller->Registers->HcRhDescriptorB, 0xFFFF0000);
            Controller->PowerMode = PortControl;
        }
        else {
            // Oh well, it's either all on or all off
            WRITE_VOLATILE(Controller->Registers->HcRhDescriptorB, 0);
            WRITE_VOLATILE(Controller->Registers->HcRhStatus, OHCI_STATUS_POWER_ENABLED);
            Controller->PowerMode = GlobalControl;
        }
    }

    // Get Port count from (DescriptorA & 0x7F)
    Controller->Base.PortCount = READ_VOLATILE(Controller->Registers->HcRhDescriptorA) & 0x7F;
    if (Controller->Base.PortCount > OHCI_MAXPORTS) {
        Controller->Base.PortCount = OHCI_MAXPORTS;
    }

    // Clear RhA
    Temporary = READ_VOLATILE(Controller->Registers->HcRhDescriptorA);
    Temporary &= ~(0x00000000 | OHCI_DESCRIPTORA_DEVICETYPE);
    WRITE_VOLATILE(Controller->Registers->HcRhDescriptorA, Temporary);

    // Get Power On Delay
    // PowerOnToPowerGoodTime (24 - 31)
    // This byte specifies the duration HCD has to wait before
    // accessing a powered-on Port of the Root Hub.
    // It is implementation-specific.  The unit of time is 2 ms.
    // The duration is calculated as POTPGT * 2 ms.
    Temporary = READ_VOLATILE(Controller->Registers->HcRhDescriptorA);
    Temporary >>= 24;
    Temporary &= 0x000000FF;
    Temporary *= 2;

    // Anything under 100 ms is not good in OHCI
    if (Temporary < 100) {
        Temporary = 100;
    }
    Controller->PowerOnDelayMs = Temporary;

    TRACE("Ports %u (power mode %u, power delay %u)",
        Controller->Base.PortCount, Controller->PowerMode, Temporary);
    
    // Register the controller before starting
    if (UsbManagerRegisterController(&Controller->Base) != OS_EOK) {
        ERROR("Failed to register uhci controller with the system.");
    }

    // Now we can enable hub events (and clear interrupts)
    WRITE_VOLATILE(Controller->Registers->HcInterruptStatus, 
        READ_VOLATILE(Controller->Registers->HcInterruptStatus));
    WRITE_VOLATILE(Controller->Registers->HcInterruptEnable, 
        READ_VOLATILE(Controller->Registers->HcInterruptEnable) | OHCI_ROOTHUB_EVENT);
    
    // If it's individual port power, iterate through port and power up
    if (Controller->PowerMode == PortControl) {
        for (i = 0; i < (int)Controller->Base.PortCount; i++) {
            reg32_t PortStatus = READ_VOLATILE(Controller->Registers->HcRhPortStatus[i]);
            if (!(PortStatus & OHCI_PORT_POWER)) {
                WRITE_VOLATILE(Controller->Registers->HcRhPortStatus[i], OHCI_PORT_POWER);
            }
        }
    }
    
    // Wait for ports to power up in any case, even if power is always on/global
    thrd_sleep2(Controller->PowerOnDelayMs);
    return OhciPortsCheck(Controller, 1);
}
