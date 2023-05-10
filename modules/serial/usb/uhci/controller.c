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
#include <os/device.h>
#include "uhci.h"
#include <threads.h>
#include <stdlib.h>

oserr_t     UhciSetup(UhciController_t *controller);
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

static oserr_t
__AcquireIOSpace(
        _In_ BusDevice_t*      busDevice,
        _In_ UhciController_t* controller)
{
    DeviceIo_t* ioBase = NULL;

    // Get I/O Base, and for UHCI it'll be the first address we encounter
    // of type IO
    for (int i = 0; i < __DEVICEMANAGER_MAX_IOSPACES; i++) {
        if (controller->Base.Device->IoSpaces[i].Type == DeviceIoPortBased) {
            ioBase = &controller->Base.Device->IoSpaces[i];
            break;
        }
    }

    if (ioBase == NULL) {
        return OS_ENOENT;
    }

    controller->Base.IoBase = ioBase;
    return AcquireDeviceIo(ioBase);
}

static oserr_t
__InitializeInterrupt(
        _In_ BusDevice_t*      busDevice,
        _In_ UhciController_t* controller)
{
    DeviceInterrupt_t deviceInterrupt;

    // Initialize the interrupt settings
    DeviceInterruptInitialize(&deviceInterrupt, busDevice);
    RegisterInterruptDescriptor(&deviceInterrupt, controller->Base.event_descriptor);
    RegisterFastInterruptHandler(&deviceInterrupt, (InterruptHandler_t)OnFastInterrupt);
    RegisterFastInterruptIoResource(&deviceInterrupt, controller->Base.IoBase);
    RegisterFastInterruptMemoryResource(&deviceInterrupt, (uintptr_t)controller, sizeof(UhciController_t), 0);
    controller->Base.Interrupt = RegisterInterruptSource(&deviceInterrupt, 0);
    if (controller->Base.Interrupt == UUID_INVALID) {
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

UsbManagerController_t*
HCIControllerCreate(
    _In_ BusDevice_t* busDevice)
{
    UhciController_t* controller;
    size_t            ioctlValue = 0;
    oserr_t           oserr;
    
    // Debug
    TRACE("UhciControllerCreate(device=0x%" PRIxIN ", device->Length=%u, device->Id=%u)",
          busDevice, LODWORD(busDevice->Base.Length), busDevice->Base.Id);

    controller = (UhciController_t*)UsbManagerCreateController(
            busDevice,
            USBCONTROLLER_KIND_OHCI,
            sizeof(UhciController_t)
    );
    if (!controller) {
        return NULL;
    }

    oserr = __AcquireIOSpace(busDevice, controller);
    if (oserr != OS_EOK) {
        ERROR("UhciControllerCreate: failed to find suitable io-space");
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }

    TRACE(
            "UhciControllerCreate: io-space: type=%u, physical=0x%x, size=0x%x",
            controller->Base.IoBase->Type, controller->Base.IoBase->Access.Memory.PhysicalBase,
            controller->Base.IoBase->Access.Memory.Length
    );

    oserr = __InitializeInterrupt(busDevice, controller);
    if (oserr != OS_EOK) {
        ERROR("UhciControllerCreate: failed to initialize interrupt resources");
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }

    // Enable the underlying bus device
    oserr = OSDeviceIOCtl(
            controller->Base.Device->Base.Id,
            OSIOCTLREQUEST_BUS_CONTROL,
            &(struct OSIOCtlBusControl) {
                    .Flags = (__DEVICEMANAGER_IOCTL_ENABLE | __DEVICEMANAGER_IOCTL_IO_ENABLE |
                              __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE)
            }, sizeof(struct OSIOCtlBusControl)
    );
    if (oserr != OS_EOK) {
        ERROR("UhciControllerCreate: failed to enable PCI device");
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }

    // Claim the BIOS ownership and enable pci interrupts
    ioctlValue = 0x2000;
    if (IoctlDeviceEx(controller->Base.Device->Base.Id, __DEVICEMANAGER_IOCTL_EXT_WRITE,
                      UHCI_USBLEGEACY, &ioctlValue, 2) != OS_EOK) {
        return NULL;
    }

    // If vendor is Intel we null out the intel register
    if (controller->Base.Device->Base.VendorId == 0x8086) {
        ioctlValue = 0x00;
        if (IoctlDeviceEx(controller->Base.Device->Base.Id, __DEVICEMANAGER_IOCTL_EXT_WRITE,
                          UHCI_USBRES_INTEL, &ioctlValue, 1) != OS_EOK) {
            return NULL;
        }
    }

    // Now that all formalities has been taken care
    // off we can actually setup controller
    oserr = UhciSetup(controller);
    if (oserr != OS_EOK) {
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }
    return &controller->Base;
}

void
HCIControllerDestroy(
    _In_ UsbManagerController_t* Controller)
{
    // Unregister, then destroy
    UsbManagerDestroyController(Controller);
    
    // Cleanup scheduler
    UHCIQueueDestroy((UhciController_t*)Controller);

    // Unregister the interrupt
    UnregisterInterruptSource(Controller->Interrupt);

    // Release the io-space
    ReleaseDeviceIo(Controller->IoBase);
    free(Controller);
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

oserr_t
UhciStart(
    _In_ UhciController_t* Controller,
    _In_ int               Wait)
{
    uint16_t cmd;
    TRACE("UhciStart()");

    // Read current command register
    // to preserve information, then assert some flags
    cmd = UhciRead16(Controller, UHCI_REGISTER_COMMAND);
    cmd |= (UHCI_COMMAND_CONFIGFLAG | UHCI_COMMAND_RUN | UHCI_COMMAND_MAXPACKET64);
    UhciWrite16(Controller, UHCI_REGISTER_COMMAND, cmd);
    if (Wait == 0) {
        return OS_EOK;
    }

    // Wait for controller to start
    WaitForConditionWithFault(cmd,
        (UhciRead16(Controller, UHCI_REGISTER_STATUS) & UHCI_STATUS_HALTED) == 0,
                              100, 10
    );
    return (cmd == 0) ? OS_EOK : OS_ETIMEOUT;
}

oserr_t
UhciStop(
    _In_ UhciController_t* Controller)
{
    uint16_t cmd;
    
    // Read current command register
    // to preserve information, then deassert run flag
    cmd = UhciRead16(Controller, UHCI_REGISTER_COMMAND);
    cmd &= ~(UHCI_COMMAND_RUN);
    UhciWrite16(Controller, UHCI_REGISTER_COMMAND, cmd);
    return OS_EOK;
}

oserr_t
UhciReset(
    _In_ UhciController_t*  Controller)
{
    uint16_t tmp;

    // Assert the host-controller reset bit
    UhciWrite16(Controller, UHCI_REGISTER_COMMAND, UHCI_COMMAND_HCRESET);

    // Wait for it to stop being asserted
    WaitForConditionWithFault(tmp, (UhciRead16(Controller, UHCI_REGISTER_COMMAND) & UHCI_COMMAND_HCRESET) == 0, 100, 25);
    if (tmp == 1) {
        WARNING("UHCI: Reset signal is still active..");
        thrd_sleep(&(struct timespec) { .tv_nsec = 200 * NSEC_PER_MSEC }, NULL); // give it another try
        if (UhciRead16(Controller, UHCI_REGISTER_COMMAND) & UHCI_COMMAND_HCRESET) {
            ERROR("UHCI::Giving up on controller reset");
            return OS_EUNKNOWN;
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
    return OS_EOK;
}

oserr_t
UhciSetup(
    _In_ UhciController_t* controller)
{
    oserr_t oserr;
    int     i;
    TRACE("UhciSetup()");

    // Disable interrupts while configuring (and stop controller)
    UhciWrite16(controller, UHCI_REGISTER_COMMAND, 0x0000);
    UhciWrite16(controller, UHCI_REGISTER_INTR, 0x0000);

    // Perform a global reset, we must wait 100 ms for this complete
    UhciWrite16(controller, UHCI_REGISTER_COMMAND, UHCI_COMMAND_GRESET);
    thrd_sleep(&(struct timespec) { .tv_nsec = 100 * NSEC_PER_MSEC }, NULL);
    UhciWrite16(controller, UHCI_REGISTER_COMMAND, 0x0000);

    // Initialize queues and controller
    oserr = UhciQueueInitialize(controller);
    if (oserr != OS_EOK) {
        ERROR("UhciSetup: failed to initialize memory for queues");
        return oserr;
    }

    // Set up the i/o requirements, UHCI controllers sometimes have problems
    // with addresses above >2gb. Also buffer alignment must be used for UHCI as
    // we cannot deal with cross-page boundary buffers. Thus buffers must be aligned
    // for the highest possible packet-size supported by USB 1.1.
    // Maximum data payload size for full-speed devices is 64 bytes.
    controller->Base.IORequirements.BufferAlignment = 64;
    controller->Base.IORequirements.Conformity = OSMEMORYCONFORMITY_LOW;

    oserr = UhciReset(controller);
    if (oserr != OS_EOK) {
        ERROR("UhciSetup: failed to reset controller");
        return oserr;
    }

    // Enumerate all available ports
    for (i = 0; i <= UHCI_MAX_PORTS; i++) {
        uint16_t base = UhciRead16(controller, (UHCI_REGISTER_PORT_BASE + (i * 2)));
        if (!(base & UHCI_PORT_RESERVED) || base == 0xFFFF) {
            // This reserved bit must be 1
            // And we must have 2 ports atleast
            break;
        }
    }

    // Store the number of available ports
    TRACE("UhciSetup: number of ports %i", i);
    controller->Base.PortCount = i;

    // Register the controller before starting
    oserr = UsbManagerRegisterController(&controller->Base);
    if (oserr != OS_EOK) {
        ERROR("Failed to register uhci controller with the system.");
        return oserr;
    }

    // Start the controller and return result from that
    return UhciStart(controller, 1);
}
