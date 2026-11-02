/**
 * Copyright 2026, Philip Meulengracht
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
 */

//#define __TRACE

#include <ddk/interrupts/interrupt.h>
#include <ddk/io.h>
#include <ddk/utils.h>
#include <os/device.h>
#include <os/handle.h>
#include <os/shm.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include "xhci.h"

irqstatus_t OnFastInterrupt(InterruptFunctionTable_t*, InterruptResourceTable_t*);

oserr_t
XhciHalt(
        _In_ XhciController_t* controller)
{
    reg32_t command;
    int     fault;

    TRACE("XhciHalt()");
    command = READ_VOLATILE(controller->OpRegisters->UsbCommand);
    command &= ~(XHCI_OP_USBCMD_RUN | XHCI_OP_USBCMD_INTERRUPTER);
    WRITE_VOLATILE(controller->OpRegisters->UsbCommand, command);

    WaitForConditionWithFault(fault,
            (READ_VOLATILE(controller->OpRegisters->UsbStatus) & XHCI_OP_USBSTS_HALTED) != 0,
            250,
            10);
    return fault ? OS_ETIMEOUT : OS_EOK;
}

oserr_t
XhciReset(
        _In_ XhciController_t* controller)
{
    reg32_t command;
    int     fault;

    TRACE("XhciReset()");
    if (controller->OpRegisters != NULL && XhciHalt(controller) != OS_EOK) {
        return OS_ETIMEOUT;
    }

    command = READ_VOLATILE(controller->OpRegisters->UsbCommand);
    command |= XHCI_OP_USBCMD_HCRESET;
    WRITE_VOLATILE(controller->OpRegisters->UsbCommand, command);

    WaitForConditionWithFault(fault,
            (READ_VOLATILE(controller->OpRegisters->UsbCommand) & XHCI_OP_USBCMD_HCRESET) == 0,
            250,
            10);
    if (fault) {
        return OS_ETIMEOUT;
    }

    WaitForConditionWithFault(fault,
            (READ_VOLATILE(controller->OpRegisters->UsbStatus) & XHCI_OP_USBSTS_NOT_READY) == 0,
            250,
            10);
    return fault ? OS_ETIMEOUT : OS_EOK;
}

static oserr_t
__XhciDisableLegacySupport(
    _In_ XhciController_t* controller)
{
    uintptr_t         registerBase = (uintptr_t)controller->CapRegisters;
    unsigned int      offset = XHCI_HCCPARAMS1_XECP(controller->HccParams1);
    volatile reg32_t* capability;
    reg32_t           value;
    unsigned int      next;
    int               fault;

    while (offset != 0) {
        // Integration registers outside the xHCI window are not capabilities.
        if ((uint64_t)offset * 4 + sizeof(reg32_t) > controller->RegisterLength) {
            return OS_ENOTSUPPORTED;
        }
        
        capability = (volatile reg32_t*)(registerBase + ((size_t)offset * 4));
        value = READ_VOLATILE(*capability);
        next = XHCI_EXTCAP_NEXT(value);

        if (XHCI_EXTCAP_ID(value) == XHCI_EXTCAP_LEGACY_SUPPORT) {
            if ((uint64_t)offset * 4 + 2 * sizeof(reg32_t) > controller->RegisterLength) {
                return OS_ENOTSUPPORTED;
            }

            // Request ownership from firmware. BIOS Owned should clear once OS
            // Owned is set; clearing the following control/status register then
            // suppresses legacy SMIs for the controller.
            WRITE_VOLATILE(*capability, value | XHCI_LEGSUP_OS_OWNED);
            WaitForConditionWithFault(fault,
                    (READ_VOLATILE(*capability) & XHCI_LEGSUP_BIOS_OWNED) == 0,
                    250,
                    10);
            if (fault) {
                WARNING("XHCI: firmware did not release ownership before reset.");
            }
            WRITE_VOLATILE(*(volatile reg32_t*)((uintptr_t)capability + sizeof(reg32_t)), 0);
            return OS_EOK;
        }

        offset = next ? offset + next : 0;
    }
    return OS_EOK;
}

