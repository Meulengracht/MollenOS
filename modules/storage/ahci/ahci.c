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
#include <ddk/utils.h>
#include <threads.h>
#include <stdlib.h>
#include "ahci.h"

// Prototypes
InterruptStatus_t OnFastInterrupt(FastInterruptResources_t*, void*);
OsStatus_t        AhciSetup(AhciController_t* Controller);

/* AhciControllerCreate
 * Registers a new controller with the AHCI driver */
AhciController_t*
AhciControllerCreate(
    _In_ MCoreDevice_t* Device)
{
    AhciController_t* Controller;
    DeviceIo_t*       IoBase = NULL;
    OsStatus_t        Status;
    int               i;

    Controller = (AhciController_t*)malloc(sizeof(AhciController_t));
    memset(Controller, 0, sizeof(AhciController_t));
    memcpy(&Controller->Device, Device, Device->Length);
    
    Controller->Contract.DeviceId = Controller->Device.Id;
    SpinlockReset(&Controller->Lock, 0);

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

    // Start out by initializing the contract
    InitializeContract(&Controller->Contract, Controller->Contract.DeviceId, 1,
        ContractController, "AHCI Controller Interface");
    TRACE("Io-Space was assigned virtual address 0x%x", IoBase->Access.Memory.VirtualBase);

    // Instantiate the register-access
    Controller->Registers = (AHCIGenericRegisters_t*)IoBase->Access.Memory.VirtualBase;
    RegisterFastInterruptHandler(&Controller->Device.Interrupt, OnFastInterrupt);
    RegisterFastInterruptIoResource(&Controller->Device.Interrupt, IoBase);
    RegisterFastInterruptMemoryResource(&Controller->Device.Interrupt, 
        (uintptr_t)&Controller->InterruptResource, sizeof(AhciInterruptResource_t), 0);

    // Register contract before interrupt
    Status = RegisterContract(&Controller->Contract);
    if (Status != OsSuccess) {
        ERROR("Failed to register contract for ahci-controller");
        ReleaseDeviceIo(Controller->IoBase);
        free(Controller);
        return NULL;
    }

    // Register interrupt
    TRACE(" > ahci interrupt line is %u", Controller->Device.Interrupt.Line);
    RegisterInterruptContext(&Controller->Device.Interrupt, Controller);
    Controller->InterruptId = RegisterInterruptSource(&Controller->Device.Interrupt, INTERRUPT_USERSPACE);

    // Enable device
    Status = IoctlDevice(Controller->Device.Id, __DEVICEMANAGER_IOCTL_BUS,
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

/* AhciControllerDestroy
 * Destroys an existing controller instance and cleans up
 * any resources related to it */
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

    // Free the controller resources
    if (Controller->CommandListBase != NULL) {
        MemoryFree(Controller->CommandListBase, 1024 * Controller->PortCount);
    }
    if (Controller->CommandTableBase != NULL) {
        MemoryFree(Controller->CommandListBase, 
            (AHCI_COMMAND_TABLE_SIZE * 32) * Controller->PortCount);
    }
    if (Controller->FisBase != NULL) {
        if (ReadVolatile32(&Controller->Registers->Capabilities) & AHCI_CAPABILITIES_FBSS) {
            MemoryFree(Controller->FisBase, 0x1000 * Controller->PortCount);
        }
        else {
            MemoryFree(Controller->FisBase, 256 * Controller->PortCount);
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
    Ghc = ReadVolatile32(&Controller->Registers->GlobalHostControl);
    WriteVolatile32(&Controller->Registers->GlobalHostControl, Ghc | AHCI_HOSTCONTROL_HR);

    // The bit shall be cleared to 0 by the HBA when the reset is complete. 
    // If the HBA has not cleared GHC.HR to 0 within 1 second of 
    // software setting GHC.HR to 1, the HBA is in a hung or locked state.
    WaitForConditionWithFault(Hung, 
        ((ReadVolatile32(&Controller->Registers->GlobalHostControl) & AHCI_HOSTCONTROL_HR) == 0),
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
    if (!(ReadVolatile32(&Controller->Registers->Capabilities) & AHCI_CAPABILITIES_SAM)) {
        Ghc = ReadVolatile32(&Controller->Registers->GlobalHostControl);
        WriteVolatile32(&Controller->Registers->GlobalHostControl, Ghc | AHCI_HOSTCONTROL_AE);
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
    OsCtrl = ReadVolatile32(&Controller->Registers->OSControlAndStatus);
    WriteVolatile32(&Controller->Registers->OSControlAndStatus, OsCtrl | AHCI_CONTROLSTATUS_OOS);

    // Wait 25 ms, to determine how long time BIOS needs to release
    thrd_sleepex(25);

    // If the BIOS Busy (BOHC.BB) has been set to 1 within 25 milliseconds, 
    // then the OS driver shall provide the BIOS a minimum of two seconds 
    // for finishing outstanding commands on the HBA.
    if (ReadVolatile32(&Controller->Registers->OSControlAndStatus) & AHCI_CONTROLSTATUS_BB) {
        thrd_sleepex(2000);
    }

    // Step 2. Spin on the BIOS Ownership (BOHC.BOS) bit, waiting for it to be cleared to 0.
    WaitForConditionWithFault(Hung, 
        ((ReadVolatile32(&Controller->Registers->OSControlAndStatus) & AHCI_CONTROLSTATUS_BOS) == 0),
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
AllocateOperationalMemory(
    _In_ AhciController_t* Controller)
{
    Flags_t MemoryFlags = MEMORY_LOWFIRST | MEMORY_CONTIGIOUS | MEMORY_CLEAN | 
        MEMORY_COMMIT | MEMORY_UNCHACHEABLE | MEMORY_READ | MEMORY_WRITE;
    TRACE("AllocateOperationalMemory()");

    // Allocate some shared resources. The resource we need is 
    // 1K for the Command List per port
    // A Command table for each command header (32) per port
    if (MemoryAllocate(NULL, sizeof(AHCICommandList_t) * Controller->PortCount, 
        MemoryFlags, &Controller->CommandListBase, &Controller->CommandListBasePhysical) != OsSuccess) {
        ERROR("AHCI::Failed to allocate memory for the command list.");
        return OsError;
    }
    if (MemoryAllocate(NULL, (AHCI_COMMAND_TABLE_SIZE * 32) * Controller->PortCount, 
        MemoryFlags, &Controller->CommandTableBase, &Controller->CommandTableBasePhysical) != OsSuccess) {
        ERROR("AHCI::Failed to allocate memory for the command table.");
        return OsError;
    }

    // Trace allocations
    TRACE("Command List memory at 0x%x (Physical 0x%x), size 0x%x",
        Controller->CommandListBase, Controller->CommandListBasePhysical,
        sizeof(AHCICommandList_t) * Controller->PortCount);
    TRACE("Command Table memory at 0x%x (Physical 0x%x), size 0x%x",
        Controller->CommandTableBase, Controller->CommandTableBasePhysical,
        (AHCI_COMMAND_TABLE_SIZE * 32) * Controller->PortCount);
    
    // We have to take into account FIS based switching here, 
    // if it's supported we need 4K per port, otherwise 256 bytes per port
    if (ReadVolatile32(&Controller->Registers->Capabilities) & AHCI_CAPABILITIES_FBSS) {
        if (MemoryAllocate(NULL, 0x1000 * Controller->PortCount,
            MemoryFlags, &Controller->FisBase, &Controller->FisBasePhysical) != OsSuccess) {
            ERROR("AHCI::Failed to allocate memory for the fis-area.");
            return OsError;
        }
    }
    else {
        if (MemoryAllocate(NULL, 256 * Controller->PortCount,
            MemoryFlags, &Controller->FisBase, &Controller->FisBasePhysical) != OsSuccess) {
            ERROR("AHCI::Failed to allocate memory for the fis-area.");
            return OsError;
        }
    }
    TRACE("FIS-Area memory at 0x%x (Physical 0x%x), size 0x%x", 
        Controller->FisBase, Controller->FisBasePhysical, (AHCI_COMMAND_TABLE_SIZE * 32) * Controller->PortCount);
    return OsSuccess;
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
    Caps = ReadVolatile32(&Controller->Registers->Capabilities);
    if (!(Caps & AHCI_CAPABILITIES_SAM)) {
        Ghc = ReadVolatile32(&Controller->Registers->GlobalHostControl);
        WriteVolatile32(&Controller->Registers->GlobalHostControl, Ghc | AHCI_HOSTCONTROL_AE);
    }

    // Determine which ports are implemented by the HBA, by reading the PI register. 
    // This bit map value will aid software in determining how many ports are 
    // available and which port registers need to be initialized.
    Controller->ValidPorts       = ReadVolatile32(&Controller->Registers->PortsImplemented);
    Controller->CommandSlotCount = AHCI_CAPABILITIES_NCS(Caps);
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!(Controller->ValidPorts & AHCI_IMPLEMENTED_PORT(i))) {
            continue;
        }
        Controller->PortCount++;
    }
    TRACE("Port Validity Bitmap 0x%x, Capabilities 0x%x", Controller->ValidPorts, Caps);

    // Allocate memory neccessary, we must have set Controller->PortCount by this point
    if (AllocateOperationalMemory(Controller) != OsSuccess) {
        ERROR("Failed to allocate neccessary memory for the controller.");
        return OsError;
    }

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
    Ghc = ReadVolatile32(&Controller->Registers->GlobalHostControl);
    WriteVolatile32(&Controller->Registers->InterruptStatus, 0xFFFFFFFF);
    WriteVolatile32(&Controller->Registers->GlobalHostControl, Ghc | AHCI_HOSTCONTROL_IE);
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (Controller->Ports[i] != NULL) {
            if (AhciPortStart(Controller, Controller->Ports[i]) != OsSuccess) {
                ERROR(" > failed to start port %i", i);
            }
        }
    }
    return OsSuccess;
}
