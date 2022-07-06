/**
 * MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Universal Host Controller Interface Driver
 * Todo:
 * Power Management
 */

//#define __TRACE

#include <ddk/interrupt.h>
#include <ddk/utils.h>
#include "uhci.h"
#include <threads.h>
#include <stdlib.h>

oscode_t        UhciSetup(UhciController_t *Controller);
irqstatus_t OnFastInterrupt(InterruptFunctionTable_t*, InterruptResourceTable_t*);

void
UhciControllerDump(
    _In_ UhciController_t* Controller)
{
    WARNING("Command(0x%x), Status(0x%x), Interrupt(0x%x)", 
        UhciRead16(Controller, UHCI_REGISTER_COMMAND),
        UhciRead16(Controller, UHCI_REGISTER_STATUS),
        UhciRead16(Controller, UHCI_REGISTER_INTR));
    WARNING("FrameNumber(0x%x), BaseAddress(0x%x), Sofmod(0x%x)", 
        UhciRead16(Controller, UHCI_REGISTER_FRNUM),
        UhciRead32(Controller, UHCI_REGISTER_FRBASEADDR),
        UhciRead8(Controller, UHCI_REGISTER_SOFMOD));
}

UsbManagerController_t*
HciControllerCreate(
    _In_ BusDevice_t* busDevice)
{
    UhciController_t* controller;
    DeviceInterrupt_t deviceInterrupt;
    DeviceIo_t*       ioBase = NULL;
    size_t            ioctlValue = 0;
    int               i;
    
    // Debug
    TRACE("UhciControllerCreate(device=0x%" PRIxIN ", device->Length=%u, device->Id=%u)",
          busDevice, LODWORD(busDevice->Base.Length), busDevice->Base.Id);

    controller = (UhciController_t*)UsbManagerCreateController(busDevice, UsbUHCI, sizeof(UhciController_t));
    if (!controller) {
        return NULL;
    }
    
    // Get I/O Base, and for UHCI it'll be the first address we encounter
    // of type IO
    for (i = 0; i < __DEVICEMANAGER_MAX_IOSPACES; i++) {
        if (controller->Base.Device.IoSpaces[i].Type == DeviceIoPortBased) {
            TRACE(" > found io-space at bar %i", i);
            ioBase = &controller->Base.Device.IoSpaces[i];
            break;
        }
    }

    // Sanitize that we found the io-space
    if (ioBase == NULL) {
        ERROR("No memory space found for uhci-controller");
        free(controller);
        return NULL;
    }

    // Trace
    TRACE("Found Io-Space (Type %u, Physical 0x%x, Size 0x%x)",
          ioBase->Type, ioBase->Access.Memory.PhysicalBase, ioBase->Access.Memory.Length);

    // Acquire the io-space
    if (AcquireDeviceIo(ioBase) != OsOK) {
        ERROR("Failed to create and acquire the io-space for uhci-controller");
        free(controller);
        return NULL;
    }
    else {
        // Store information
        controller->Base.IoBase = ioBase;
    }

    // Initialize the interrupt settings
    DeviceInterruptInitialize(&deviceInterrupt, busDevice);
    RegisterInterruptDescriptor(&deviceInterrupt, controller->Base.event_descriptor);
    RegisterFastInterruptHandler(&deviceInterrupt, (InterruptHandler_t)OnFastInterrupt);
    RegisterFastInterruptIoResource(&deviceInterrupt, ioBase);
    RegisterFastInterruptMemoryResource(&deviceInterrupt, (uintptr_t)controller, sizeof(UhciController_t), 0);

    // Register interrupt
    controller->Base.Interrupt = RegisterInterruptSource(&deviceInterrupt, 0);

    // Enable device
    if (IoctlDevice(controller->Base.Device.Base.Id, __DEVICEMANAGER_IOCTL_BUS,
                    (__DEVICEMANAGER_IOCTL_ENABLE | __DEVICEMANAGER_IOCTL_IO_ENABLE
            | __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE)) != OsOK) {
        ERROR("Failed to enable the uhci-controller");
        UnregisterInterruptSource(controller->Base.Interrupt);
        ReleaseDeviceIo(controller->Base.IoBase);
        free(controller);
        return NULL;
    }

    // Claim the BIOS ownership and enable pci interrupts
    ioctlValue = 0x2000;
    if (IoctlDeviceEx(controller->Base.Device.Base.Id, __DEVICEMANAGER_IOCTL_EXT_WRITE,
                      UHCI_USBLEGEACY, &ioctlValue, 2) != OsOK) {
        return NULL;
    }

    // If vendor is Intel we null out the intel register
    if (controller->Base.Device.Base.VendorId == 0x8086) {
        ioctlValue = 0x00;
        if (IoctlDeviceEx(controller->Base.Device.Base.Id, __DEVICEMANAGER_IOCTL_EXT_WRITE,
                          UHCI_USBRES_INTEL, &ioctlValue, 1) != OsOK) {
            return NULL;
        }
    }

    // Now that all formalities has been taken care
    // off we can actually setup controller
    if (UhciSetup(controller) == OsOK) {
        return &controller->Base;
    }
    else {
        HciControllerDestroy(&controller->Base);
        return NULL;
    }
}

oscode_t
HciControllerDestroy(
    _In_ UsbManagerController_t* Controller)
{
    // Unregister, then destroy
    UsbManagerDestroyController(Controller);
    
    // Cleanup scheduler
    UhciQueueDestroy((UhciController_t*)Controller);

    // Unregister the interrupt
    UnregisterInterruptSource(Controller->Interrupt);

    // Release the io-space
    ReleaseDeviceIo(Controller->IoBase);

    free(Controller);
    return OsOK;
}

