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
 * MollenOS MCore - Advanced Host Controller Interface Driver
 * TODO:
 *    - Port Multiplier Support
 *    - Power Management
 */

//#define __TRACE

#include <ddk/busdevice.h>
#include <ddk/utils.h>
#include <os/device.h>
#include <threads.h>
#include <stdlib.h>
#include <event.h>
#include <ioset.h>
#include <gracht/server.h>
#include <ioctl.h>
#include "ahci.h"

extern gracht_server_t* __crt_get_module_server(void);

// Prototypes
irqstatus_t OnFastInterrupt(InterruptFunctionTable_t*, InterruptResourceTable_t*);
oserr_t        AhciSetup(AhciController_t* controller);

AhciController_t*
AhciControllerCreate(
    _In_ BusDevice_t* busDevice)
{
    AhciController_t* controller;
    DeviceInterrupt_t interrupt;
    DeviceIo_t*       ioBase = NULL;
    oserr_t           osStatus;
    int               i;
    int               opt = 1;

    controller = (AhciController_t*)malloc(sizeof(AhciController_t));
    if (!controller) {
        return NULL;
    }
    
    memset(controller, 0, sizeof(AhciController_t));
    spinlock_init(&controller->Lock);
    ELEMENT_INIT(&controller->header, (void*)(uintptr_t)busDevice->Base.Id, controller);
    controller->Device = busDevice;

    // Create the event descriptor used to receive irqs
    controller->event_descriptor = eventd(0, EVT_RESET_EVENT);
    if (controller->event_descriptor < 0) {
        free(controller);
        ERROR("[AhciControllerCreate] failed to create event descriptor");
        return NULL;
    }

    // register it with the io set
    ioset_ctrl(gracht_server_get_aio_handle(__crt_get_module_server()),
               IOSET_ADD, controller->event_descriptor,
               &(struct ioset_event) { .data.context = controller, .events = IOSETSYN });
    ioctl(controller->event_descriptor, FIONBIO, &opt);

    // Get I/O Base, and for AHCI there might be between 1-5
    // IO-spaces filled, so we always, ALWAYS go for the last one
    for (i = __DEVICEMANAGER_MAX_IOSPACES - 1; i >= 0; i--) {
        if (controller->Device->IoSpaces[i].Type == DeviceIoMemoryBased) {
            ioBase = &controller->Device->IoSpaces[i];
            break;
        }
    }

    // Sanitize that we found the io-space
    if (ioBase == NULL) {
        ERROR("No memory space found for ahci-controller");
        free(controller);
        return NULL;
    }
    TRACE("Found Io-Space (Type %u, Physical 0x%" PRIxIN ", Size 0x%" PRIxIN ")",
          ioBase->Type, ioBase->Access.Memory.PhysicalBase, ioBase->Access.Memory.Length);

    // Acquire the io-space
    osStatus = AcquireDeviceIo(ioBase);
    if (osStatus != OS_EOK) {
        ERROR("Failed to create and acquire the io-space for ahci-controller");
        free(controller);
        return NULL;
    }

    controller->IoBase = ioBase;
    TRACE("Io-Space was assigned virtual address 0x%" PRIxIN, ioBase->Access.Memory.VirtualBase);
    controller->Registers = (AHCIGenericRegisters_t*)ioBase->Access.Memory.VirtualBase;
    
    DeviceInterruptInitialize(&interrupt, busDevice);
    RegisterInterruptDescriptor(&interrupt, controller->event_descriptor);
    RegisterFastInterruptHandler(&interrupt, (InterruptHandler_t)OnFastInterrupt);
    RegisterFastInterruptIoResource(&interrupt, ioBase);
    RegisterFastInterruptMemoryResource(&interrupt,
                                        (uintptr_t)&controller->InterruptResource,
                                        sizeof(AhciInterruptResource_t), 0);

    // Register interrupt
    TRACE(" > ahci interrupt line is %u", interrupt.Line);
    controller->InterruptId = RegisterInterruptSource(&interrupt, 0);

    // Enable device
    osStatus = OSDeviceIOCtl(
            controller->Device->Base.Id,
            OSIOCTLREQUEST_BUS_CONTROL,
            &(struct OSIOCtlBusControl) {
                    .Flags = (__DEVICEMANAGER_IOCTL_ENABLE | __DEVICEMANAGER_IOCTL_MMIO_ENABLE |
                              __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE)
            }, sizeof(struct OSIOCtlBusControl)
    );
    if (osStatus != OS_EOK || controller->InterruptId == UUID_INVALID) {
        ERROR("Failed to enable the ahci-controller");
        UnregisterInterruptSource(controller->InterruptId);
        ReleaseDeviceIo(controller->IoBase);
        free(controller);
        return NULL;
    }

    // Now that all formalities has been taken care
    // off we can actually setup controller
    if (AhciSetup(controller) == OS_EOK) {
        return controller;
    }
    else {
        AhciControllerDestroy(controller);
        return NULL;
    }
}

