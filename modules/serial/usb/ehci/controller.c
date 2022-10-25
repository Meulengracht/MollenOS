/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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

#define __TRACE

#include <ddk/interrupt.h>
#include <ddk/utils.h>
#include "../common/hci.h"
#include "ehci.h"
#include <threads.h>
#include <stdlib.h>
#include <string.h>

/* Prototypes 
 * This is to keep the create/destroy at the top of the source file */
oserr_t        EhciSetup(EhciController_t *Controller);
irqstatus_t OnFastInterrupt(InterruptFunctionTable_t*, InterruptResourceTable_t*);

UsbManagerController_t*
HciControllerCreate(
    _In_ BusDevice_t* Device)
{
    EhciController_t* controller;
    DeviceInterrupt_t interrupt;
    DeviceIo_t*       ioBase = NULL;
    int i;

    controller = (EhciController_t*)UsbManagerCreateController(Device, UsbEHCI, sizeof(EhciController_t));
    if (!controller) {
        return NULL;
    }

    // Get I/O Base, and for EHCI it'll be the first address we encounter
    // of type MMIO
    for (i = 0; i < __DEVICEMANAGER_MAX_IOSPACES; i++) {
        if (controller->Base.Device->IoSpaces[i].Type == DeviceIoMemoryBased) {
            ioBase = &controller->Base.Device->IoSpaces[i];
            break;
        }
    }

    // Sanitize that we found the io-space
    if (!ioBase) {
        ERROR("No memory space found for ehci-controller");
        free(controller);
        return NULL;
    }

    TRACE("Found Io-Space (Type %u, Physical 0x%x, Size 0x%x)",
          ioBase->Type, ioBase->Access.Memory.PhysicalBase, ioBase->Access.Memory.Length);

    // Acquire the io-space
    if (AcquireDeviceIo(ioBase) != OS_EOK) {
        ERROR("Failed to create and acquire the io-space for ehci-controller");
        free(controller);
        return NULL;
    }

    controller->Base.IoBase = ioBase;

    // Trace
    TRACE("Io-Space was assigned virtual address 0x%x", ioBase->Access.Memory.VirtualBase);

    // Instantiate the register-access
    controller->CapRegisters = (EchiCapabilityRegisters_t*)ioBase->Access.Memory.VirtualBase;
    controller->OpRegisters  = (EchiOperationalRegisters_t*)
        (ioBase->Access.Memory.VirtualBase + controller->CapRegisters->Length);

    // Initialize the interrupt settings
    DeviceInterruptInitialize(&interrupt, Device);
    RegisterFastInterruptHandler(&interrupt, (InterruptHandler_t)OnFastInterrupt);
    RegisterFastInterruptIoResource(&interrupt, ioBase);
    RegisterFastInterruptMemoryResource(&interrupt, (uintptr_t)controller, sizeof(EhciController_t), 0);
    
    // Register interrupt
    RegisterInterruptDescriptor(&interrupt, controller->Base.event_descriptor);
    controller->Base.Interrupt = RegisterInterruptSource(&interrupt, 0);

    // Enable device
    if (IoctlDevice(controller->Base.Device->Base.Id, __DEVICEMANAGER_IOCTL_BUS,
                    (__DEVICEMANAGER_IOCTL_ENABLE | __DEVICEMANAGER_IOCTL_MMIO_ENABLE
            | __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE)) != OS_EOK) {
        ERROR("Failed to enable the ehci-controller");
        UnregisterInterruptSource(controller->Base.Interrupt);
        ReleaseDeviceIo(controller->Base.IoBase);
        free(controller);
        return NULL;
    }

    // Now that all formalities has been taken care
    // off we can actually setup controller
    if (EhciSetup(controller) == OS_EOK) {
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
    // Unregister, then destroy
    UsbManagerDestroyController(Controller);

    // Cleanup scheduler
    EhciQueueDestroy((EhciController_t*)Controller);

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
EhciDisableLegacySupport(
    _In_ EhciController_t* Controller)
{
    reg32_t cparams = READ_VOLATILE(Controller->CapRegisters->CParams);
    reg32_t eecp;

    // Debug
    TRACE("EhciDisableLegacySupport()");

    // Pci Registers
    // BAR0 - Usb Base Registers
    // 0x60 - Revision
    // 0x61 - Frame Length Adjustment
    // 0x62/3 - Port Wake capabilities
    // ????? - Usb Legacy Support Extended Capability Register
    // ???? + 4 - Usb Legacy Support Control And Status Register
    // The above means ???? = EECP. EECP Offset in PCI space where
    // we can find the above registers
    eecp = EHCI_CPARAM_EECP(cparams);

    // If the eecp is valid ( >= 0x40), then there are a few
    // cases we can handle, but if its not valid, there is no legacy
    if (eecp >= 0x40) {
        size_t semaphore = 0;
        size_t capId    = 0;
        size_t nextEecp = 0;

        // Get the extended capability register
        // We read the second byte, because it contains the BIOS Semaphore
        while (1) {
            // Retrieve capability id
            if (IoctlDeviceEx(Controller->Base.Device->Base.Id, 0, eecp, &capId, 1) != OS_EOK) {
                return;
            }

            // Legacy support?
            if (capId == 0x01) {
                break;
            }

            // Nope, follow eecp link
            if (IoctlDeviceEx(Controller->Base.Device->Base.Id, 0, eecp + 0x1, &nextEecp, 1) != OS_EOK) {
                return;
            }

            // Sanitize end of link
            if (nextEecp == 0x00) {
                break;
            }
            else {
                eecp = nextEecp;
            }
        }

        // Only continue if Id == 0x01
        if (capId == 0x01) {
            size_t Zero = 0;
            if (IoctlDeviceEx(Controller->Base.Device->Base.Id, 0, eecp + 0x2, &semaphore, 1) != OS_EOK) {
                return;
            }

            // Is it BIOS owned? First bit in second byte
            if (semaphore & 0x1) {
                // Request for my hat back :/
                // Third byte contains the OS Semaphore 
                size_t One = 0x1;
                if (IoctlDeviceEx(Controller->Base.Device->Base.Id, 1, eecp + 0x3, &One, 1) != OS_EOK) {
                    return;
                }

                // Now wait for bios to release the semaphore
                while (One++) {
                    if (IoctlDeviceEx(Controller->Base.Device->Base.Id, 0, eecp + 0x2, &semaphore, 1) != OS_EOK) {
                        return;
                    }
                    if ((semaphore & 0x1) == 0) {
                        break;
                    }
                    if (One >= 250) {
                        TRACE("EHCI: Failed to release BIOS Semaphore");
                        break;
                    }
                    thrd_sleepex(10);
                }
                One = 1;
                while (One++) {
                    if (IoctlDeviceEx(Controller->Base.Device->Base.Id, 0, eecp + 0x3, &semaphore, 1) != OS_EOK) {
                        return;
                    }
                    if ((semaphore & 0x1) == 1) {
                        break;
                    }
                    if (One >= 250) {
                        TRACE("EHCI: Failed to set OS Semaphore");
                        break;
                    }
                    thrd_sleepex(10);
                }
            }

            // Disable SMI by setting all lower 16 bits to 0 of EECP+4
            if (IoctlDeviceEx(Controller->Base.Device->Base.Id, 1, eecp + 0x4, &Zero, 2) != OS_EOK) {
                return;
            }
        }
    }
}

oserr_t
EhciHalt(
    _In_ EhciController_t* Controller)
{
    reg32_t TemporaryValue = 0;
    int     Fault          = 0;

    // Debug
    TRACE("EhciHalt()");

    // Try to stop the scheduler
    TemporaryValue = READ_VOLATILE(Controller->OpRegisters->UsbCommand);
    TemporaryValue &= ~(EHCI_COMMAND_PERIODIC_ENABLE | EHCI_COMMAND_ASYNC_ENABLE);
    WRITE_VOLATILE(Controller->OpRegisters->UsbCommand, TemporaryValue);

    // Wait for the active-bits to clear
    WaitForConditionWithFault(Fault, (READ_VOLATILE(Controller->OpRegisters->UsbStatus) & 0xC000) == 0, 250, 10);
    if (Fault) {
        ERROR("EHCI-Failure: Failed to stop scheduler, Command Register: 0x%x - Status: 0x%x",
            Controller->OpRegisters->UsbCommand, Controller->OpRegisters->UsbStatus);
    }

    // Clear remaining interrupts
    WRITE_VOLATILE(Controller->OpRegisters->UsbIntr, 0);
    WRITE_VOLATILE(Controller->OpRegisters->UsbStatus, 0x3F);

    // Now stop the controller, this should succeed
    TemporaryValue = READ_VOLATILE(Controller->OpRegisters->UsbCommand);
    TemporaryValue &= ~(EHCI_COMMAND_RUN);
    WRITE_VOLATILE(Controller->OpRegisters->UsbCommand, TemporaryValue);

    // Wait for the active-bit to clear
    Fault = 0;
    WaitForConditionWithFault(Fault, (READ_VOLATILE(Controller->OpRegisters->UsbStatus) & EHCI_STATUS_HALTED) != 0, 250, 10);
    if (Fault) {
        ERROR("EHCI-Failure: Failed to stop controller, Command Register: 0x%x - Status: 0x%x",
            Controller->OpRegisters->UsbCommand, Controller->OpRegisters->UsbStatus);
            return OS_EUNKNOWN;
    }
    else {
        return OS_EOK;
    }
}

void
EhciSilence(
    _In_ EhciController_t* Controller)
{
    // Debug
    TRACE("EhciSilence()");

    // Halt controller and mark it unconfigured, it won't be used
    // when the configure flag is 0
    EhciHalt(Controller);
    WRITE_VOLATILE(Controller->OpRegisters->ConfigFlag, 0);
}

oserr_t
EhciReset(
    _In_ EhciController_t* Controller)
{
    reg32_t TemporaryValue = 0;
    int     Fault          = 0;

    // Debug
    TRACE("EhciReset()");

    // Reset controller
    TemporaryValue = READ_VOLATILE(Controller->OpRegisters->UsbCommand);
    TemporaryValue |= EHCI_COMMAND_HCRESET;
    WRITE_VOLATILE(Controller->OpRegisters->UsbCommand, TemporaryValue);

    // Wait for signal to deassert
    WaitForConditionWithFault(Fault, (READ_VOLATILE(Controller->OpRegisters->UsbCommand) & EHCI_COMMAND_HCRESET) == 0, 250, 10);
    if (Fault) {
        ERROR("EHCI-Failure: Reset signal won't deassert, waiting one last long wait",
            Controller->OpRegisters->UsbCommand, Controller->OpRegisters->UsbStatus);
        thrd_sleepex(250);
        return ((READ_VOLATILE(Controller->OpRegisters->UsbCommand) & EHCI_COMMAND_HCRESET) == 0) ? OS_EOK : OS_EUNKNOWN;
    }
    else {
        return OS_EOK;
    }
}

oserr_t
EhciRestart(
    _In_ EhciController_t* Controller)
{
    uintptr_t AsyncListHead  = 0;
    reg32_t   TemporaryValue = 0;

    // Debug
    TRACE("EhciRestart()");

    // Stop controller, unschedule everything
    // and then reset it.
    if (EhciHalt(Controller) != OS_EOK ||
        EhciReset(Controller) != OS_EOK) {
        ERROR("Failed to halt or reset controller");
        return OS_EUNKNOWN;
    }

    // Reset certain indices
    if (Controller->CParameters & EHCI_CPARAM_64BIT) {
        WRITE_VOLATILE(Controller->OpRegisters->SegmentSelector, 0);
#ifdef __OSCONFIG_EHCI_ALLOW_64BIT
        WRITE_VOLATILE(Controller->OpRegisters->SegmentSelector,
            HIDWORD(Controller->Base.Scheduler->Settings.FrameListPhysical));
#endif
    }
    WRITE_VOLATILE(Controller->OpRegisters->FrameIndex, 0);
    WRITE_VOLATILE(Controller->OpRegisters->UsbIntr,    0);
    WRITE_VOLATILE(Controller->OpRegisters->UsbStatus,  Controller->OpRegisters->UsbStatus);

    // Update the hardware registers to point to the newly allocated addresses
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, EHCI_QH_POOL, EHCI_QH_ASYNC, NULL, &AsyncListHead);
    WRITE_VOLATILE(Controller->OpRegisters->PeriodicListAddress, 
        LODWORD(Controller->Base.Scheduler->Settings.FrameListPhysical));
    WRITE_VOLATILE(Controller->OpRegisters->AsyncListAddress, 
        LODWORD(AsyncListHead) | EHCI_LINK_QH);

    // Next step is to build the command configuring the controller
    // Set irq latency to 0, enable per-port changes, async park.
    TemporaryValue = EHCI_COMMAND_INTR_THRESHOLD(8);
    if (Controller->CParameters & (EHCI_CPARAM_VARIABLEFRAMELIST | EHCI_CPARAM_32FRAME_SUPPORT)) {
        if (Controller->CParameters & EHCI_CPARAM_32FRAME_SUPPORT) {
            TemporaryValue |= EHCI_COMMAND_LISTSIZE(EHCI_LISTSIZE_32);
        }
        else {
            TemporaryValue |= EHCI_COMMAND_LISTSIZE(EHCI_LISTSIZE_256);
        }
    }

    if (Controller->CParameters & EHCI_CPARAM_ASYNCPARK) {
        TemporaryValue |= EHCI_COMMAND_ASYNC_PARKMODE;
        TemporaryValue |= EHCI_COMMAND_PARK_COUNT(3);
    }

    // Supported with controllers 1.1
    if (Controller->CParameters & EHCI_CPARAM_PERPORT_CHANGE) {
        TemporaryValue |= EHCI_COMMAND_PERPORT_ENABLE;
    }
    if (Controller->CParameters & EHCI_CPARAM_HWPREFETCH) {
        TemporaryValue |= EHCI_COMMAND_PERIOD_PREFECTCH;
        TemporaryValue |= EHCI_COMMAND_ASYNC_PREFETCH;
        TemporaryValue |= EHCI_COMMAND_FULL_PREFETCH;
    }

    // Start the controller by enabling it
    TemporaryValue |= EHCI_COMMAND_RUN;
    WRITE_VOLATILE(Controller->OpRegisters->UsbCommand, TemporaryValue);

    // Mark as configured, this will enable the controller
    WRITE_VOLATILE(Controller->OpRegisters->UsbIntr, (EHCI_INTR_PROCESS | EHCI_INTR_PROCESSERROR
        | EHCI_INTR_PORTCHANGE | EHCI_INTR_HOSTERROR | EHCI_INTR_ASYNC_DOORBELL));
    WRITE_VOLATILE(Controller->OpRegisters->ConfigFlag, 1);
    return OS_EOK;
}

oserr_t
EhciWaitForCompanionControllers(
    _In_ EhciController_t* controller)
{
    UsbHcController_t* hcController;
    int                controllerCount = 0;
    int                ccStarted       = 0;
    int                ccToStart;
    int                timeout = 3000;
    TRACE("EhciWaitForCompanionControllers(controller=0x%" PRIxIN ")", controller);

    hcController = (UsbHcController_t*)malloc(sizeof(UsbHcController_t));
    if (!hcController) {
        return OS_EOOM;
    }

    ccToStart = EHCI_SPARAM_CCCOUNT(controller->SParameters);

    // Wait
    TRACE("EhciWaitForCompanionControllers waiting for %i cc's to boot", ccToStart);
    while (ccStarted < ccToStart && (timeout > 0)) {
        int updatedControllerCount = 0;
        thrd_sleepex(500);
        timeout -= 500;

        if (UsbQueryControllerCount(&updatedControllerCount) != OS_EOK) {
            WARNING("EhciWaitForCompanionControllers failed to acquire controller count");
            break;
        }

        // Check for new data?
        if (updatedControllerCount != controllerCount) {
            for (int checkCount = controllerCount; checkCount < updatedControllerCount; checkCount++) {
                if (UsbQueryController(updatedControllerCount - 1, hcController)) {
                    WARNING("EhciWaitForCompanionControllers failed to query the new controller");
                    break;
                }

                // Does controller belong to our bus?
                if (hcController->Device.Bus == controller->Base.Device->Bus
                    && hcController->Device.Slot == controller->Base.Device->Slot
                    && (hcController->Type == UsbUHCI || hcController->Type == UsbOHCI)) {
                    ccStarted++;
                }
            }
            controllerCount = updatedControllerCount;
        }
    }
    free(hcController);
    return (timeout != 0) ? OS_EOK : OS_EUNKNOWN;
}

oserr_t
EhciSetup(
    _In_ EhciController_t* Controller)
{
    size_t i = 0;

    // Debug
    TRACE("EhciSetup()");

    // Disable legacy support in controller
    EhciDisableLegacySupport(Controller);

    // Are we configured to disable ehci?
#ifdef __OSCONFIG_DISABLE_EHCI
    _CRT_UNUSED(i);
    EhciSilence(Controller);
#else
    // Save some read-only but often accessed information
    Controller->SParameters    = Controller->CapRegisters->SParams;
    Controller->CParameters    = Controller->CapRegisters->CParams;
    Controller->Base.PortCount = EHCI_SPARAM_PORTCOUNT(Controller->SParameters);

    // We then stop the controller, reset it and 
    // initialize data-structures
    EhciQueueInitialize(Controller);
    EhciRestart(Controller);
    EhciWaitForCompanionControllers(Controller);    
    
    // Register the controller before starting
    if (UsbManagerRegisterController(&Controller->Base) != OS_EOK) {
        ERROR(" > failed to register ehci controller with the system.");
    }

    // Now, controller is up and running
    // and we should start doing port setups by first powering on
    TRACE(" > Powering up ports");
    for (i = 0; i < Controller->Base.PortCount; i++) {
        EhciPortSetBits(Controller, i, EHCI_PORT_POWER);
    }

    // Wait 20 ms for power to stabilize
    thrd_sleepex(20);

    // Last step is to enumerate all ports that are connected with low-speed
    // devices and release them to companion hc's for bandwidth.
    TRACE(" > Initializing ports");
    for (i = 0; i < Controller->Base.PortCount; i++) {
        reg32_t PortStatus = READ_VOLATILE(Controller->OpRegisters->Ports[i]);
        if (PortStatus & EHCI_PORT_CONNECTED) {
            // Is the port destined for other controllers?
            // Port must be in K-State + low-speed
            if (EHCI_PORT_LINESTATUS(PortStatus) == EHCI_LINESTATUS_RELEASE) {
                if (EHCI_SPARAM_CCCOUNT(Controller->SParameters) != 0) {
                    if (Controller->SParameters & EHCI_SPARAM_PORTINDICATORS) {
                        PortStatus |= EHCI_PORT_COLOR_GREEN;
                    }
                    PortStatus |= EHCI_PORT_COMPANION_HC;
                    WRITE_VOLATILE(Controller->OpRegisters->Ports[i], PortStatus);
                }
            }
            else {
                if (Controller->SParameters & EHCI_SPARAM_PORTINDICATORS) {
                    PortStatus |= EHCI_PORT_COLOR_AMBER;
                    WRITE_VOLATILE(Controller->OpRegisters->Ports[i], PortStatus);
                }
                UsbEventPort(Controller->Base.Device->Base.Id, (uint8_t)(i & 0xFF));
            }
        }
    }
#endif
    return OS_EOK;
}
