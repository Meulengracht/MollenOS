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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 */
//#define __TRACE

#include <ddk/interrupt.h>
#include <ddk/utils.h>
#include "../common/hci.h"
#include "ehci.h"
#include <threads.h>
#include <stdlib.h>
#include <string.h>

/* Prototypes 
 * This is to keep the create/destroy at the top of the source file */
OsStatus_t        EhciSetup(EhciController_t *Controller);
InterruptStatus_t OnFastInterrupt(InterruptFunctionTable_t*, InterruptResourceTable_t*);

void GetModuleIdentifiers(unsigned int* vendorId, unsigned int* deviceId,
    unsigned int* class, unsigned int* subClass)
{
    *vendorId = 0;
    *deviceId = 0;
    *class    = 0xC0003;
    *subClass = 0x200000;
}

UsbManagerController_t*
HciControllerCreate(
    _In_ BusDevice_t* Device)
{
    EhciController_t* Controller;
    DeviceInterrupt_t Interrupt;
    DeviceIo_t*       IoBase = NULL;
    int i;

    // Allocate a new instance of the controller
    Controller = (EhciController_t*)malloc(sizeof(EhciController_t));
    if (!Controller) {
        return NULL;
    }
    
    memset(Controller, 0, sizeof(EhciController_t));
    memcpy(&Controller->Base.Device, Device, Device->Base.Length);

    // Fill in some basic stuff needed for init
    Controller->Base.Type               = UsbEHCI;
    Controller->Base.TransactionList    = CollectionCreate(KeyInteger);
    Controller->Base.Endpoints          = CollectionCreate(KeyInteger);
    spinlock_init(&Controller->Base.Lock, spinlock_plain);

    // Get I/O Base, and for EHCI it'll be the first address we encounter
    // of type MMIO
    for (i = 0; i < __DEVICEMANAGER_MAX_IOSPACES; i++) {
        if (Controller->Base.Device.IoSpaces[i].Type == DeviceIoMemoryBased) {
            IoBase = &Controller->Base.Device.IoSpaces[i];
            break;
        }
    }

    // Sanitize that we found the io-space
    if (IoBase == NULL) {
        ERROR("No memory space found for ehci-controller");
        free(Controller);
        return NULL;
    }

    // Trace
    TRACE("Found Io-Space (Type %u, Physical 0x%x, Size 0x%x)",
        IoBase->Type, IoBase->PhysicalBase, IoBase->Size);

    // Acquire the io-space
    if (AcquireDeviceIo(IoBase) != OsSuccess) {
        ERROR("Failed to create and acquire the io-space for ehci-controller");
        free(Controller);
        return NULL;
    }
    else {
        // Store information
        Controller->Base.IoBase = IoBase;
    }

    // Trace
    TRACE("Io-Space was assigned virtual address 0x%x", IoBase->Access.Memory.VirtualBase);

    // Instantiate the register-access
    Controller->CapRegisters = (EchiCapabilityRegisters_t*)IoBase->Access.Memory.VirtualBase;
    Controller->OpRegisters  = (EchiOperationalRegisters_t*)
        (IoBase->Access.Memory.VirtualBase + Controller->CapRegisters->Length);

    // Initialize the interrupt settings
    DeviceInterruptInitialize(&Interrupt, Device);
    RegisterFastInterruptHandler(&Interrupt, (InterruptHandler_t)OnFastInterrupt);
    RegisterFastInterruptIoResource(&Interrupt, IoBase);
    RegisterFastInterruptMemoryResource(&Interrupt, (uintptr_t)Controller, sizeof(EhciController_t), 0);
    
    // Register interrupt
    RegisterInterruptEventDescriptor(&Interrupt, Controller);
    Controller->Base.Interrupt  = RegisterInterruptSource(
        &Interrupt, INTERRUPT_USERSPACE);

    // Enable device
    if (IoctlDevice(Controller->Base.Device.Base.Id, __DEVICEMANAGER_IOCTL_BUS,
        (__DEVICEMANAGER_IOCTL_ENABLE | __DEVICEMANAGER_IOCTL_MMIO_ENABLE
            | __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE)) != OsSuccess) {
        ERROR("Failed to enable the ehci-controller");
        UnregisterInterruptSource(Controller->Base.Interrupt);
        ReleaseDeviceIo(Controller->Base.IoBase);
        free(Controller);
        return NULL;
    }

    // Now that all formalities has been taken care
    // off we can actually setup controller
    if (EhciSetup(Controller) == OsSuccess) {
        return &Controller->Base;
    }
    else {
        HciControllerDestroy(&Controller->Base);
        return NULL;
    }
}

/* HciControllerDestroy
 * Destroys an existing controller instance and cleans up
 * any resources related to it */
OsStatus_t
HciControllerDestroy(
    _In_ UsbManagerController_t*    Controller)
{
    // Unregister, then destroy
    UsbManagerDestroyController(Controller);

    // Cleanup scheduler
    EhciQueueDestroy((EhciController_t*)Controller);

    // Unregister the interrupt
    UnregisterInterruptSource(Controller->Interrupt);

    // Release the io-space
    ReleaseDeviceIo(Controller->IoBase);

    // Free the list of endpoints
    CollectionDestroy(Controller->TransactionList);
    CollectionDestroy(Controller->Endpoints);
    free(Controller);
    return OsSuccess;
}

