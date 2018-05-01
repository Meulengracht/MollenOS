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
#include <os/utils.h>
#include "ohci.h"

/* Includes
 * - Library */
#include <threads.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Prototypes 
 * This is to keep the create/destroy at the top of the source file */
OsStatus_t          OhciSetup(OhciController_t *Controller);
InterruptStatus_t   OnFastInterrupt(void *InterruptData);

/* HciControllerCreate 
 * Initializes and creates a new Hci Controller instance
 * from a given new system device on the bus. */
UsbManagerController_t*
HciControllerCreate(
    _In_ MCoreDevice_t*             Device)
{
    // Variables
    OhciController_t *Controller = NULL;
    DeviceIoSpace_t *IoBase = NULL;
    int i;

    // Allocate a new instance of the controller
    Controller = (OhciController_t*)malloc(sizeof(OhciController_t));
    memset(Controller, 0, sizeof(OhciController_t));
    memcpy(&Controller->Base.Device, Device, Device->Length);

    // Fill in some basic stuff needed for init
    Controller->Base.Contract.DeviceId  = Controller->Base.Device.Id;
    Controller->Base.Type               = UsbOHCI;
    Controller->Base.TransactionList    = CollectionCreate(KeyInteger);
    Controller->Base.Endpoints          = CollectionCreate(KeyInteger);
    SpinlockReset(&Controller->Base.Lock);

    // Get I/O Base, and for OHCI it'll be the first address we encounter
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
        ERROR("No memory space found for ohci-controller");
        free(Controller);
        return NULL;
    }

    // Trace
    TRACE("Found Io-Space (Type %u, Physical 0x%x, Size 0x%x)",
        IoBase->Type, IoBase->PhysicalBase, IoBase->Size);

    // Acquire the io-space
    if (CreateIoSpace(IoBase) != OsSuccess || AcquireIoSpace(IoBase) != OsSuccess) {
        ERROR("Failed to create and acquire the io-space for ohci-controller");
        free(Controller);
        return NULL;
    }
    else {
        // Store information
        Controller->Base.IoBase = IoBase;
    }

    // Start out by initializing the contract
    InitializeContract(&Controller->Base.Contract, Controller->Base.Contract.DeviceId, 1,
        ContractController, "OHCI Controller Interface");

    // Trace
    TRACE("Io-Space was assigned virtual address 0x%x", IoBase->VirtualBase);

    // Instantiate the register-access and disable interrupts on device
    Controller->Registers                           = (OhciRegisters_t*)IoBase->VirtualBase;
    Controller->Registers->HcInterruptEnable        = 0;
    Controller->Registers->HcInterruptDisable       = OHCI_MASTER_INTERRUPT;

    // Initialize the interrupt settings
    Controller->Base.Device.Interrupt.FastHandler   = OnFastInterrupt;
    Controller->Base.Device.Interrupt.Data          = Controller;

    // Register contract before interrupt
    if (RegisterContract(&Controller->Base.Contract) != OsSuccess) {
        ERROR("Failed to register contract for ohci-controller");
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
        ERROR("Failed to enable the ohci-controller");
        UnregisterInterruptSource(Controller->Base.Interrupt);
        ReleaseIoSpace(Controller->Base.IoBase);
        DestroyIoSpace(Controller->Base.IoBase->Id);
        free(Controller);
        return NULL;
    }

    // Now that all formalities has been taken care
    // off we can actually setup controller
    if (OhciSetup(Controller) == OsSuccess) {
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
    // Unregister us with service
    UsbManagerDestroyController(Controller);

    // Cleanup scheduler
    OhciQueueDestroy((OhciController_t*)Controller);

    // Free resources
    if (((OhciController_t*)Controller)->Hcca != NULL) {
        MemoryFree((void*)((OhciController_t*)Controller), 0x1000);
    }

    // Unregister the interrupt
    UnregisterInterruptSource(Controller->Interrupt);

    // Release the io-space
    ReleaseIoSpace(Controller->IoBase);
    DestroyIoSpace(Controller->IoBase->Id);

    // Free the list of endpoints
    CollectionDestroy(Controller->Endpoints);
    free(Controller);
    return OsSuccess;
}

