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
 * Advanced Host Controller Interface Driver
 * TODO:
 *    - Port Multiplier Support
 *    - Power Management
 */

//#define __TRACE

#include <ddk/utils.h>
#include <ddk/convert.h>
#include <ioset.h>
#include <os/types/device.h>
#include "manager.h"

#include <ctt_driver_service_server.h>
#include <ctt_storage_service_server.h>
#include <io.h>
#include <os/usched/job.h>

extern gracht_server_t* __crt_get_module_server(void);

static list_t controllers = LIST_INIT;

static list_t g_cleanupControllers = LIST_INIT;
static bool   g_cleanupWorkerRunning;

static void
__CleanupWorker(
    _In_ void* argument,
    _In_ void* cancellationToken)
{
    _CRT_UNUSED(argument);
    while (!usched_is_cancelled(cancellationToken) && g_cleanupControllers.head != NULL) {
        struct timespec wakeUp;
        int             remaining;
        
        timespec_get(&wakeUp, TIME_UTC);
        wakeUp.tv_sec++;
        usched_job_sleep(&wakeUp);
        
        remaining = g_cleanupControllers.count;
        while (remaining-- > 0 && g_cleanupControllers.head != NULL) {
            bool              pending = false;
            AhciController_t* controller = g_cleanupControllers.head->value;
            list_remove(&g_cleanupControllers, &controller->CleanupHeader);
            
            controller->CleanupQueued = false;
            if (controller->Stopping) {
                if (AhciControllerDestroy(controller) != OS_EOK) {
                    AhciControllerScheduleCleanup(controller);
                }
                continue;
            }
            
            if (controller->Cleaning) {
                AhciControllerScheduleCleanup(controller);
                continue;
            }
            
            // Completion can yield during IPC. Keep the port table allocated
            // until this pass finishes.
            controller->Cleaning++;
            
            for (int i = 0; i < AHCI_MAX_PORTS; i++) {
                if (controller->Ports[i] != NULL) {
                    AhciTransactionsRetry(controller, controller->Ports[i]);
                    pending |= controller->Ports[i]->CleanupTransactions.count != 0;
                }
            }

            controller->Cleaning--;
            if (pending || controller->Stopping) {
                AhciControllerScheduleCleanup(controller);
            }
        }
    }

    g_cleanupWorkerRunning = false;
}

void
AhciControllerScheduleCleanup(
    _In_ AhciController_t* controller)
{
    if (!controller->CleanupQueued) {
        ELEMENT_INIT(&controller->CleanupHeader, 0, controller);
        list_append(&g_cleanupControllers, &controller->CleanupHeader);
        controller->CleanupQueued = true;
    }

    // Healthy controllers need no timer or request-time job allocation.
    if (!g_cleanupWorkerRunning) {
        g_cleanupWorkerRunning = true;
        if (usched_job_queue(__CleanupWorker, NULL) == UUID_INVALID) {
            g_cleanupWorkerRunning = false;
            ERROR("AHCI: could not start cleanup worker; controller stays indexed");
        }
    }
}

void
AhciControllerRemove(
    _In_ AhciController_t* controller)
{
    if (controller->CleanupQueued) {
        list_remove(&g_cleanupControllers, &controller->CleanupHeader);
    }
    list_remove(&controllers, &controller->header);
}

irqstatus_t
OnFastInterrupt(
    _In_ InterruptFunctionTable_t* interruptTable,
    _In_ InterruptResourceTable_t* resourceTable)
{
    AhciInterruptResource_t*         resource  = (AhciInterruptResource_t*)INTERRUPT_RESOURCE(resourceTable, 0);
    volatile AHCIGenericRegisters_t* registers = (volatile AHCIGenericRegisters_t*)INTERRUPT_IOSPACE(resourceTable, 0)->Access.Memory.VirtualBase;
    reg32_t                          interruptStatus;
    int                              i;

    // Skip processing immediately if the interrupt was not for us
    interruptStatus = registers->InterruptStatus;
    if (!interruptStatus) {
        return IRQSTATUS_NOT_HANDLED;
    }

    // Save the status to port that made it and clear
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if ((interruptStatus & (1U << i)) != 0) {
            volatile AHCIPortRegisters_t* portRegister = (volatile AHCIPortRegisters_t*)((uintptr_t)registers + AHCI_REGISTER_PORTBASE(i));
            atomic_fetch_or(&resource->PortInterruptStatus[i], portRegister->InterruptStatus);
            portRegister->InterruptStatus = portRegister->InterruptStatus;
        }
    }

    // Write clear interrupt register and return
    registers->InterruptStatus = interruptStatus;
    atomic_fetch_or(&resource->ControllerInterruptStatus, interruptStatus);
    interruptTable->EventSignal(resourceTable->HandleResource);
    return IRQSTATUS_HANDLED;
}