void
EhciDisableLegacySupport(
    _In_ EhciController_t* Controller)
{
    reg32_t Cparams = READ_VOLATILE(Controller->CapRegisters->CParams);
    reg32_t Eecp    = 0;

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
    Eecp = EHCI_CPARAM_EECP(Cparams);

    // If the eecp is valid ( >= 0x40), then there are a few
    // cases we can handle, but if its not valid, there is no legacy
    if (Eecp >= 0x40) {
        size_t Semaphore = 0;
        size_t CapId     = 0;
        size_t NextEecp  = 0;
        int Run          = 1;

        // Get the extended capability register
        // We read the second byte, because it contains the BIOS Semaphore
        while (Run) {
            // Retrieve capability id
            if (IoctlDeviceEx(Controller->Base.Device.Base.Id, 0, Eecp, &CapId, 1) != OsSuccess) {
                return;
            }

            // Legacy support?
            if (CapId == 0x01) {
                break;
            }

            // Nope, follow eecp link
            if (IoctlDeviceEx(Controller->Base.Device.Base.Id, 0, Eecp + 0x1, &NextEecp, 1) != OsSuccess) {
                return;
            }

            // Sanitize end of link
            if (NextEecp == 0x00) {
                break;
            }
            else {
                Eecp = NextEecp;
            }
        }

        // Only continue if Id == 0x01
        if (CapId == 0x01) {
            size_t Zero = 0;
            if (IoctlDeviceEx(Controller->Base.Device.Base.Id, 0, Eecp + 0x2, &Semaphore, 1) != OsSuccess) {
                return;
            }

            // Is it BIOS owned? First bit in second byte
            if (Semaphore & 0x1) {
                // Request for my hat back :/
                // Third byte contains the OS Semaphore 
                size_t One = 0x1;
                if (IoctlDeviceEx(Controller->Base.Device.Base.Id, 1, Eecp + 0x3, &One, 1) != OsSuccess) {
                    return;
                }

                // Now wait for bios to release the semaphore
                while (One++) {
                    if (IoctlDeviceEx(Controller->Base.Device.Base.Id, 0, Eecp + 0x2, &Semaphore, 1) != OsSuccess) {
                        return;
                    }
                    if ((Semaphore & 0x1) == 0) {
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
                    if (IoctlDeviceEx(Controller->Base.Device.Base.Id, 0, Eecp + 0x3, &Semaphore, 1) != OsSuccess) {
                        return;
                    }
                    if ((Semaphore & 0x1) == 1) {
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
            if (IoctlDeviceEx(Controller->Base.Device.Base.Id, 1, Eecp + 0x4, &Zero, 2) != OsSuccess) {
                return;
            }
        }
    }
}

OsStatus_t
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
            return OsError;
    }
    else {
        return OsSuccess;
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

OsStatus_t
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
        return ((READ_VOLATILE(Controller->OpRegisters->UsbCommand) & EHCI_COMMAND_HCRESET) == 0) ? OsSuccess : OsError;
    }
    else {
        return OsSuccess;
    }
}

OsStatus_t
EhciRestart(
    _In_ EhciController_t* Controller)
{
    uintptr_t AsyncListHead  = 0;
    reg32_t   TemporaryValue = 0;

    // Debug
    TRACE("EhciRestart()");

    // Stop controller, unschedule everything
    // and then reset it.
    if (EhciHalt(Controller)  != OsSuccess || 
        EhciReset(Controller) != OsSuccess) {
        ERROR("Failed to halt or reset controller");
        return OsError;
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
    return OsSuccess;
}

OsStatus_t
EhciWaitForCompanionControllers(
    _In_ EhciController_t* Controller)
{
    UsbHcController_t* HcController;
    int                ControllerCount = 0;
    int                CcStarted       = 0;
    int                CcToStart       = 0;
    int                Timeout         = 3000;

    HcController = (UsbHcController_t*)malloc(sizeof(UsbHcController_t));
    if (!HcController) {
        return OsOutOfMemory;
    }
    
    CcToStart = EHCI_SPARAM_CCCOUNT(Controller->SParameters);

    // Wait
    TRACE("Waiting for %i cc's to boot", CcToStart);
    while (CcStarted < CcToStart && (Timeout > 0)) {
        int UpdatedControllerCount = 0;
        thrd_sleepex(500);
        Timeout -= 500;
        if (UsbQueryControllerCount(&UpdatedControllerCount) != OsSuccess) {
            WARNING("Failed to acquire controller count");
            break;
        }

        // Check for new data?
        if (UpdatedControllerCount != ControllerCount) {
            for (int CheckCount = ControllerCount; CheckCount < UpdatedControllerCount; CheckCount++) {
                if (UsbQueryController(UpdatedControllerCount - 1, HcController)) {
                    WARNING("Failed to query the new controller");
                    break;
                }
                // Does controller belong to our bus?
                if (HcController->Device.Bus == Controller->Base.Device.Bus
                    && HcController->Device.Slot == Controller->Base.Device.Slot
                    && (HcController->Type == UsbUHCI || HcController->Type == UsbOHCI)) {
                    CcStarted++;
                }
            }
            ControllerCount = UpdatedControllerCount;
        }
    }
    free(HcController);
    return (Timeout != 0) ? OsSuccess : OsError;
}

OsStatus_t
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
    if (UsbManagerRegisterController(&Controller->Base) != OsSuccess) {
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
                UsbEventPort(Controller->Base.Device.Base.Id, 0, (uint8_t)(i & 0xFF));
            }
        }
    }
#endif
    return OsSuccess;
}