/* OhciSetMode
 * Changes the state of the OHCI controller to the given mode */
void
OhciSetMode(
    _In_ OhciController_t*          Controller, 
    _In_ reg32_t                    Mode)
{
    // Read in value, mask it off, then update
    reg32_t Value                       = Controller->Registers->HcControl;
    
    Value                               &= ~(OHCI_CONTROL_SUSPEND);
    Value                               |= Mode;
    
    Controller->Registers->HcControl    = Value;
}

/* OhciTakeControl
 * Verifies the ownership status of the controller, and if either 
 * SMM or BIOS holds the control, we try to take it back */
OsStatus_t
OhciTakeControl(
    _In_ OhciController_t*          Controller)
{
    // Variables
    uint32_t Temp   = 0;
    int i           = 0;

    // Trace
    TRACE("OhciTakeControl()");

    // Does SMM hold control of chip? Then ask for it back
    if (Controller->Registers->HcControl & OHCI_CONTROL_IR) {
        Temp                                    = Controller->Registers->HcCommandStatus;
        Temp                                    |= OHCI_COMMAND_OWNERSHIP;
        Controller->Registers->HcCommandStatus  = Temp;

        // Now we wait for the bit to clear
        WaitForConditionWithFault(i, (Controller->Registers->HcControl & OHCI_CONTROL_IR) == 0, 250, 10);

        if (i != 0) {
            // Didn't work, we try the IR bit
            Controller->Registers->HcControl &= ~OHCI_CONTROL_IR;
            WaitForConditionWithFault(i, (Controller->Registers->HcControl & OHCI_CONTROL_IR) == 0, 250, 10);
            if (i != 0) {
                ERROR("failed to clear routing bit");
                ERROR("SMM Won't give us the Controller, we're backing down >(");
                return OsError;
            }
        }
    }
    // Did BIOS play tricks on us?
    else if ((Controller->Registers->HcControl & OHCI_CONTROL_STATE_MASK) != 0) {
        // If it's suspended, resume and wait 10 ms
        if ((Controller->Registers->HcControl & OHCI_CONTROL_STATE_MASK) != OHCI_CONTROL_ACTIVE) {
            OhciSetMode(Controller, OHCI_CONTROL_ACTIVE);
            thrd_sleepex(10);
        }
    }
    else {
        // Cold boot, wait 10 ms
        thrd_sleepex(10);
    }
    return OsSuccess;
}

/* OhciReset
 * Reinitializes the controller and returns the controller to a working
 * state, also verifies some of the registers to sane values. */
