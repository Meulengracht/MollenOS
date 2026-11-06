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

#include <ddk/interrupts/interrupt.h>
#include <ddk/utils.h>
#include <ddk/barrier.h>
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
    oserr_t     status;

    // UHCI registers use the first port-based I/O space.
    for (int i = 0; i < __DEVICEMANAGER_MAX_IOSPACES; i++) {
        if (((BusDevice_t*)controller->Base.Device)->IoSpaces[i].Type == DeviceIoPortBased) {
            ioBase = &((BusDevice_t*)controller->Base.Device)->IoSpaces[i];
            break;
        }
    }

    if (ioBase == NULL) {
        return OS_ENOENT;
    }

    status = AcquireDeviceIo(ioBase);
    if (status == OS_EOK) {
        controller->Base.IoBase = ioBase;
    }
    return status;
}

static oserr_t
__InitializeInterrupt(
    _In_ UhciController_t* controller)
{
    oserr_t status;

    // Disable interrupt sources until setup enables them.
    UhciWrite16(controller, UHCI_REGISTER_INTR, 0);
    
    // The ISR requires the resource order set by UsbManagerConfigureInterrupt.
    status = UsbManagerConfigureInterrupt(
        &controller->Base,
        (InterruptHandler_t)OnFastInterrupt,
        sizeof(UhciController_t)
    );
    if (status != OS_EOK) {
        return status;
    }
    return DeviceInterruptEnable(&controller->Base.Interrupt);
}

