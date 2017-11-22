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
 * MollenOS MCore - Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 * - Isochronous Transport
 * - Transaction Translator Support
 */
#define __TRACE

/* Includes
 * - System */
#include <os/driver/device.h>
#include <os/thread.h>
#include <os/utils.h>
#include "ehci.h"

/* Includes
 * - Library */
#include <stdlib.h>
#include <string.h>

/* Prototypes 
 * This is to keep the create/destroy at the top of the source file */
OsStatus_t
EhciSetup(
    _In_ EhciController_t *Controller);

/* Externs
 * We need access to the interrupt-handlers in main.c */
__EXTERN
InterruptStatus_t
OnFastInterrupt(
    _In_Opt_ void *InterruptData);

/* EhciControllerCreate 
 * Initializes and creates a new Ehci Controller instance
 * from a given new system device on the bus. */
EhciController_t*
EhciControllerCreate(
    _In_ MCoreDevice_t *Device)
{
    // Variables
    EhciController_t *Controller = NULL;
    DeviceIoSpace_t *IoBase = NULL;
    int i;

    // Allocate a new instance of the controller
    Controller = (EhciController_t*)malloc(sizeof(EhciController_t));
    memset(Controller, 0, sizeof(EhciController_t));
    memcpy(&Controller->Base.Device, Device, Device->Length);

    // Fill in some basic stuff needed for init
    Controller->Base.Contract.DeviceId = Controller->Base.Device.Id;
    Controller->Base.Type = UsbEHCI;
    SpinlockReset(&Controller->Base.Lock);

    // Get I/O Base, and for EHCI it'll be the first address we encounter
    // of type MMIO
    for (i = 0; i < __DEVICEMANAGER_MAX_IOSPACES; i++) {
        if (Controller->Base.Device.IoSpaces[i].Size != 0
            && Controller->Base.Device.IoSpaces[i].Type == IO_SPACE_MMIO) {
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
    if (CreateIoSpace(IoBase) != OsSuccess
        || AcquireIoSpace(IoBase) != OsSuccess) {
        ERROR("Failed to create and acquire the io-space for ehci-controller");
        free(Controller);
        return NULL;
    }
    else {
        // Store information
        Controller->Base.IoBase = IoBase;
    }

    // Start out by initializing the contract
    InitializeContract(&Controller->Base.Contract, Controller->Base.Contract.DeviceId, 1,
        ContractController, "EHCI Controller Interface");

    // Trace
    TRACE("Io-Space was assigned virtual address 0x%x", IoBase->VirtualBase);

    // Instantiate the register-access
    Controller->CapRegisters = (EchiCapabilityRegisters_t*)IoBase->VirtualBase;
    Controller->OpRegisters = (EchiOperationalRegisters_t*)
        (IoBase->VirtualBase + Controller->CapRegisters->Length);

    // Initialize the interrupt settings
    Controller->Base.Device.Interrupt.FastHandler = OnFastInterrupt;
    Controller->Base.Device.Interrupt.Data = Controller;

    // Register contract before interrupt
    if (RegisterContract(&Controller->Base.Contract) != OsSuccess) {
        ERROR("Failed to register contract for ehci-controller");
        ReleaseIoSpace(Controller->Base.IoBase);
        DestroyIoSpace(Controller->Base.IoBase->Id);
        free(Controller);
        return NULL;
    }

    // Register interrupt
    Controller->Base.Interrupt = RegisterInterruptSource(
        &Controller->Base.Device.Interrupt, INTERRUPT_USERSPACE);

    // Enable device
    if (IoctlDevice(Controller->Base.Device.Id, __DEVICEMANAGER_IOCTL_BUS,
        (__DEVICEMANAGER_IOCTL_ENABLE | __DEVICEMANAGER_IOCTL_MMIO_ENABLE
            | __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE)) != OsSuccess) {
        ERROR("Failed to enable the ehci-controller");
        UnregisterInterruptSource(Controller->Base.Interrupt);
        ReleaseIoSpace(Controller->Base.IoBase);
        DestroyIoSpace(Controller->Base.IoBase->Id);
        free(Controller);
        return NULL;
    }

    // Allocate a list of endpoints
    Controller->Base.Endpoints = CollectionCreate(KeyInteger);

    // Now that all formalities has been taken care
    // off we can actually setup controller
    if (UsbManagerCreateController(&Controller->Base) == OsSuccess
        && EhciSetup(Controller) == OsSuccess) {
        return Controller;
    }
    else {
        EhciControllerDestroy(Controller);
        return NULL;
    }
}

/* EhciControllerDestroy
 * Destroys an existing controller instance and cleans up
 * any resources related to it */
OsStatus_t
EhciControllerDestroy(
    _In_ EhciController_t *Controller)
{
    // Unregister, then destroy
    UsbManagerDestroyController(&Controller->Base);

    // Cleanup scheduler
    EhciQueueDestroy(Controller);

    // Unregister the interrupt
    UnregisterInterruptSource(Controller->Base.Interrupt);

    // Release the io-space
    ReleaseIoSpace(Controller->Base.IoBase);
    DestroyIoSpace(Controller->Base.IoBase->Id);

    // Free the list of endpoints
    CollectionDestroy(Controller->Base.Endpoints);

    // Free the controller structure
    free(Controller);

    // Cleanup done
    return OsSuccess;
}

/* EhciDisableLegacySupport
 * Disables legacy-support, by doing this PS/2 emulation and any of the
 * kind will stop working. Must be done before using the ehci-controller. */
void
EhciDisableLegacySupport(
    _In_ EhciController_t *Controller)
{
    // Variables
    reg32_t Eecp = 0;

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
    Eecp = EHCI_CPARAM_EECP(Controller->CapRegisters->CParams);

    // If the eecp is valid ( >= 0x40), then there are a few
    // cases we can handle, but if its not valid, there is no legacy
    if (Eecp >= 0x40) {

        // Variables
        Flags_t Semaphore = 0;
        Flags_t CapId = 0;
        Flags_t NextEecp = 0;
        int Run = 1;

        // Get the extended capability register
        // We read the second byte, because it contains
        // the BIOS Semaphore
        while (Run) {
            // Retrieve capability id
            if (IoctlDeviceEx(Controller->Base.Device.Id, 0, Eecp, &CapId, 1) != OsSuccess) {
                return;
            }

            // Legacy support?
            if (CapId == 0x01) {
                break;
            }

            // Nope, follow eecp link
            if (IoctlDeviceEx(Controller->Base.Device.Id, 0, Eecp + 0x1, &NextEecp, 1) != OsSuccess) {
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
            // Variables
            Flags_t Zero = 0;
            if (IoctlDeviceEx(Controller->Base.Device.Id, 0, Eecp + 0x2, &Semaphore, 1) != OsSuccess) {
                return;
            }

            // Is it BIOS owned? First bit in second byte
            if (Semaphore & 0x1) {
                // Request for my hat back :/
                // Third byte contains the OS Semaphore 
                Flags_t One = 0x1;
                if (IoctlDeviceEx(Controller->Base.Device.Id, 1, Eecp + 0x3, &One, 1) != OsSuccess) {
                    return;
                }

                // Now wait for bios to release the semaphore
                while (One++) {
                    if (IoctlDeviceEx(Controller->Base.Device.Id, 0, Eecp + 0x2, &Semaphore, 1) != OsSuccess) {
                        return;
                    }
                    if ((Semaphore & 0x1) == 0) {
                        break;
                    }
                    if (One >= 250) {
                        TRACE("EHCI: Failed to release BIOS Semaphore");
                        break;
                    }
                    ThreadSleep(10);
                }
                One = 1;
                while (One++) {
                    if (IoctlDeviceEx(Controller->Base.Device.Id, 0, Eecp + 0x3, &Semaphore, 1) != OsSuccess) {
                        return;
                    }
                    if ((Semaphore & 0x1) == 1) {
                        break;
                    }
                    if (One >= 250) {
                        TRACE("EHCI: Failed to set OS Semaphore");
                        break;
                    }
                    ThreadSleep(10);
                }
            }

            // Disable SMI by setting all lower 16 bits to 0 of EECP+4
            if (IoctlDeviceEx(Controller->Base.Device.Id, 1, Eecp + 0x4, &Zero, 2) != OsSuccess) {
                return;
            }
        }
    }
}

/* EhciHalt
 * Halt's the controller and clears any pending events. */
OsStatus_t
EhciHalt(
    _In_ EhciController_t *Controller)
{
    // Variables
    reg32_t TemporaryValue  = 0;
    int Fault               = 0;

    // Debug
    TRACE("EhciHalt()");

    // Try to stop the scheduler
    TemporaryValue = Controller->OpRegisters->UsbCommand;
    TemporaryValue &= ~(EHCI_COMMAND_PERIODIC_ENABLE | EHCI_COMMAND_ASYNC_ENABLE);
    Controller->OpRegisters->UsbCommand = TemporaryValue;

    // Wait for the active-bits to clear
    WaitForConditionWithFault(Fault, 
        (Controller->OpRegisters->UsbStatus & 0xC000) == 0, 250, 10);

    // Did we stop the scheduler?
    if (Fault) {
        ERROR("EHCI-Failure: Failed to stop scheduler, Command Register: 0x%x - Status: 0x%x",
            Controller->OpRegisters->UsbCommand, Controller->OpRegisters->UsbStatus);
    }

    // Clear remaining interrupts
    Controller->OpRegisters->UsbIntr = 0;
    Controller->OpRegisters->UsbStatus = (0x3F);

    // Now stop the controller, this should succeed
    TemporaryValue = Controller->OpRegisters->UsbCommand;
    TemporaryValue &= ~(EHCI_COMMAND_RUN);
    Controller->OpRegisters->UsbCommand = TemporaryValue;

    // Wait for the active-bit to clear
    Fault = 0;
    WaitForConditionWithFault(Fault, 
        (Controller->OpRegisters->UsbStatus & EHCI_STATUS_HALTED) != 0, 250, 10);

    if (Fault) {
        ERROR("EHCI-Failure: Failed to stop controller, Command Register: 0x%x - Status: 0x%x",
            Controller->OpRegisters->UsbCommand, Controller->OpRegisters->UsbStatus);
            return OsError;
    }
    else {
        return OsSuccess;
    }
}

/* EhciSilence
 * Silences the controller by halting it and marking it unconfigured. */
void
EhciSilence(
    _In_ EhciController_t *Controller)
{
    // Debug
    TRACE("EhciSilence()");

    // Halt controller and mark it unconfigured, it won't be used
    // when the configure flag is 0
    EhciHalt(Controller);
    Controller->OpRegisters->ConfigFlag = 0;
}

/* EhciReset
 * Resets the controller from any state, leaves it post-reset state. */
OsStatus_t
EhciReset(
    _In_ EhciController_t *Controller)
{
    // Variables
    reg32_t TemporaryValue  = 0;
    int Fault               = 0;

    // Debug
    TRACE("EhciReset()");

    // Reset controller
    TemporaryValue = Controller->OpRegisters->UsbCommand;
    TemporaryValue |= EHCI_COMMAND_HCRESET;
    Controller->OpRegisters->UsbCommand = TemporaryValue;

    // Wait for signal to deassert
    WaitForConditionWithFault(Fault, 
        (Controller->OpRegisters->UsbCommand & EHCI_COMMAND_HCRESET) == 0, 250, 10);

    // Handle result
    if (Fault) {
        ERROR("EHCI-Failure: Reset signal won't deassert, waiting one last long wait",
            Controller->OpRegisters->UsbCommand, Controller->OpRegisters->UsbStatus);
        ThreadSleep(250);
        return ((Controller->OpRegisters->UsbCommand & EHCI_COMMAND_HCRESET) == 0) ? OsSuccess : OsError;
    }
    else {
        return OsSuccess;
    }
}

/* EhciRestart
 * Resets and restarts the entire controller and schedule, this can be used in
 * case of serious failures. */
OsStatus_t
EhciRestart(
    _In_ EhciController_t *Controller)
{
    // Variables
    reg32_t TemporaryValue                          = 0;

    // Debug
    TRACE("EhciRestart()");

    // Stop controller, unschedule everything
    // and then reset it.
    EhciHalt(Controller);
    EhciReset(Controller);

    // Reset certain indexes
    Controller->OpRegisters->SegmentSelector        = 0;
    Controller->OpRegisters->FrameIndex             = 0;

    // Enable desired interrupts and clear status
    Controller->OpRegisters->UsbIntr                = (EHCI_INTR_PROCESS | EHCI_INTR_PROCESSERROR
        | EHCI_INTR_PORTCHANGE | EHCI_INTR_HOSTERROR | EHCI_INTR_ASYNC_DOORBELL);
    Controller->OpRegisters->UsbStatus              = Controller->OpRegisters->UsbIntr;

    // Update the hardware registers to point to the newly allocated
	// addresses
	Controller->OpRegisters->PeriodicListAddress    = (reg32_t)Controller->QueueControl.FrameListPhysical;
	Controller->OpRegisters->AsyncListAddress       = (reg32_t)EHCI_POOL_QHINDEX(Controller, EHCI_POOL_QH_ASYNC) | EHCI_LINK_QH;

    // Next step is to build the command configuring the controller
    // Set irq latency to 0, enable per-port changes, async park
    // and if variable frame-list, set it to 256.
    TemporaryValue = EHCI_COMMAND_INTR_THRESHOLD(8);
    if (Controller->CParameters & EHCI_CPARAM_VARIABLEFRAMELIST) {
        TemporaryValue |= EHCI_COMMAND_LISTSIZE(EHCI_LISTSIZE_256);
    }
    if (Controller->CParameters & EHCI_CPARAM_ASYNCPARK) {
        TemporaryValue |= EHCI_COMMAND_ASYNC_PARKMODE;
        TemporaryValue |= EHCI_COMMAND_PARK_COUNT(3);
    }

    if (Controller->CapRegisters->Version == EHCI_VERSION_11) {
        if (Controller->CParameters & EHCI_CPARAM_PERPORT_CHANGE) {
            TemporaryValue |= EHCI_COMMAND_PERPORT_ENABLE;
        }
        if (Controller->CParameters & EHCI_CPARAM_HWPREFECT) {
            TemporaryValue |= EHCI_COMMAND_PERIOD_PREFECTCH;
            TemporaryValue |= EHCI_COMMAND_ASYNC_PREFETCH;
            TemporaryValue |= EHCI_COMMAND_FULL_PREFETCH;
        }
    }

    // Start the controller by enabling it
    TemporaryValue |= EHCI_COMMAND_RUN;
    Controller->OpRegisters->UsbCommand = TemporaryValue;

    // Mark as configured, this will enable the controller
    Controller->OpRegisters->ConfigFlag = 1;
    return OsSuccess;
}

/* EhciSetup
 * Initializes the ehci-controller and boots it up into runnable state. */
OsStatus_t
EhciSetup(
    _In_ EhciController_t *Controller)
{
    // Variables
    int Timeout                         = 3000;
    UsbHcController_t *HcController     = NULL;
    reg32_t TemporaryValue              = 0;
    size_t i                            = 0;
    int ControllerCount                 = 0;
    int CcStarted                       = 0;
    int CcToStart                       = 0;

    // Debug
    TRACE("EhciSetup()");

    // Disable legacy support in controller
    EhciDisableLegacySupport(Controller);

    // Are we configured to disable ehci?
#ifdef __OSCONFIG_DISABLE_EHCI
    _CRT_UNUSED(TemporaryValue);
    _CRT_UNUSED(i);
    EhciSilence(Controller);
#else
    // Save some read-only but often accessed information
    Controller->Base.PortCount  = EHCI_SPARAM_PORTCOUNT(Controller->CapRegisters->SParams);
    Controller->SParameters = Controller->CapRegisters->SParams;
    Controller->CParameters = Controller->CapRegisters->CParams;

    // Wait for all companion controllers to startup before initializing us
    HcController = (UsbHcController_t*)malloc(sizeof(UsbHcController_t));
    CcToStart = EHCI_SPARAM_CCCOUNT(Controller->SParameters);
    TRACE("Waiting for %i cc's to boot", CcToStart);
    while (CcStarted < CcToStart && (Timeout > 0)) {
        int UpdatedControllerCount = 0;
        ThreadSleep(500);
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

    // We then stop the controller, reset it and 
    // initialize data-structures
    EhciQueueInitialize(Controller);
    EhciRestart(Controller);
    
    // Now, controller is up and running
    // and we should start doing port setups by first powering on
    TRACE("Powering up ports");
    if (Controller->SParameters & EHCI_SPARAM_PPC) {
        for (i = 0; i < Controller->Base.PortCount; i++) {
            TemporaryValue = Controller->OpRegisters->Ports[i];
            TemporaryValue |= EHCI_PORT_POWER;
            Controller->OpRegisters->Ports[i] = TemporaryValue;
        }
    }

    // Wait 20 ms for power to stabilize
    ThreadSleep(20);

    // Last step is to enumerate all ports that are connected with low-speed
    // devices and release them to companion hc's for bandwidth.
    TRACE("Initializing ports");
    for (i = 0; i < Controller->Base.PortCount; i++) {
        if (Controller->OpRegisters->Ports[i] & EHCI_PORT_CONNECTED) {
            // Is the port destined for other controllers?
            // Port must be in K-State + low-speed
            if (EHCI_PORT_LINESTATUS(Controller->OpRegisters->Ports[i]) == EHCI_LINESTATUS_RELEASE) {
                if (CcToStart != 0) {
                    if (Controller->CapRegisters->SParams & EHCI_SPARAM_PORTINDICATORS) {
                        Controller->OpRegisters->Ports[i] |= EHCI_PORT_COLOR_GREEN;
                    }
                    Controller->OpRegisters->Ports[i] |= EHCI_PORT_COMPANION_HC;
                }
            }
            else {
                if (Controller->CapRegisters->SParams & EHCI_SPARAM_PORTINDICATORS) {
                    Controller->OpRegisters->Ports[i] |= EHCI_PORT_COLOR_AMBER;
                }
            }
        }
    }
#endif
    return OsSuccess;
}