OsStatus_t
OhciReset(
    _In_ OhciController_t*          Controller)
{
    // Variables
    reg32_t Temporary   = 0;
    reg32_t FmInt       = 0;
    int i               = 0;

    // Trace
    TRACE("OhciReset()");

    // Verify HcFmInterval and store the original value
    FmInt = Controller->Registers->HcFmInterval;

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
    Controller->Registers->HcInterruptDisable = (reg32_t)OHCI_MASTER_INTERRUPT;

    // Suspend the controller just in case it's running
    // and wait for it to suspend
    OhciSetMode(Controller, OHCI_CONTROL_SUSPEND);
    thrd_sleepex(10);

    // Toggle bit 0 to initiate a reset
    Temporary                                   = Controller->Registers->HcCommandStatus;
    Temporary                                   |= OHCI_COMMAND_RESET;
    Controller->Registers->HcCommandStatus      = Temporary;

    // Wait for reboot (takes maximum of 10 ms)
    // But the world is not perfect, given it up to 50
    TRACE(" > Resetting controller");
    WaitForConditionWithFault(i, (Controller->Registers->HcCommandStatus & OHCI_COMMAND_RESET) == 0, 50, 1);

    // Sanitize the fault variable
    if (i != 0) {
        ERROR("Controller failed to reboot");
        ERROR("Reset Timeout :(");
        return OsError;
    }

    //**************************************
    // We now have 2 ms to complete setup
    // and put it in Operational Mode
    //**************************************

    // Restore the FmInt and toggle the FIT
    Controller->Registers->HcFmInterval     = FmInt;
    Controller->Registers->HcFmInterval     ^= 0x80000000;

    // Setup the Hcca Address and initiate some members of the HCCA
    Controller->Registers->HcHCCA           = Controller->HccaPhysical;
    Controller->Hcca->CurrentFrame          = 0;
    Controller->Hcca->HeadDone              = 0;

    // Setup initial ED queues
    Controller->Registers->HcControlHeadED  = Controller->Registers->HcControlCurrentED = 0;
    Controller->Registers->HcBulkHeadED     = Controller->Registers->HcBulkCurrentED    = 0;

    // Set HcEnableInterrupt to all except SOF and OC
    Controller->Registers->HcInterruptDisable = OHCI_SOF_EVENT | 
        OHCI_ROOTHUB_EVENT | OHCI_OWNERSHIP_EVENT;

    // Clear out INTR state and initialize the interrupt enable
    Controller->Registers->HcInterruptStatus = Controller->Registers->HcInterruptStatus;
    Controller->Registers->HcInterruptEnable = OHCI_OVERRUN_EVENT 
        | OHCI_PROCESS_EVENT | OHCI_RESUMEDETECT_EVENT | OHCI_FATAL_EVENT
        | OHCI_OVERFLOW_EVENT | OHCI_MASTER_INTERRUPT;

    // Set HcPeriodicStart to a value that is 90% of 
    // FrameInterval in HcFmInterval
    Temporary                               = (FmInt & OHCI_FMINTERVAL_FIMASK);
    Controller->Registers->HcPeriodicStart  = (Temporary / 10U) * 9U;

    // Clear Lists, Mode, Ratio and IR
    Temporary                               = Controller->Registers->HcControl;
    Temporary                               &= ~(OHCI_CONTROL_ALL_ACTIVE | OHCI_CONTROL_SUSPEND | OHCI_CONTROL_RATIO_MASK | OHCI_CONTROL_IR);

    // Set Ratio (4:1) and Mode (Operational)
    Temporary                               |= (OHCI_CONTROL_RATIO_MASK | OHCI_CONTROL_ACTIVE);
    Temporary                               |= OHCI_CONTROL_PERIODIC_ACTIVE;
    Temporary                               |= OHCI_CONTROL_ISOC_ACTIVE;
    Temporary                               |= OHCI_CONTROL_REMOTEWAKE;
    Controller->Registers->HcControl        = Temporary;
    TRACE(" > Wrote control to controller");
    return OsSuccess;
}

/* OhciSetup
 * Initializes a controller from unknown state to a working state 
 * with resources allocated. */