oserr_t
AhciControllerDestroy(
    _In_ AhciController_t* controller)
{
    int i;

    // First step is to clear out all ports
    // this releases devices and resources
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (controller->Ports[i]) {
            AhciPortCleanup(controller, controller->Ports[i]);
        }
    }

    UnregisterInterruptSource(controller->InterruptId);
    ReleaseDeviceIo(controller->IoBase);
    free(controller->Device);
    free(controller);
    return OS_EOK;
}

oserr_t
AhciReset(
    _In_ AhciController_t* controller)
{
    reg32_t ghc;
    reg32_t caps;
    int     hung;
    int     i;
    TRACE("AhciReset()");

    // Software may perform an HBA reset prior to initializing the controller by setting GHC.AE to 1 and then setting GHC.HR to 1 if desired
    ghc = READ_VOLATILE(controller->Registers->GlobalHostControl);
    WRITE_VOLATILE(controller->Registers->GlobalHostControl, ghc | AHCI_HOSTCONTROL_HR);

    // The bit shall be cleared to 0 by the HBA when the reset is complete. 
    // If the HBA has not cleared GHC.HR to 0 within 1 second of 
    // software setting GHC.HR to 1, the HBA is in a hung or locked state.
    WaitForConditionWithFault(hung,
                              ((READ_VOLATILE(controller->Registers->GlobalHostControl) & AHCI_HOSTCONTROL_HR) == 0),
                              10, 200);
    if (hung) {
        return OS_EUNKNOWN;
    }

    // If the HBA supports staggered spin-up, the PxCMD.SUD bit will be reset to 0; 
    // software is responsible for setting the PxCMD.SUD and PxSCTL.DET fields 
    // appropriately such that communication can be established on the Serial ATA link. 
    // If the HBA does not support staggered spin-up, the HBA reset shall cause 
    // a COMRESET to be sent on the port.

    // Indicate that system software is AHCI aware by setting GHC.AE to 1.
    caps = READ_VOLATILE(controller->Registers->Capabilities);
    if (!(caps & AHCI_CAPABILITIES_SAM)) {
        ghc = READ_VOLATILE(controller->Registers->GlobalHostControl);
        WRITE_VOLATILE(controller->Registers->GlobalHostControl, ghc | AHCI_HOSTCONTROL_AE);
    }

    // Ensure that the controller is not in the running state by reading and
    // examining each implemented ports PxCMD register
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!(controller->ValidPorts & AHCI_IMPLEMENTED_PORT(i))) {
            continue;
        }
        TRACE(" > port %i status after reset: 0x%x", i, controller->Ports[i]->Registers->CommandAndStatus);
    }
    return OS_EOK;
}

oserr_t
AhciTakeOwnership(
    _In_ AhciController_t* Controller)
{
    reg32_t osCtrl;
    int     hung;
    TRACE("AhciTakeOwnership()");

    // Step 1. Sets the OS Ownership (BOHC.OOS) bit to 1.
    osCtrl = READ_VOLATILE(Controller->Registers->OSControlAndStatus);
    WRITE_VOLATILE(Controller->Registers->OSControlAndStatus, osCtrl | AHCI_CONTROLSTATUS_OOS);

    // Wait 25 ms, to determine how long time BIOS needs to release
    thrd_sleep(&(struct timespec) { .tv_nsec = 25 * NSEC_PER_MSEC }, NULL);

    // If the BIOS Busy (BOHC.BB) has been set to 1 within 25 milliseconds, 
    // then the OS driver shall provide the BIOS a minimum of two seconds 
    // for finishing outstanding commands on the HBA.
    if (READ_VOLATILE(Controller->Registers->OSControlAndStatus) & AHCI_CONTROLSTATUS_BB) {
        thrd_sleep(&(struct timespec) { .tv_sec = 2 }, NULL);
    }

    // Step 2. Spin on the BIOS Ownership (BOHC.BOS) bit, waiting for it to be cleared to 0.
    WaitForConditionWithFault(hung,
                              ((READ_VOLATILE(Controller->Registers->OSControlAndStatus) & AHCI_CONTROLSTATUS_BOS) == 0),
                              10, 25);

    // Sanitize if we got the ownership 
    if (hung) {
        return OS_EUNKNOWN;
    }
    else {
        return OS_EOK;
    }
}