oserr_t
XhciRun(
        _In_ XhciController_t* controller)
{
    reg32_t command;
    int     fault;

    TRACE("XhciRun()");

    dma_wmb();
    WRITE_VOLATILE(controller->OpRegisters->Configure, XHCI_OP_CONFIG_MAXSLOTS(controller->SlotCount));
    WRITE_VOLATILE(controller->OpRegisters->DeviceContextBaseAddressArray, controller->DCBaaDeviceAddress);
    WRITE_VOLATILE(controller->OpRegisters->CommandRingControl,
            controller->CommandRing.DeviceBase | XHCI_OP_CRCR_RING_CYCLE);

    WRITE_VOLATILE(controller->InterrupterRegisters->EventRingSegmentTableSize, XHCI_ERST_ENTRIES);
    WRITE_VOLATILE(controller->InterrupterRegisters->EventRingDequeuePointer,
            controller->EventRingDeviceAddress | XHCI_INTERRUPTER_ERDP_BUSY);
    WRITE_VOLATILE(controller->InterrupterRegisters->EventRingSegmentTableBaseAddress,
            controller->ErstDeviceAddress);
    WRITE_VOLATILE(controller->InterrupterRegisters->Management, XHCI_INTERRUPTER_MANAGEMENT_ENABLE);

    command = READ_VOLATILE(controller->OpRegisters->UsbCommand);
    command |= XHCI_OP_USBCMD_RUN | XHCI_OP_USBCMD_INTERRUPTER;
    WRITE_VOLATILE(controller->OpRegisters->UsbCommand, command);

    WaitForConditionWithFault(fault,
            (READ_VOLATILE(controller->OpRegisters->UsbStatus) & XHCI_OP_USBSTS_HALTED) == 0,
            250,
            10);
    return fault ? OS_ETIMEOUT : OS_EOK;
}

static oserr_t
__SetupController(
        _In_ XhciController_t* controller)
{
    oserr_t oserr;

    controller->HcsParams1 = READ_VOLATILE(controller->CapRegisters->HcsParams1);
    controller->HcsParams2 = READ_VOLATILE(controller->CapRegisters->HcsParams2);
    controller->HccParams1 = READ_VOLATILE(controller->CapRegisters->HccParams1);
    controller->SlotCount = XHCI_HCSPARAMS1_MAXSLOTS(controller->HcsParams1);
    controller->Base.PortCount = XHCI_HCSPARAMS1_MAXPORTS(controller->HcsParams1);
    controller->ScratchpadCount = XHCI_HCSPARAMS2_MAXSCRATCHPADS(controller->HcsParams2);
    controller->ContextSize = (controller->HccParams1 & XHCI_HCCPARAMS1_CSZ) ? 64 : 32;

    if (controller->Base.PortCount == 0) {
        ERROR("XHCI-Failure: unsupported port count %u", (unsigned int)controller->Base.PortCount);
        return OS_EUNKNOWN;
    }
    if (controller->Base.PortCount > USB_MAX_PORTS) {
        WARNING("XHCI: limiting exposed root ports from %u to %u",
                (unsigned int)controller->Base.PortCount, (unsigned int)USB_MAX_PORTS);
        controller->Base.PortCount = USB_MAX_PORTS;
    }

    controller->Base.IORequirements.BufferAlignment = XHCI_CONTEXT_ALIGNMENT;
    controller->Base.IORequirements.Conformity = (controller->HccParams1 & XHCI_HCCPARAMS1_AC64) ?
            OSMEMORYCONFORMITY_BITS64 : OSMEMORYCONFORMITY_BITS32;

    oserr = __XhciDisableLegacySupport(controller);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = XhciReset(controller);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Address width belongs to the controller, even when the driver itself
    // uses 32-bit pointers. The context applies this limit to device addresses.
    oserr = DMAOpen(controller->Base.Device->Id,
        (controller->HccParams1 & XHCI_HCCPARAMS1_AC64) ? UINT64_MAX : UINT32_MAX,
        &controller->Base.DmaContext);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = XhciQueueInitialize(controller);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = DeviceActivate(controller->Base.Device->Id, DeviceStart);
    if (oserr != OS_EOK) {
        return oserr;
    }
    oserr = XhciRun(controller);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = UsbManagerRegisterController(&controller->Base);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // xHCI root ports are self-describing. Power any port that supports software
    // port power control, then report already-connected ports to usbd so the hub
    // logic can query and reset them through the common HCI callbacks.
    for (size_t i = 0; i < controller->Base.PortCount; i++) {
        reg32_t status = READ_VOLATILE(controller->Ports[i].StatusControl);
        WRITE_VOLATILE(controller->Ports[i].StatusControl,
                (status & XHCI_PORT_STATUS_RW_MASK) | XHCI_PORT_STATUS_POWER | XHCI_PORT_STATUS_CHANGE_BITS);
    }
    thrd_sleep(&(struct timespec) { .tv_nsec = 20 * NSEC_PER_MSEC }, NULL);
    XhciPortScan(controller, true);
    return OS_EOK;
}

