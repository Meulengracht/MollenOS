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
 * 
 * Virtio network device interrupt handling.
 * 
 * Handles interrupt registration and teardown for the Virtio network device.
 */

#include <ddk/utils.h>
#include <event.h>
#include <io.h>
#include <ioctl.h>
#include <ioset.h>

#include "virtio-net.h"

extern gracht_server_t*
__crt_get_module_server(void);

oserr_t
VirtioNetRegisterInterrupt(
    _InOut_ VirtioNetDevice_t* device)
{
    DeviceInterrupt_t interrupt;
    DeviceIo_t*       isrIoSpace;
    int               enable = 1;
    int               status;
    TRACE("VirtioNetRegisterInterrupt()");

    // Obtain the I/O space for the ISR configuration region
    isrIoSpace = device->Transport.Regions[VIRTIO_PCI_CAP_ISR_CFG - 1].IoSpace;

    // The fast handler only acknowledges the ISR and signals this descriptor.
    // Queue draining and protocol responses stay in the normal module context.
    device->EventDescriptor = eventd(0, EVT_RESET_EVENT);
    if (device->EventDescriptor < 0) {
        ERROR("VirtioNetRegisterInterrupt: failed to create event descriptor");
        return OS_EUNKNOWN;
    }

    status = ioset_ctrl(
        gracht_server_get_aio_handle(__crt_get_module_server()),
        IOSET_ADD,
        device->EventDescriptor,
        &(struct ioset_event){
            .data.context = device,
            .events = IOSETSYN
        }
    );
    if (status) {
        ERROR("VirtioNetRegisterInterrupt: failed to register event descriptor");
        close(device->EventDescriptor);
        device->EventDescriptor = -1;
        return OS_EUNKNOWN;
    }

    (void)ioctl(device->EventDescriptor, FIONBIO, &enable);

    device->InterruptResource.IsrOffset =
            device->Transport.Regions[VIRTIO_PCI_CAP_ISR_CFG - 1].Offset;

    atomic_init(&device->InterruptResource.PendingStatus, 0);
    DeviceInterruptInitialize(&interrupt, device->BusDevice);

    TRACE("VirtioNetRegisterInterrupt line=%i, pin=%i, isrOffset=0x%x",
          interrupt.Line,
          interrupt.Pin,
          device->InterruptResource.IsrOffset);
    RegisterInterruptDescriptor(&interrupt, device->EventDescriptor);

    // Register handler for fast interrupt, and the resources it needs
    RegisterFastInterruptHandler(&interrupt, (InterruptHandler_t)OnFastInterrupt);
    RegisterFastInterruptIoResource(&interrupt, isrIoSpace);
    RegisterFastInterruptMemoryResource(&interrupt,
                                        (uintptr_t)&device->InterruptResource,
                                        sizeof(device->InterruptResource),
                                        0);

    device->InterruptId = RegisterInterruptSource(&interrupt, 0);
    if (device->InterruptId == UUID_INVALID) {
        ERROR("VirtioNetRegisterInterrupt: failed to register the interrupt source");
        VirtioNetUnregisterInterrupt(device);
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

void
VirtioNetUnregisterInterrupt(
    _InOut_ VirtioNetDevice_t* device)
{
    if (device->InterruptId != UUID_INVALID) {
        UnregisterInterruptSource(device->InterruptId);
        device->InterruptId = UUID_INVALID;
    }

    if (device->EventDescriptor >= 0) {
        ioset_ctrl(
            gracht_server_get_aio_handle(__crt_get_module_server()),
            IOSET_DEL,
            device->EventDescriptor,
            NULL
        );
        close(device->EventDescriptor);
        device->EventDescriptor = -1;
    }
}