oserr_t
AhciSetup(
    _In_ AhciController_t* controller)
{
    reg32_t ghc;
    reg32_t capabilities;
    int     fullResetRequired = 0;
    int     activePortCount   = 0;
    int     i;
    TRACE("AhciSetup()");

    // Take ownership of the controller
    if (AhciTakeOwnership(controller) != OS_EOK) {
        ERROR("Failed to take ownership of the controller.");
        return OS_EUNKNOWN;
    }

    // Indicate that system software is AHCI aware by setting GHC.AE to 1.
    capabilities = READ_VOLATILE(controller->Registers->Capabilities);
    if (!(capabilities & AHCI_CAPABILITIES_SAM)) {
        ghc = READ_VOLATILE(controller->Registers->GlobalHostControl);
        WRITE_VOLATILE(controller->Registers->GlobalHostControl, ghc | AHCI_HOSTCONTROL_AE);
    }

    // Store whether we support 64 bit addressing.
    if (capabilities & AHCI_CAPABILITIES_S64A) {
        controller->Bits64 = true;
    }

    // Determine which ports are implemented by the HBA, by reading the PI register. 
    // This bit map value will aid software in determining how many ports are 
    // available and which port registers need to be initialized.
    controller->ValidPorts       = READ_VOLATILE(controller->Registers->PortsImplemented);
    controller->CommandSlotCount = AHCI_CAPABILITIES_NCS(capabilities);
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!(controller->ValidPorts & AHCI_IMPLEMENTED_PORT(i))) {
            continue;
        }
        controller->PortCount++;
    }

    if (controller->PortCount != AHCI_CAPABILITIES_NP(capabilities)) {
        WARNING("AhciSetup number of implemented ports != number of ports %u != %u",
                controller->PortCount, AHCI_CAPABILITIES_NP(capabilities));
    }

    TRACE("Port Validity Bitmap 0x%x, Capabilities 0x%x", controller->ValidPorts, capabilities);

    // Initialize ports
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!(controller->ValidPorts & AHCI_IMPLEMENTED_PORT(i))) {
            continue;
        }

        // Create a port descriptor and get register access
        controller->Ports[i] = AhciPortCreate(controller, activePortCount++, i);
        AhciPortInitiateSetup(controller, controller->Ports[i]);
        AhciPortRebase(controller, controller->Ports[i]);
    }

    // Finish the stop sequences
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (controller->Ports[i] != NULL) {
            if (AhciPortFinishSetup(controller, controller->Ports[i]) != OS_EOK) {
                ERROR(" > failed to initialize port %i", i);
                fullResetRequired = 1;
                break;
            }
        }
    }

    // Perform full reset if required here
    if (fullResetRequired) {
        if (AhciReset(controller) != OS_EOK) {
            ERROR("Failed to initialize the AHCI controller, aborting");
            return OS_EUNKNOWN;
        }
    }

    // To enable the HBA to generate interrupts, 
    // system software must also set GHC.IE to a 1
    ghc = READ_VOLATILE(controller->Registers->GlobalHostControl);
    WRITE_VOLATILE(controller->Registers->InterruptStatus, 0xFFFFFFFF);
    WRITE_VOLATILE(controller->Registers->GlobalHostControl, ghc | AHCI_HOSTCONTROL_IE);
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (controller->Ports[i] != NULL) {
            if (AhciPortStart(controller, controller->Ports[i]) != OS_EOK) {
                ERROR(" > failed to start port %i", i);
            }
        }
    }
    return OS_EOK;
}