OsStatus_t
OhciSetup(
    _In_ OhciController_t*          Controller)
{
    // Variables
    void *VirtualPointer        = NULL;
    uintptr_t PhysicalAddress   = 0;
    reg32_t Temporary           = 0;
    int i;

    // Trace
    TRACE("OhciSetup()");

    // Allocate the HCCA-space in low memory as controllers
    // have issues with higher memory (<2GB)
    if (MemoryAllocate(NULL, 0x1000, MEMORY_CLEAN | MEMORY_COMMIT
        | MEMORY_LOWFIRST | MEMORY_UNCHACHEABLE, &VirtualPointer, &PhysicalAddress) != OsSuccess) {
        ERROR("Failed to allocate space for HCCA");
        return OsError;
    }

    // Cast the pointer to hcca
    Controller->HccaPhysical    = LODWORD(PhysicalAddress);
    Controller->Hcca            = (OhciHCCA_t*)VirtualPointer;

    // Retrieve the revision of the controller, we support 0x10 && 0x11
    Temporary                   = (Controller->Registers->HcRevision & 0xFF);
    if (Temporary != OHCI_REVISION1 && Temporary != OHCI_REVISION11) {
        ERROR("Invalid OHCI Revision (0x%x)", Temporary);
        return OsError;
    }

    // Initialize the queue system
    OhciQueueInitialize(Controller);

    // Last step is to take ownership, reset the controller and initialize
    // the registers, all resource must be allocated before this
    if (OhciTakeControl(Controller) != OsSuccess || OhciReset(Controller) != OsSuccess) {
        ERROR("Failed to initialize the ohci-controller");
        return OsError;
    }
    
    // Controller should now be in a running state
    TRACE("Controller %u Started, Control 0x%x, Ints 0x%x, FmInterval 0x%x",
        Controller->Base.Id, Controller->Registers->HcControl, 
        Controller->Registers->HcInterruptEnable, Controller->Registers->HcFmInterval);

    // We are not completely done yet, we need to figure out the
    // power-mode of the controller, and we need the port-count
    if (Controller->Registers->HcRhDescriptorA & (1 << 9)) {
        // Power is always on
        Controller->PowerMode = AlwaysOn;
        Controller->Registers->HcRhStatus = OHCI_STATUS_POWER_ENABLED;
        Controller->Registers->HcRhDescriptorB = 0;
    }
    else {
        // Ports have individual power
        if (Controller->Registers->HcRhDescriptorA & (1 << 8)) {
            // We prefer this, we can control each port's power
            Controller->PowerMode = PortControl;
            Controller->Registers->HcRhDescriptorB = 0xFFFF0000;
        }
        else {
            // Oh well, it's either all on or all off
            Controller->Registers->HcRhDescriptorB = 0;
            Controller->Registers->HcRhStatus = OHCI_STATUS_POWER_ENABLED;
            Controller->PowerMode = GlobalControl;
        }
    }

    // Get Port count from (DescriptorA & 0x7F)
    Controller->Base.PortCount = Controller->Registers->HcRhDescriptorA & 0x7F;
    if (Controller->Base.PortCount > OHCI_MAXPORTS) {
        Controller->Base.PortCount = OHCI_MAXPORTS;
    }

    // Clear RhA
    Controller->Registers->HcRhDescriptorA &= ~(0x00000000 | OHCI_DESCRIPTORA_DEVICETYPE);

    // Get Power On Delay
    // PowerOnToPowerGoodTime (24 - 31)
    // This byte specifies the duration HCD has to wait before
    // accessing a powered-on Port of the Root Hub.
    // It is implementation-specific.  The unit of time is 2 ms.
    // The duration is calculated as POTPGT * 2 ms.
    Temporary = Controller->Registers->HcRhDescriptorA;
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
    if (UsbManagerRegisterController(&Controller->Base) != OsSuccess) {
        ERROR("Failed to register uhci controller with the system.");
    }

    // Now we can enable hub events (and clear interrupts)
    Controller->Registers->HcInterruptStatus &= ~(reg32_t)0;
    Controller->Registers->HcInterruptEnable = OHCI_ROOTHUB_EVENT;

    // Enumerate the ports
    for (i = 0; i < (int)Controller->Base.PortCount; i++) {
        // If power has been disabled, enable it
        if (!(Controller->Registers->HcRhPortStatus[i] & OHCI_PORT_POWER)) {
            Controller->Registers->HcRhPortStatus[i] = OHCI_PORT_POWER;
        }

        // Initialize port if connected
        if (Controller->Registers->HcRhPortStatus[i] & OHCI_PORT_CONNECTED) {
            OhciPortInitialize(Controller, i);
        }
    }
    return OsSuccess;
}
