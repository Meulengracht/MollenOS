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

#include <ddk/interrupt.h>
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

static void
__XhciFreeDMA(
        _In_ OSHandle_t*   handle,
        _In_ SHMSGTable_t* table)
{
    OSHandleDestroy(handle);
    free(table->Entries);
    memset(table, 0, sizeof(SHMSGTable_t));
}

static oserr_t
__XhciAllocateDMA(
        _In_  size_t        size,
        _Out_ OSHandle_t*   handle,
        _Out_ SHMSGTable_t* table,
        _Out_ void**        bufferOut)
{
    oserr_t oserr;

    oserr = SHMCreate(
            &(SHM_t) {
                    .Flags = SHM_DEVICE | SHM_PRIVATE | SHM_CLEAN,
                    .Conformity = OSMEMORYCONFORMITY_BITS64,
                    .Size = size,
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE
            },
            handle
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = SHMGetSGTable(handle, table, -1);
    if (oserr != OS_EOK) {
        OSHandleDestroy(handle);
        return oserr;
    }

    *bufferOut = SHMBuffer(handle);
    return OS_EOK;
}

static void
__XhciFreeController(
        _In_ XhciController_t* controller)
{
    UsbManagerDestroyController(&controller->Base);
    free(controller);
}

oserr_t
XhciQueueInitialize(
        _In_ XhciController_t* controller)
{
    size_t  dcbaaEntries = MIN(controller->SlotCount + 1, XHCI_MAX_DEVICE_CONTEXTS);
    size_t  scratchpadBytes;
    oserr_t oserr;

    TRACE("XhciQueueInitialize()");

    // The DCBAA is the anchor the controller uses to discover every device
    // context. Entry 0 is special: when scratchpad buffers are required by the
    // hardware it points to the scratchpad-buffer pointer array.
    oserr = __XhciAllocateDMA(
            dcbaaEntries * sizeof(reg64_t),
            &controller->DCBaaDMA,
            &controller->DCBaaDMATable,
            (void**)&controller->DCBaa
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    if (controller->ScratchpadCount > 0) {
        oserr = __XhciAllocateDMA(
                controller->ScratchpadCount * sizeof(reg64_t),
                &controller->ScratchpadArrayDMA,
                &controller->ScratchpadArrayDMATable,
                (void**)&controller->ScratchpadArray
        );
        if (oserr != OS_EOK) {
            return oserr;
        }

        scratchpadBytes = controller->ScratchpadCount * XHCI_PAGE_SIZE;
        void* scratchpadBuffer;
        oserr = __XhciAllocateDMA(
                scratchpadBytes,
                &controller->ScratchpadBufferDMA,
                &controller->ScratchpadBufferDMATable,
                &scratchpadBuffer
        );
        if (oserr != OS_EOK) {
            return oserr;
        }

        for (size_t i = 0; i < controller->ScratchpadCount; i++) {
            controller->ScratchpadArray[i] = controller->ScratchpadBufferDMATable.Entries[0].Address + (i * XHCI_PAGE_SIZE);
        }
        controller->DCBaa[0] = controller->ScratchpadArrayDMATable.Entries[0].Address;
    }

    oserr = __XhciAllocateDMA(
            XHCI_COMMAND_RING_ENTRIES * sizeof(XhciTransferRequestBlock_t),
            &controller->CommandRingDMA,
            &controller->CommandRingDMATable,
            (void**)&controller->CommandRing
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    // The final TRB links the command ring back to itself. This creates the
    // command-ring cycle required by xHCI section 4.9 without yet submitting
    // any commands.
    controller->CommandRing[XHCI_COMMAND_RING_ENTRIES - 1].Parameter = controller->CommandRingDMATable.Entries[0].Address;
    controller->CommandRing[XHCI_COMMAND_RING_ENTRIES - 1].Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_LINK) | XHCI_TRB_CONTROL_CYCLE;

    oserr = __XhciAllocateDMA(
            XHCI_EVENT_RING_ENTRIES * sizeof(XhciTransferRequestBlock_t),
            &controller->EventRingDMA,
            &controller->EventRingDMATable,
            (void**)&controller->EventRing
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __XhciAllocateDMA(
            XHCI_ERST_ENTRIES * sizeof(XhciEventRingSegmentTableEntry_t),
            &controller->ErstDMA,
            &controller->ErstDMATable,
            (void**)&controller->Erst
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    controller->Erst[0].RingSegmentBaseAddress = controller->EventRingDMATable.Entries[0].Address;
    controller->Erst[0].RingSegmentSize = XHCI_EVENT_RING_ENTRIES;
    return OS_EOK;
}

void
XhciQueueDestroy(
        _In_ XhciController_t* controller)
{
    TRACE("XhciQueueDestroy()");
    __XhciFreeDMA(&controller->ErstDMA, &controller->ErstDMATable);
    __XhciFreeDMA(&controller->EventRingDMA, &controller->EventRingDMATable);
    __XhciFreeDMA(&controller->CommandRingDMA, &controller->CommandRingDMATable);
    __XhciFreeDMA(&controller->ScratchpadBufferDMA, &controller->ScratchpadBufferDMATable);
    __XhciFreeDMA(&controller->ScratchpadArrayDMA, &controller->ScratchpadArrayDMATable);
    __XhciFreeDMA(&controller->DCBaaDMA, &controller->DCBaaDMATable);
}

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
    if (XhciHalt(controller) != OS_EOK) {
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

oserr_t
XhciRun(
        _In_ XhciController_t* controller)
{
    reg32_t command;
    int     fault;

    TRACE("XhciRun()");

    WRITE_VOLATILE(controller->OpRegisters->Configure, XHCI_OP_CONFIG_MAXSLOTS(controller->SlotCount));
    WRITE_VOLATILE(controller->OpRegisters->DeviceContextBaseAddressArray, controller->DCBaaDMATable.Entries[0].Address);
    WRITE_VOLATILE(controller->OpRegisters->CommandRingControl,
            controller->CommandRingDMATable.Entries[0].Address | XHCI_OP_CRCR_RING_CYCLE);

    WRITE_VOLATILE(controller->InterrupterRegisters->EventRingSegmentTableSize, XHCI_ERST_ENTRIES);
    WRITE_VOLATILE(controller->InterrupterRegisters->EventRingSegmentTableBaseAddress,
            controller->ErstDMATable.Entries[0].Address);
    WRITE_VOLATILE(controller->InterrupterRegisters->EventRingDequeuePointer,
            controller->EventRingDMATable.Entries[0].Address | XHCI_INTERRUPTER_ERDP_BUSY);
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

static DeviceIo_t*
__GetControllerIoSpace(
        _In_ BusDevice_t* device)
{
    for (int i = 0; i < __DEVICEMANAGER_MAX_IOSPACES; i++) {
        if (device->IoSpaces[i].Type == DeviceIoMemoryBased) {
            return &device->IoSpaces[i];
        }
    }
    return NULL;
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

    if (controller->Base.PortCount == 0 || controller->Base.PortCount > USB_MAX_PORTS) {
        ERROR("XHCI-Failure: unsupported port count %u", controller->Base.PortCount);
        return OS_EUNKNOWN;
    }

    controller->Base.IORequirements.BufferAlignment = XHCI_CONTEXT_ALIGNMENT;
    controller->Base.IORequirements.Conformity = (controller->HccParams1 & XHCI_HCCPARAMS1_AC64) ?
            OSMEMORYCONFORMITY_BITS64 : OSMEMORYCONFORMITY_BITS32;

    oserr = XhciReset(controller);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = XhciQueueInitialize(controller);
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
    XhciPortScan(controller);
    return OS_EOK;
}

UsbManagerController_t*
HCIControllerCreate(
        _In_ BusDevice_t* device)
{
    XhciController_t* controller;
    DeviceInterrupt_t interrupt;
    DeviceIo_t*       ioBase;
    uintptr_t         registerBase;
    oserr_t           oserr;

    controller = (XhciController_t*)UsbManagerCreateController(device, USBCONTROLLER_KIND_XHCI, sizeof(XhciController_t));
    if (!controller) {
        return NULL;
    }

    ioBase = __GetControllerIoSpace(device);
    if (!ioBase) {
        ERROR("No memory space found for xhci-controller");
        __XhciFreeController(controller);
        return NULL;
    }

    oserr = AcquireDeviceIo(ioBase);
    if (oserr != OS_EOK) {
        ERROR("Failed to acquire the io-space for xhci-controller");
        __XhciFreeController(controller);
        return NULL;
    }

    controller->Base.IoBase = ioBase;
    registerBase = ioBase->Access.Memory.VirtualBase;
    controller->CapRegisters = (XhciCapabilityRegisters_t*)registerBase;
    controller->OpRegisters = (XhciOperationalRegisters_t*)(registerBase + controller->CapRegisters->CapLength);
    controller->RuntimeRegisters = (XhciRuntimeRegisters_t*)(registerBase + (controller->CapRegisters->RuntimeOffset & ~0x1FUL));
    controller->InterrupterRegisters = (XhciInterrupterRegisters_t*)((uintptr_t)controller->RuntimeRegisters + 0x20);
    controller->Doorbells = (volatile reg32_t*)(registerBase + (controller->CapRegisters->DoorbellOffset & ~0x3UL));
    controller->Ports = (XhciPortRegisters_t*)((uintptr_t)controller->OpRegisters + 0x400);

    DeviceInterruptInitialize(&interrupt, device);
    RegisterFastInterruptHandler(&interrupt, (InterruptHandler_t)OnFastInterrupt);
    RegisterFastInterruptIoResource(&interrupt, ioBase);
    RegisterFastInterruptMemoryResource(&interrupt, (uintptr_t)controller, sizeof(XhciController_t), 0);
    RegisterInterruptDescriptor(&interrupt, controller->Base.event_descriptor);
    controller->Base.Interrupt = RegisterInterruptSource(&interrupt, 0);

    oserr = OSDeviceIOCtl(
            controller->Base.Device->Base.Id,
            OSIOCTLREQUEST_BUS_CONTROL,
            &(struct OSIOCtlBusControl) {
                    .Flags = (__DEVICEMANAGER_IOCTL_ENABLE | __DEVICEMANAGER_IOCTL_MMIO_ENABLE |
                              __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE)
            },
            sizeof(struct OSIOCtlBusControl)
    );
    if (oserr != OS_EOK) {
        ERROR("Failed to enable the xhci-controller");
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }

    if (__SetupController(controller) != OS_EOK) {
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }
    return &controller->Base;
}

void
HCIControllerDestroy(
        _In_ UsbManagerController_t* baseController)
{
    XhciController_t* controller = (XhciController_t*)baseController;

    TRACE("HCIControllerDestroy()");
    UsbManagerDestroyController(baseController);
    XhciHalt(controller);
    XhciQueueDestroy(controller);
    UnregisterInterruptSource(baseController->Interrupt);
    ReleaseDeviceIo(baseController->IoBase);
    free(controller);
}

void
HciTimerCallback(
        _In_ UsbManagerController_t* baseController)
{
    _CRT_UNUSED(baseController);
}
