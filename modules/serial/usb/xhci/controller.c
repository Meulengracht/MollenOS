/**
 * Copyright 2026, Philip Meulengracht
 */

#include <ddk/interrupt.h>
#include <ddk/utils.h>
#include <os/device.h>
#include <stdlib.h>
#include <string.h>
#include "xhci.h"

irqstatus_t
OnFastInterrupt(
    InterruptFunctionTable_t* functionTable,
    InterruptResourceTable_t* resourceTable);

static oserr_t
__AcquireIOSpace(
    _In_ XhciController_t* controller)
{
    DeviceIo_t* ioBase = NULL;

    for (int i = 0; i < __DEVICEMANAGER_MAX_IOSPACES; i++) {
        if (controller->Base.Device->IoSpaces[i].Type == DeviceIoMemoryBased) {
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
    _In_ XhciController_t* controller)
{
    DeviceInterrupt_t deviceInterrupt;

    DeviceInterruptInitialize(&deviceInterrupt, busDevice);
    RegisterInterruptDescriptor(&deviceInterrupt, controller->Base.event_descriptor);
    RegisterFastInterruptHandler(&deviceInterrupt, (InterruptHandler_t)OnFastInterrupt);
    RegisterFastInterruptIoResource(&deviceInterrupt, controller->Base.IoBase);
    RegisterFastInterruptMemoryResource(&deviceInterrupt, (uintptr_t)controller, sizeof(XhciController_t), 0);
    controller->Base.Interrupt = RegisterInterruptSource(&deviceInterrupt, 0);
    return controller->Base.Interrupt == UUID_INVALID ? OS_EUNKNOWN : OS_EOK;
}

static oserr_t
XhciSetup(
    _In_ XhciController_t* controller)
{
    controller->MaxDeviceSlots = XHCI_HCSPARAMS1_MAXSLOTS(controller->CapRegisters->HcsParams1);
    controller->Base.PortCount = XHCI_HCSPARAMS1_MAXPORTS(controller->CapRegisters->HcsParams1);
    if (controller->Base.PortCount > XHCI_MAX_PORTS) {
        controller->Base.PortCount = XHCI_MAX_PORTS;
    }
    controller->ContextSize    = (controller->CapRegisters->HccParams1 & XHCI_HCCPARAMS1_CSZ) ? 64 : 32;
    controller->PortRegisters  = (volatile XhciPortRegisters_t*)((uintptr_t)controller->OpRegisters + 0x400);
    controller->Doorbells      = (volatile reg32_t*)((uintptr_t)controller->Base.IoBase->Access.Memory.VirtualBase + controller->CapRegisters->DbOff);
    controller->RuntimeRegisters = (volatile uint8_t*)((uintptr_t)controller->Base.IoBase->Access.Memory.VirtualBase + controller->CapRegisters->RtsOff);

    controller->Base.IORequirements.BufferAlignment = 0;
    controller->Base.IORequirements.Conformity =
            (controller->CapRegisters->HccParams1 & XHCI_HCCPARAMS1_AC64) ? OSMEMORYCONFORMITY_BITS64 : OSMEMORYCONFORMITY_BITS32;

    if (XhciQueueInitialize(controller) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    if (UsbManagerRegisterController(&controller->Base) != OS_EOK) {
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

irqstatus_t
OnFastInterrupt(
    InterruptFunctionTable_t* functionTable,
    InterruptResourceTable_t* resourceTable)
{
    _CRT_UNUSED(functionTable);
    _CRT_UNUSED(resourceTable);
    return IRQSTATUS_HANDLED;
}

UsbManagerController_t*
HCIControllerCreate(
    _In_ BusDevice_t* busDevice)
{
    XhciController_t* controller;
    oserr_t           oserr;

    controller = (XhciController_t*)UsbManagerCreateController(
            busDevice,
            USBCONTROLLER_KIND_XHCI,
            sizeof(XhciController_t)
    );
    if (controller == NULL) {
        return NULL;
    }

    list_construct(&controller->Endpoints);

    oserr = __AcquireIOSpace(controller);
    if (oserr != OS_EOK) {
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }

    controller->CapRegisters = (XhciCapabilityRegisters_t*)controller->Base.IoBase->Access.Memory.VirtualBase;
    controller->OpRegisters  = (XhciOperationalRegisters_t*)((uintptr_t)controller->CapRegisters + controller->CapRegisters->CapLength);

    oserr = __InitializeInterrupt(busDevice, controller);
    if (oserr != OS_EOK) {
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }

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
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }

    if (XhciSetup(controller) != OS_EOK) {
        HCIControllerDestroy(&controller->Base);
        return NULL;
    }
    return &controller->Base;
}

void
HCIControllerDestroy(
    _In_ UsbManagerController_t* controllerBase)
{
    XhciController_t* controller = (XhciController_t*)controllerBase;

    if (controller == NULL) {
        return;
    }

    UsbManagerDestroyController(controllerBase);
    XhciQueueDestroy(controller);
    if (controllerBase->Interrupt != UUID_INVALID) {
        UnregisterInterruptSource(controllerBase->Interrupt);
    }
    if (controllerBase->IoBase != NULL) {
        ReleaseDeviceIo(controllerBase->IoBase);
    }
    free(controller);
}