UsbManagerController_t*
HCIControllerCreate(
    _In_ Device_t* description)
{
    BusDevice_t*      busDevice = (BusDevice_t*)description;
    UhciController_t* controller;
    size_t            ioctlValue = 0;
    oserr_t           oserr;
    
    // Debug
    TRACE("UhciControllerCreate(device=0x%" PRIxIN ", device->Length=%u, device->Id=%u)",
          busDevice, LODWORD(busDevice->Base.Length), busDevice->Base.Id);

    if (description->Length != sizeof(BusDevice_t)) {
        return NULL;
    }

    controller = (UhciController_t*)UsbManagerCreateController(
        &busDevice->Base,
        USBCONTROLLER_KIND_UHCI,
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
        controller->Base.IoBase->Type, controller->Base.IoBase->Access.Port.Base,
        controller->Base.IoBase->Access.Port.Length
    );

    oserr = __InitializeInterrupt(controller);
    if (oserr != OS_EOK) {
        ERROR("UhciControllerCreate: failed to initialize interrupt resources");
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }

    // Enable the underlying bus device
    oserr = OSDeviceIOCtl(
        controller->Base.Device->Id,
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

    // Switch from BIOS control to the driver and enable PCI interrupts.
    ioctlValue = 0x2000;
    oserr = IoctlDeviceEx(
        controller->Base.Device->Id,
        __DEVICEMANAGER_IOCTL_EXT_WRITE,
        UHCI_USBLEGEACY,
        &ioctlValue,
        2
    );
    if (oserr != OS_EOK) {
        return NULL;
    }

    // Clear the Intel-specific register on Intel controllers.
    if (controller->Base.Device->VendorId == 0x8086) {
        ioctlValue = 0x00;
        oserr = IoctlDeviceEx(
            controller->Base.Device->Id,
            __DEVICEMANAGER_IOCTL_EXT_WRITE,
            UHCI_USBRES_INTEL,
            &ioctlValue,
            1
        );
        if (oserr != OS_EOK) {
            return NULL;
        }
    }

    // The bus device and interrupt handler must be ready before controller setup.
    oserr = UhciSetup(controller);
    if (oserr != OS_EOK) {
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }

    if (!UsbManagerCompleteControllerInitialization(&controller->Base)) {
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }
    return &controller->Base;
}

oserr_t
HCIControllerDestroy(
    _In_ UsbManagerController_t* base)
{
    UhciController_t* controller = (UhciController_t*)base;
    oserr_t status;

    if (!UsbManagerBeginControllerDestroy(base)) {
        return OS_EBUSY;
    }
    if (base->IoBase != NULL) {
        UhciWrite16(controller, UHCI_REGISTER_INTR, 0);
        status = UhciStop(controller);
        if (status != OS_EOK) {
            goto retry;
        }
        base->State.DmaStopped = true;
    }

    // Stop the controller and disconnect the handler before freeing their memory.
    // Leave the controller in the manager's list if cleanup fails, so the next
    // attempt can release the remaining resources.
    status = DeviceInterruptDestroy(&base->Interrupt);
    if (status != OS_EOK) {
        goto retry;
    }

    status = UHCIQueueDestroy(controller);
    if (status != OS_EOK) {
        goto retry;
    }

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
    WARNING("UHCI: controller cleanup failed (%u), will retry", status);
    UsbManagerRetryControllerDestroy(base);
    return status;
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

    // Preserve the current command bits when setting the start flags.
    cmd = UhciRead16(Controller, UHCI_REGISTER_COMMAND);
    cmd |= (UHCI_COMMAND_CONFIGFLAG | UHCI_COMMAND_RUN | UHCI_COMMAND_MAXPACKET64);
    dma_wmb();
    Controller->Base.State.DmaStopped = false;
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
    _In_ UhciController_t* controller)
{
    uint16_t command;
    int      hung;

    command = UhciRead16(controller, UHCI_REGISTER_COMMAND);
    UhciWrite16(controller, UHCI_REGISTER_COMMAND, command & ~UHCI_COMMAND_RUN);

    // Clearing RUN requests a stop. Wait for HALTED before freeing DMA memory,
    // since the last bus transaction must finish first.
    WaitForConditionWithFault(hung,
        (UhciRead16(controller, UHCI_REGISTER_STATUS) & UHCI_STATUS_HALTED) != 0, 100, 1);
    if (hung) {
        WARNING("UHCI: controller did not halt; keeping DMA resources");
        return OS_ETIMEOUT;
    }

    controller->Base.State.DmaStopped = true;
    return OS_EOK;
}

oserr_t
UhciReset(
    _In_ UhciController_t*  Controller)
{
    uint16_t tmp;

    // Assert the host-controller reset bit
    UhciWrite16(Controller, UHCI_REGISTER_COMMAND, UHCI_COMMAND_HCRESET);

    // Wait for the controller to clear HCRESET.
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

    // Restore the frame timing and schedule after reset.
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

    // Hold the global reset for 100 ms before configuring the controller.
    UhciWrite16(controller, UHCI_REGISTER_COMMAND, UHCI_COMMAND_GRESET);
    thrd_sleep(&(struct timespec) { .tv_nsec = 100 * NSEC_PER_MSEC }, NULL);
    UhciWrite16(controller, UHCI_REGISTER_COMMAND, 0x0000);

    // Use the same DMA address limit for queue pools and client requests.
    oserr = DMAOpen(controller->Base.Device->Id, INT32_MAX, &controller->Base.DmaContext);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Initialize queues and controller
    oserr = UhciQueueInitialize(controller);
    if (oserr != OS_EOK) {
        ERROR("UhciSetup: failed to initialize memory for queues");
        return oserr;
    }

    oserr = UhciReset(controller);
    if (oserr != OS_EOK) {
        ERROR("UhciSetup: failed to reset controller");
        return oserr;
    }

    // Enumerate all available ports
    for (i = 0; i <= UHCI_MAX_PORTS; i++) {
        uint16_t base = UhciRead16(controller, (UHCI_REGISTER_PORT_BASE + (i * 2)));
        if (!(base & UHCI_PORT_RESERVED) || base == 0xFFFF) {
            // Valid port registers must have the reserved bit set.
            // UHCI requires at least two ports.
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

    oserr = UhciStart(controller, 1);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Firmware may have cleared the connection-change bit while enumerating
    // the keyboard. Report connected ports once after the new schedule starts.
    for (i = 0; i < controller->Base.PortCount; i++) {
        if (UhciRead16(controller, UHCI_REGISTER_PORT_BASE + i * 2) & UHCI_PORT_CONNECT_STATUS) {
            UsbEventPort(controller->Base.Device->Id, (uint8_t)i);
        }
    }
    return OS_EOK;
}