UsbManagerController_t*
HCIControllerCreate(
    _In_ Device_t* device)
{
    XhciController_t*      controller;
    struct DeviceResources resources;
    uintptr_t              base;
    size_t                 length;
    uint32_t               operational;
    uint32_t               runtime;
    uint32_t               doorbells;
    uint32_t               ports;
    unsigned int           i;
    oserr_t                status;

    controller = (XhciController_t*)UsbManagerCreateController(device,
        USBCONTROLLER_KIND_XHCI, sizeof(*controller));
    if (controller == NULL) {
        return NULL;
    }
    controller->State = XHCI_CONTROLLER_RUNNING;
    
    list_construct(&controller->XhciEndpoints);
    list_construct(&controller->Devices);
    
    status = DeviceActivate(device->Id, DevicePrepare);
    if (status == OS_EOK) {
        controller->Prepared = true;
        status = DeviceGetResources(device->Id, &resources);
    }
    
    if (status != OS_EOK) {
        goto error;
    }

    for (i = 0; i < resources.Count; i++) {
        if (resources.Registers[i].Type == DeviceIoMemoryBased) {
            controller->RegisterIo = resources.Registers[i];
            break;
        }
    }
    if (i == resources.Count) {
        goto error;
    }
    
    status = AcquireDeviceIo(&controller->RegisterIo);
    if (status != OS_EOK) {
        goto error;
    }
    
    controller->Base.IoBase = &controller->RegisterIo;
    base = controller->RegisterIo.Access.Memory.VirtualBase;
    length = controller->RegisterIo.Access.Memory.Length;

    if (Dwc3IsDevice(device)) {
        status = Dwc3Prepare(
            &controller->Dwc3,
            &resources,
            &controller->RegisterIo,
            &length
        );
        if (status != OS_EOK) {
            goto error;
        }
    }

    controller->RegisterLength = length;
    if (length < sizeof(XhciCapabilityRegisters_t)) {
        goto error;
    }
    
    controller->CapRegisters = (XhciCapabilityRegisters_t*)base;
    
    operational = READ_VOLATILE(controller->CapRegisters->CapLength);
    runtime = READ_VOLATILE(controller->CapRegisters->RuntimeOffset) & ~0x1fU;
    doorbells = READ_VOLATILE(controller->CapRegisters->DoorbellOffset) & ~3U;
    ports = XHCI_HCSPARAMS1_MAXPORTS(READ_VOLATILE(controller->CapRegisters->HcsParams1));
    
    if (operational < sizeof(XhciCapabilityRegisters_t) ||
        (uint64_t)operational + 0x400 + ports * sizeof(XhciPortRegisters_t) > length) {
        goto error;
    }
    
    if ((uint64_t)runtime + 0x20 + sizeof(XhciInterrupterRegisters_t) > length ||
        (uint64_t)doorbells + 256 * sizeof(reg32_t) > length) {
        goto error;
    }
    
    controller->OpRegisters = (XhciOperationalRegisters_t*)(base + operational);
    controller->RuntimeRegisters = (XhciRuntimeRegisters_t*)(base + runtime);
    controller->InterrupterRegisters = (XhciInterrupterRegisters_t*)(base + runtime + 0x20);
    controller->Doorbells = (volatile reg32_t*)(base + doorbells);
    controller->Ports = (XhciPortRegisters_t*)(base + operational + 0x400);

    // Keep the peripheral quiet while the kernel binds the common fast handler.
    WRITE_VOLATILE(controller->OpRegisters->UsbCommand,
        READ_VOLATILE(controller->OpRegisters->UsbCommand) & ~XHCI_OP_USBCMD_INTERRUPTER);
    
    status = UsbManagerConfigureInterrupt(
        &controller->Base,
        (InterruptHandler_t)OnFastInterrupt,
        sizeof(XhciController_t)
    );
    if (status == OS_EOK) {
        status = DeviceInterruptEnable(&controller->Base.Interrupt);
    }

    if (status == OS_EOK) {
        status = __SetupController(controller);
    }
    
    if (status != OS_EOK) {
        goto error;
    }
    return &controller->Base;

error:
    HCIControllerDestroy(&controller->Base);
    return NULL;
}

