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
 * MollenOS MCore - Advanced Host Controller Interface Driver
 * TODO:
 *    - Port Multiplier Support
 *    - Power Management
 */
//#define __TRACE

#include <os/mollenos.h>
#include <ddk/busdevice.h>
#include <ddk/utils.h>
#include <threads.h>
#include <stdlib.h>
#include "ahci.h"

// Prototypes
InterruptStatus_t OnFastInterrupt(InterruptFunctionTable_t*, InterruptResourceTable_t*);
OsStatus_t        AhciSetup(AhciController_t* Controller);

AhciController_t*
AhciControllerCreate(
    _In_ BusDevice_t* Device)
{
    AhciController_t* Controller;
    DeviceInterrupt_t Interrupt;
    DeviceIo_t*       IoBase = NULL;
    OsStatus_t        Status;
    int               i;

    Controller = (AhciController_t*)malloc(sizeof(AhciController_t));
    if (!Controller) {
        return NULL;
    }
    
    memset(Controller, 0, sizeof(AhciController_t));
    memcpy(&Controller->Device, Device, Device->Base.Length);
    
    spinlock_init(&Controller->Lock, spinlock_plain);

    // Get I/O Base, and for AHCI there might be between 1-5
    // IO-spaces filled, so we always, ALWAYS go for the last one
    for (i = __DEVICEMANAGER_MAX_IOSPACES - 1; i >= 0; i--) {
        if (Controller->Device.IoSpaces[i].Type == DeviceIoMemoryBased) {
            IoBase = &Controller->Device.IoSpaces[i];
            break;
        }
    }

    // Sanitize that we found the io-space
    if (IoBase == NULL) {
        ERROR("No memory space found for ahci-controller");
        free(Controller);
        return NULL;
    }
    TRACE("Found Io-Space (Type %u, Physical 0x%x, Size 0x%x)",
        IoBase->Type, IoBase->Access.Memory.PhysicalBase, IoBase->Access.Memory.Length);

    // Acquire the io-space
    Status = AcquireDeviceIo(IoBase); 
    if (Status != OsSuccess) {
        ERROR("Failed to create and acquire the io-space for ahci-controller");
        free(Controller);
        return NULL;
    }
    
    Controller->IoBase = IoBase;
    TRACE("Io-Space was assigned virtual address 0x%x", IoBase->Access.Memory.VirtualBase);
    Controller->Registers = (AHCIGenericRegisters_t*)IoBase->Access.Memory.VirtualBase;
    
    DeviceInterruptInitialize(&Interrupt, Device);
    RegisterFastInterruptHandler(&Interrupt, OnFastInterrupt);
    RegisterFastInterruptIoResource(&Interrupt, IoBase);
    RegisterFastInterruptMemoryResource(&Interrupt, 
        (uintptr_t)&Controller->InterruptResource, sizeof(AhciInterruptResource_t), 0);

    // Register interrupt
    TRACE(" > ahci interrupt line is %u", Interrupt.Line);
    RegisterInterruptDescriptor(&Interrupt, Controller);
    Controller->InterruptId = RegisterInterruptSource(&Interrupt, 0);

    // Enable device
    Status = IoctlDevice(Controller->Device.Base.Id, __DEVICEMANAGER_IOCTL_BUS,
        (__DEVICEMANAGER_IOCTL_ENABLE | __DEVICEMANAGER_IOCTL_MMIO_ENABLE | __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE));
    if (Status != OsSuccess || Controller->InterruptId == UUID_INVALID) {
        ERROR("Failed to enable the ahci-controller");
        UnregisterInterruptSource(Controller->InterruptId);
        ReleaseDeviceIo(Controller->IoBase);
        free(Controller);
        return NULL;
    }

    // Now that all formalities has been taken care
    // off we can actually setup controller
    if (AhciSetup(Controller) == OsSuccess) {
        return Controller;
    }
    else {
        AhciControllerDestroy(Controller);
        return NULL;
    }
}

OsStatus_t
AhciControllerDestroy(
    _In_ AhciController_t* Controller)
{
    int i;

    // First step is to clear out all ports
    // this releases devices and resources
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (Controller->Ports[i] != NULL) {
            AhciPortCleanup(Controller, Controller->Ports[i]);
        }
    }

    UnregisterInterruptSource(Controller->InterruptId);
    ReleaseDeviceIo(Controller->IoBase);
    free(Controller);
    return OsSuccess;
}

OsStatus_t
AhciReset(
    _In_ AhciController_t* Controller)
{
    reg32_t Ghc;
    int     Hung = 0;
    int     i;
    TRACE("AhciReset()");

    // Software may perform an HBA reset prior to initializing the controller by setting GHC.AE to 1 and then setting GHC.HR to 1 if desired
    Ghc = READ_VOLATILE(Controller->Registers->GlobalHostControl);
    WRITE_VOLATILE(Controller->Registers->GlobalHostControl, Ghc | AHCI_HOSTCONTROL_HR);

    // The bit shall be cleared to 0 by the HBA when the reset is complete. 
    // If the HBA has not cleared GHC.HR to 0 within 1 second of 
    // software setting GHC.HR to 1, the HBA is in a hung or locked state.
    WaitForConditionWithFault(Hung, 
        ((READ_VOLATILE(Controller->Registers->GlobalHostControl) & AHCI_HOSTCONTROL_HR) == 0),
        10, 200);
    if (Hung) {
        return OsError;
    }

    // If the HBA supports staggered spin-up, the PxCMD.SUD bit will be reset to 0; 
    // software is responsible for setting the PxCMD.SUD and PxSCTL.DET fields 
    // appropriately such that communication can be established on the Serial ATA link. 
    // If the HBA does not support staggered spin-up, the HBA reset shall cause 
    // a COMRESET to be sent on the port.

    // Indicate that system software is AHCI aware by setting GHC.AE to 1.
    if (!(READ_VOLATILE(Controller->Registers->Capabilities) & AHCI_CAPABILITIES_SAM)) {
        Ghc = READ_VOLATILE(Controller->Registers->GlobalHostControl);
        WRITE_VOLATILE(Controller->Registers->GlobalHostControl, Ghc | AHCI_HOSTCONTROL_AE);
    }

    // Ensure that the controller is not in the running state by reading and
    // examining each implemented ports PxCMD register
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!(Controller->ValidPorts & AHCI_IMPLEMENTED_PORT(i))) {
            continue;
        }
        TRACE(" > port %i status after reset: 0x%x", i, Controller->Ports[i]->Registers->CommandAndStatus);
    }
    return OsSuccess;
}