void
HciTimerCallback(
    _In_ UsbManagerController_t* baseController)
{
    TRACE("HciTimerCallback()");
    UhciUpdateCurrentFrame((UhciController_t*)baseController);
    UhciPortsCheck((UhciController_t*)baseController);
    UsbManagerProcessTransfers((UsbManagerController_t*)baseController);
}

oscode_t
UhciStart(
    _In_ UhciController_t* Controller,
    _In_ int               Wait)
{
    uint16_t OldCmd;
    
    // Debug
    TRACE("UhciStart()");

    // Read current command register
    // to preserve information, then assert some flags
    OldCmd = UhciRead16(Controller, UHCI_REGISTER_COMMAND);
    OldCmd |= (UHCI_COMMAND_CONFIGFLAG | UHCI_COMMAND_RUN | UHCI_COMMAND_MAXPACKET64);

    // Update
    UhciWrite16(Controller, UHCI_REGISTER_COMMAND, OldCmd);
    if (Wait == 0) {
        return OsOK;
    }

    // Wait for controller to start
    OldCmd = 0;
    WaitForConditionWithFault(OldCmd, 
        (UhciRead16(Controller, UHCI_REGISTER_STATUS) & UHCI_STATUS_HALTED) == 0, 100, 10);
    return (OldCmd == 0) ? OsOK : OsError;
}

oscode_t
UhciStop(
    _In_ UhciController_t*  Controller)
{
    uint16_t OldCmd = 0;
    
    // Read current command register
    // to preserve information, then deassert run flag
    OldCmd = UhciRead16(Controller, UHCI_REGISTER_COMMAND);
    OldCmd &= ~(UHCI_COMMAND_RUN);

    // Update
    UhciWrite16(Controller, UHCI_REGISTER_COMMAND, OldCmd);
    return OsOK;
}

oscode_t
UhciReset(
    _In_ UhciController_t*  Controller)
{
    uint16_t Temp = 0;

    // Assert the host-controller reset bit
    UhciWrite16(Controller, UHCI_REGISTER_COMMAND, UHCI_COMMAND_HCRESET);

    // Wait for it to stop being asserted
    WaitForConditionWithFault(Temp, (UhciRead16(Controller, UHCI_REGISTER_COMMAND) & UHCI_COMMAND_HCRESET) == 0, 100, 25);
    if (Temp == 1) {
        WARNING("UHCI: Reset signal is still active..");
        thrd_sleepex(200); // give it another try
        if (UhciRead16(Controller, UHCI_REGISTER_COMMAND) & UHCI_COMMAND_HCRESET) {
            ERROR("UHCI::Giving up on controller reset");
            return OsError;
        }
    }

    // Clear out command and interrupt register
    UhciWrite16(Controller, UHCI_REGISTER_COMMAND,  0x0000);
    UhciWrite16(Controller, UHCI_REGISTER_INTR,     0x0000);

    // Now reconfigure the controller
    UhciWrite8(Controller, UHCI_REGISTER_SOFMOD,      64); // Frame length 1 ms
    UhciWrite32(Controller, UHCI_REGISTER_FRBASEADDR, Controller->Base.Scheduler->Settings.FrameListPhysical);
    UhciWrite16(Controller, UHCI_REGISTER_FRNUM,     (Controller->Frame & UHCI_FRAME_MASK));
    TRACE(" > Queue physical address at 0x%x", Controller->Base.Scheduler->Settings.FrameListPhysical);

    // Enable the interrupts that are relevant
    UhciWrite16(Controller, UHCI_REGISTER_INTR, 
        (UHCI_INTR_TIMEOUT | UHCI_INTR_SHORT_PACKET
        | UHCI_INTR_RESUME | UHCI_INTR_COMPLETION));
    return OsOK;
}

oscode_t
UhciSetup(
    _In_ UhciController_t *Controller)
{
    uint16_t Temp = 0;
    int      i;
    
    // Debug
    TRACE("UhciSetup()");

    // Disable interrupts while configuring (and stop controller)
    UhciWrite16(Controller, UHCI_REGISTER_COMMAND,  0x0000);
    UhciWrite16(Controller, UHCI_REGISTER_INTR,     0x0000);

    // Perform a global reset, we must wait 100 ms for this complete
    UhciWrite16(Controller, UHCI_REGISTER_COMMAND,  UHCI_COMMAND_GRESET);
    thrd_sleepex(100);
    UhciWrite16(Controller, UHCI_REGISTER_COMMAND,  0x0000);

    // Initialize queues and controller
    TRACE(" > Initializing queue system");
    UhciQueueInitialize(Controller);
    TRACE(" > Resetting controller");
    if (UhciReset(Controller) != OsOK) {
        ERROR("UHCI::Failed to reset controller");
        return OsError;
    }

    // Enumerate all available ports
    for (i = 0; i <= UHCI_MAX_PORTS; i++) {
        Temp = UhciRead16(Controller, (UHCI_REGISTER_PORT_BASE + (i * 2)));
        if (!(Temp & UHCI_PORT_RESERVED) || Temp == 0xFFFF) {
            // This reserved bit must be 1
            // And we must have 2 ports atleast
            break;
        }
    }

    // Store the number of available ports
    TRACE(" > Ports(%i)", i);
    Controller->Base.PortCount = i;

    // Register the controller before starting
    TRACE(" > Registering");
    if (UsbManagerRegisterController(&Controller->Base) != OsOK) {
        ERROR("Failed to register uhci controller with the system.");
    }

    // Start the controller and return result from that
    TRACE(" > Booting");
    return UhciStart(Controller, 1);
}