oserr_t
HCIControllerDestroy(
    _In_ UsbManagerController_t* baseController)
{
    XhciController_t* controller = (XhciController_t*)baseController;
    oserr_t           status;

    // Register the retry owner before the first fallible step, including failed
    // initialization. The manager removes it only after all releases succeed.
    // Provider IPC can yield to another unregister request or monitor tick.
    if (controller->Destroying) {
        return OS_EBUSY;
    }

    controller->Destroying = true;
    if (controller->State == XHCI_CONTROLLER_RUNNING) {
        controller->State = XHCI_CONTROLLER_STOPPING;
    }
    
    UsbManagerDeferControllerDestroy(baseController);
    if (controller->State != XHCI_CONTROLLER_HALTED) {
        if (controller->OpRegisters != NULL) {
            status = XhciHalt(controller);
            if (status != OS_EOK) {
                ERROR("XHCI teardown: halt failed (%u); keeping hardware resources", status);
                goto retry;
            }
        }
        controller->State = XHCI_CONTROLLER_HALTED;
    }

    // Halting the engine ends DMA. Keep provider activation alive while removing
    // interrupts and DMA mappings: native platforms cannot release their power
    // and translation resources while these leases exist. No later retry may
    // touch MMIO after activation has been released.
    status = DeviceInterruptDestroy(&baseController->Interrupt);
    if (status != OS_EOK) {
        WARNING("XHCI teardown: interrupt release failed (%u); retry pending", status);
        goto retry;
    }

    status = XhciQueueDestroy(controller);
    if (status != OS_EOK) {
        WARNING("XHCI teardown: queue release failed (%u); retry pending", status);
        goto retry;
    }
    
    status = DMAClose(&baseController->DmaContext);
    if (status != OS_EOK) {
        goto retry;
    }
    
    // Wrapper restoration also needs live MMIO. A partial preparation or failed
    // restoration keeps its state until this succeeds, before provider stop.
    status = Dwc3Release(&controller->Dwc3);
    if (status != OS_EOK) {
        WARNING("XHCI teardown: DWC3 release failed (%u); retry pending", status);
        goto retry;
    }
    
    if (controller->Prepared) {
        status = DeviceActivate(baseController->Device->Id, DeviceStop);
        if (status != OS_EOK) {
            WARNING("XHCI teardown: activation release failed (%u); retry pending", status);
            goto retry;
        }
        controller->Prepared = false;
    }
    
    if (baseController->IoBase != NULL) {
        status = ReleaseDeviceIo(baseController->IoBase);
        if (status != OS_EOK) {
            WARNING("XHCI teardown: register detach failed (%u); retry pending", status);
            goto retry;
        }
        baseController->IoBase = NULL;
    }
    
    TRACE("XHCI teardown: released controller %u", baseController->Device->Id);
    UsbManagerDestroyController(baseController);
    free(controller);
    return OS_EOK;

retry:
    controller->Destroying = false;
    return status;
}