void
OnInterrupt(
    _In_ AhciController_t* controller)
{
    reg32_t interruptStatus = atomic_exchange(&controller->InterruptResource.ControllerInterruptStatus, 0);
    TRACE("OnInterrupt(controller=0x%" PRIxIN ")", controller);

    if (controller->Stopping) {
        return;
    }

    // Device removal and completion callbacks can send IPC. Count the entire
    // handler so teardown cannot free its controller or ports while it yields.
    controller->Cleaning++;

handler_loop:
    // Iterate the port-map and check if the interrupt
    // came from that port
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (controller->Ports[i] != NULL && ((interruptStatus & (1U << i)) != 0)) {
            AhciPortInterruptHandler(controller, controller->Ports[i]);
        }
    }

    interruptStatus = atomic_exchange(&controller->InterruptResource.ControllerInterruptStatus, 0);
    if (interruptStatus) {
        goto handler_loop;
    }
    controller->Cleaning--;
}

oserr_t
OnLoad(void)
{
    // Register supported protocols
    gracht_server_register_protocol(__crt_get_module_server(), &ctt_driver_server_protocol);
    gracht_server_register_protocol(__crt_get_module_server(), &ctt_storage_server_protocol);
    
    // If AhciManagerInitialize should fail, then the OnUnload will
    // be called automatically
    return AhciManagerInitialize();
}

void
OnUnload(void)
{
    AhciController_t* controller;

    while (controllers.head != NULL) {
        controller = controllers.head->value;
        if (AhciControllerDestroy(controller) != OS_EOK) {
            // OnUnload runs from atexit. A queued job cannot finish after the
            // process exits, so wait here until hardware and DMA cleanup finish.
            thrd_sleep(&(struct timespec) { .tv_sec = 1 }, NULL);
        }
    }
    AhciManagerDestroy();
}

oserr_t OnEvent(struct ioset_event* event)
{
    TRACE("OnEvent(event->events=0x%x)", event->events);
    if (event->events & IOSETSYN) {
        AhciController_t* controller = event->data.context;
        unsigned int      value;
        
        if (read(controller->event_descriptor, &value, sizeof(unsigned int)) != sizeof(unsigned int)) {
            return OS_EUNKNOWN;
        }

        if (!controller->Stopping) {
            OnInterrupt(controller);
        }

        // Retry worker creation after a previous allocation failure, without
        // adding scheduler work to the normal interrupt path.
        if (controller->CleanupQueued && !g_cleanupWorkerRunning) {
            AhciControllerScheduleCleanup(controller);
        }
        return OS_EOK;
    }
    return OS_ENOENT;
}

void ctt_driver_register_device_invocation(struct gracht_message* message, const struct sys_device* device)
{
    BusDevice_t*      busDevice = (BusDevice_t*)from_sys_device(device);
    AhciController_t* controller = NULL;
    oserr_t           status;

    if (busDevice == NULL) {
        ERROR("AHCI: could not allocate device description");
        return;
    }
    status = AhciControllerCreate(busDevice, &controller);
    if (controller == NULL) {
        free(busDevice);
        return;
    }

    list_append(&controllers, &controller->header);
    if (status != OS_EOK) {
        ERROR("AHCI controller %u: setup failed (%u)", busDevice->Base.Id, status);
        if (AhciControllerDestroy(controller) != OS_EOK) {
            AhciControllerScheduleCleanup(controller);
        }
    }
}

oserr_t
OnUnregister(
    _In_ Device_t* device)
{
    AhciController_t* controller = list_find_value(&controllers, (void*)(uintptr_t)device->Id);
    oserr_t           status;
    if (controller == NULL) {
        return OS_ENOENT;
    }
    
    status = AhciControllerDestroy(controller);
    if (status != OS_EOK) {
        AhciControllerScheduleCleanup(controller);
    }
    return status;
}

void ctt_driver_ioctl_invocation(
        _In_ struct gracht_message* message,
        _In_ const uuid_t           deviceId,
        _In_ const unsigned int     request,
        _In_ const uint8_t*         out,
        _In_ const uint32_t         out_count)
{
    // Filesystems address the registered disk, whose ID differs from the PCI
    // controller ID used by device registration and DMAOpen.
    AhciDevice_t* device = AhciManagerGetDevice(deviceId);
    if (device == NULL) {
        ctt_driver_ioctl_response(message, NULL, 0, OS_ENOENT);
        return;
    }

    ctt_driver_ioctl_response(message, NULL, 0, OS_ENOTSUPPORTED);
}

// libddk requires these handlers even though AHCI does not use them.
void ctt_driver_get_device_protocols_invocation(struct gracht_message* message, const uuid_t deviceId) { }
void sys_device_event_protocol_device_invocation(void) { }
void sys_device_event_device_update_invocation(void) { }