OsStatus_t
AhciTakeOwnership(
    _In_ AhciController_t* Controller)
{
    reg32_t OsCtrl;
    int     Hung = 0;
    TRACE("AhciTakeOwnership()");

    // Step 1. Sets the OS Ownership (BOHC.OOS) bit to 1.
    OsCtrl = READ_VOLATILE(Controller->Registers->OSControlAndStatus);
    WRITE_VOLATILE(Controller->Registers->OSControlAndStatus, OsCtrl | AHCI_CONTROLSTATUS_OOS);

    // Wait 25 ms, to determine how long time BIOS needs to release
    thrd_sleepex(25);

    // If the BIOS Busy (BOHC.BB) has been set to 1 within 25 milliseconds, 
    // then the OS driver shall provide the BIOS a minimum of two seconds 
    // for finishing outstanding commands on the HBA.
    if (READ_VOLATILE(Controller->Registers->OSControlAndStatus) & AHCI_CONTROLSTATUS_BB) {
        thrd_sleepex(2000);
    }

    // Step 2. Spin on the BIOS Ownership (BOHC.BOS) bit, waiting for it to be cleared to 0.
    WaitForConditionWithFault(Hung, 
        ((READ_VOLATILE(Controller->Registers->OSControlAndStatus) & AHCI_CONTROLSTATUS_BOS) == 0),
        10, 25);

    // Sanitize if we got the ownership 
    if (Hung) {
        return OsError;
    }
    else {
        return OsSuccess;
    }
}

OsStatus_t
AhciSetup(
    _In_ AhciController_t*Controller)
{
    reg32_t Ghc;
    reg32_t Caps;
    int     FullResetRequired = 0;
    int     ActivePortCount   = 0;
    int     i;
    TRACE("AhciSetup()");

    // Take ownership of the controller
    if (AhciTakeOwnership(Controller) != OsSuccess) {
        ERROR("Failed to take ownership of the controller.");
        return OsError;
    }

    // Indicate that system software is AHCI aware by setting GHC.AE to 1.
    Caps = READ_VOLATILE(Controller->Registers->Capabilities);
    if (!(Caps & AHCI_CAPABILITIES_SAM)) {
        Ghc = READ_VOLATILE(Controller->Registers->GlobalHostControl);
        WRITE_VOLATILE(Controller->Registers->GlobalHostControl, Ghc | AHCI_HOSTCONTROL_AE);
    }

    // Determine which ports are implemented by the HBA, by reading the PI register. 
    // This bit map value will aid software in determining how many ports are 
    // available and which port registers need to be initialized.
    Controller->ValidPorts       = READ_VOLATILE(Controller->Registers->PortsImplemented);
    Controller->CommandSlotCount = AHCI_CAPABILITIES_NCS(Caps);
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!(Controller->ValidPorts & AHCI_IMPLEMENTED_PORT(i))) {
            continue;
        }
        Controller->PortCount++;
    }
    TRACE("Port Validity Bitmap 0x%x, Capabilities 0x%x", Controller->ValidPorts, Caps);

    // Initialize ports
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!(Controller->ValidPorts & AHCI_IMPLEMENTED_PORT(i))) {
            continue;
        }

        // Create a port descriptor and get register access
        Controller->Ports[i] = AhciPortCreate(Controller, ActivePortCount++, i);
        AhciPortInitiateSetup(Controller, Controller->Ports[i]);
        AhciPortRebase(Controller, Controller->Ports[i]);
    }

    // Finish the stop sequences
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (Controller->Ports[i] != NULL) {
            if (AhciPortFinishSetup(Controller, Controller->Ports[i]) != OsSuccess) {
                ERROR(" > failed to initialize port %i", i);
                FullResetRequired = 1;
                break;
            }
        }
    }

    // Perform full reset if required here
    if (FullResetRequired) {
        if (AhciReset(Controller) != OsSuccess) {
            ERROR("Failed to initialize the AHCI controller, aborting");
            return OsError;
        }
    }

    // To enable the HBA to generate interrupts, 
    // system software must also set GHC.IE to a 1
    Ghc = READ_VOLATILE(Controller->Registers->GlobalHostControl);
    WRITE_VOLATILE(Controller->Registers->InterruptStatus, 0xFFFFFFFF);
    WRITE_VOLATILE(Controller->Registers->GlobalHostControl, Ghc | AHCI_HOSTCONTROL_IE);
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (Controller->Ports[i] != NULL) {
            if (AhciPortStart(Controller, Controller->Ports[i]) != OsSuccess) {
                ERROR(" > failed to start port %i", i);
            }
        }
    }
    return OsSuccess;
}
