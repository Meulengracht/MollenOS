/**
 * Copyright, Philip Meulengracht
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

/* Interrupt event registration and teardown; callbacks live in main.c. */
#define __TRACE
#include "private.h"

#include <ddk/utils.h>
#include <event.h>
#include <io.h>
#include <ioctl.h>
#include <ioset.h>

extern gracht_server_t* __crt_get_module_server(void);

oserr_t
VirtioBlkRegisterInterrupt(
    _InOut_ VirtioBlkDevice_t* device)
{
    uint32_t    count;
    oserr_t     oserr;
    DeviceIo_t* isrIoSpace = device->Transport.Regions[VIRTIO_PCI_CAP_ISR_CFG - 1].IoSpace;
    int         enable = 1;
    int         status;
    TRACE("VirtioBlkRegisterInterrupt()");

    // The fast handler only acknowledges the ISR and signals this descriptor.
    // Queue draining and protocol responses stay in the normal module context.
    device->EventDescriptor = eventd(0, EVT_RESET_EVENT);
    if (device->EventDescriptor < 0) {
        ERROR("Failed to create Virtio block interrupt event descriptor");
        return OS_EUNKNOWN;
    }
    
    status = ioset_ctrl(
        gracht_server_get_aio_handle(__crt_get_module_server()),
        IOSET_ADD,
        device->EventDescriptor,
        &(struct ioset_event) {
            .data.context = device,
            .events = IOSETSYN
        }
    );
    if (status) {
        ERROR("Failed to register Virtio block interrupt event descriptor");
        close(device->EventDescriptor);
        device->EventDescriptor = -1;
        return OS_EUNKNOWN;
    }
    
    (void)ioctl(device->EventDescriptor, FIONBIO, &enable);

    device->InterruptResource.IsrOffset =
        device->Transport.Regions[VIRTIO_PCI_CAP_ISR_CFG - 1].Offset;

    atomic_init(&device->InterruptResource.PendingStatus, 0);
    DeviceInterruptInitialize(&device->Interrupt, device->BusDevice);
    
    oserr = DeviceInterruptAllocate(
        &device->Interrupt,
        1, // Min
        1, // Optimal
        INTERRUPT_STRATEGY_INTx,
        &count
    );
    if (oserr != OS_EOK) {
        goto cleanup;
    }
    
    oserr = DeviceInterruptSetDescriptor(&device->Interrupt, 0, device->EventDescriptor);
    if (oserr != OS_EOK) {
        goto cleanup;
    }
    
    oserr = DeviceInterruptSetHandler(&device->Interrupt, 0, (InterruptHandler_t)OnFastInterrupt);
    if (oserr != OS_EOK) {
        goto cleanup;
    }
    
    oserr = DeviceInterruptAddIoResource(&device->Interrupt, 0, isrIoSpace);
    if (oserr != OS_EOK) {
        goto cleanup;
    }
    
    oserr = DeviceInterruptAddMemoryResource(
        &device->Interrupt,
        0,
        (uintptr_t)&device->InterruptResource,
        sizeof(device->InterruptResource),
        0
    );
    if (oserr != OS_EOK) {
        goto cleanup;
    }
    
    oserr = DeviceInterruptEnable(&device->Interrupt);
    if (oserr != OS_EOK) {
        goto cleanup;
    }
    return OS_EOK;

cleanup:
    // Retain the event if handler cleanup fails; it may still be referenced.
    VirtioBlkUnregisterInterrupt(device);
    return oserr;
}

oserr_t
VirtioBlkUnregisterInterrupt(
    _InOut_ VirtioBlkDevice_t* device)
{
    oserr_t status;

    // Device teardown stops the hardware first. Wait for its last handler before
    // removing the event or allowing the caller to free the device state.
    status = DeviceInterruptDestroy(&device->Interrupt);
    if (status != OS_EOK) {
        ERROR("Failed to release Virtio interrupt; keeping its resources alive");
        return status;
    }

    if (device->EventDescriptor >= 0) {
        ioset_ctrl(gracht_server_get_aio_handle(__crt_get_module_server()),
                   IOSET_DEL, device->EventDescriptor, NULL);
        close(device->EventDescriptor);
        device->EventDescriptor = -1;
    }
    return OS_EOK;
}
