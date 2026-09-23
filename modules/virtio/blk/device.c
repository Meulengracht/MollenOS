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
 * Device lifecycle, storage publication, and recovery orchestration.
 */

#define __TRACE
#include "private.h"

#include <ddk/service.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <io.h>
#include <os/types/storage.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys_storage_service_client.h>

#define VIRTIO_BLK_SUPPORTED_FEATURES (VIRTIO_BLK_F_SIZE_MAX | \
    VIRTIO_BLK_F_SEG_MAX | VIRTIO_BLK_F_RO | VIRTIO_BLK_F_BLK_SIZE | \
    VIRTIO_F_RING_EVENT_IDX | VIRTIO_F_RING_RESET)

extern int __crt_get_server_iod(void);

oserr_t
VirtioBlkDeviceRegisterStorage(
    _In_ VirtioBlkDevice_t* device)
{
    struct vali_link_message message;
    int status;

    if (device == NULL || device->Registered) {
        return OS_EINVALPARAMS;
    }
    
    TRACE("VirtioBlkDeviceRegisterStorage(device=%u)", device->Descriptor.DeviceID);
    
    message = (struct vali_link_message)VALI_MSG_INIT_HANDLE(GetFileService());
    // Registration occurs only after DRIVER_OK. filed immediately queries the
    // descriptor and may start partition I/O once this message is delivered.
    // The caller must therefore insert the device into its lookup collection
    // before invoking this function.
    status = sys_storage_register(
        GetGrachtClient(),
        &message.base,
        GetNativeHandle(__crt_get_server_iod()),
        device->Descriptor.DeviceID,
        (enum sys_storage_flags)device->Descriptor.Flags
    );

    if (status == 0) {
        device->Registered = 1;
        return OS_EOK;
    }
    ERROR("Failed to register Virtio storage device %u", device->Descriptor.DeviceID);
    return OS_EUNKNOWN;
}

static void
__UnregisterStorage(
    _In_ VirtioBlkDevice_t* device)
{
    struct vali_link_message message = VALI_MSG_INIT_HANDLE(GetFileService());
    if (!device->Registered) {
        return;
    }

    (void)sys_storage_unregister(
        GetGrachtClient(),
        &message.base,
        device->Descriptor.DeviceID,
        1
    );
    device->Registered = 0;
}

oserr_t
VirtioBlkRecoverDevice(
    _InOut_ VirtioBlkDevice_t* device)
{
    oserr_t oserr;
    TRACE("VirtioBlkRecoverDevice()");

    oserr = VirtioBlkResetRequestQueue(device);
    if (oserr != OS_EOK) {
        goto error;
    }

    oserr = VirtioPciNegotiateFeatures(
        &device->Transport,
        VIRTIO_BLK_SUPPORTED_FEATURES,
        VIRTIO_F_VERSION_1,
        &device->Features
    );
    if (oserr != OS_EOK) {
        goto error;
    }
    
    oserr = VirtioBlkReadConfiguration(device);
    if (oserr != OS_EOK) {
        goto error;
    }

    // Update the device descriptor flags based on the negotiated features.
    device->Descriptor.Flags &= ~STORAGE_READONLY;
    if (device->Features & VIRTIO_BLK_F_RO) {
        device->Descriptor.Flags |= STORAGE_READONLY;
    }
    oserr = VirtioSplitQueueCreate(
        &device->Transport,
        0,
        VIRTIO_BLK_QUEUE_SIZE,
        &device->RequestQueue
    );
    if (oserr != OS_EOK) {
        goto error;
    }
    // Finish the PCI initialization if all previous steps succeeded.
    oserr = VirtioPciFinishInitialization(&device->Transport);
    if (oserr != OS_EOK) {
        goto error;
    }
    return oserr;

error:
    ERROR("Virtio block device recovery failed: %u", oserr);
    VirtioPciSetFailed(&device->Transport);
    __UnregisterStorage(device);
    return oserr;
}

VirtioBlkDevice_t*
VirtioBlkDeviceCreate(
    _In_ BusDevice_t* busDevice,
    _In_ uuid_t       storageDeviceId)
{
    VirtioBlkDevice_t* device;
    oserr_t            oserr;
    TRACE("VirtioBlkDeviceCreate(storageDeviceId=%u)", storageDeviceId);

    if (busDevice == NULL) {
        return NULL;
    }
    
    device = calloc(1, sizeof(VirtioBlkDevice_t));
    if (device == NULL) {
        free(busDevice);
        return NULL;
    }
    
    ELEMENT_INIT(&device->Header, (void*)(uintptr_t)busDevice->Base.Id, device);
    device->BusDevice = busDevice;
    device->InterruptId = UUID_INVALID;
    device->EventDescriptor = -1;
    device->Descriptor.DeviceID = storageDeviceId;
    device->Descriptor.DriverID = GetNativeHandle(__crt_get_server_iod());
    device->Descriptor.LUNCount = 1;
    memcpy(device->Descriptor.Model, "Virtio Block Device", 20);
    snprintf(
        device->Descriptor.Serial,
        sizeof(device->Descriptor.Serial),
        "virtio-%u",
        storageDeviceId
    );

    oserr = VirtioPciTransportInitialize(busDevice, &device->Transport);
    if (oserr != OS_EOK) {
        goto error;
    }

    oserr = VirtioPciNegotiateFeatures(
        &device->Transport,
        VIRTIO_BLK_SUPPORTED_FEATURES,
        VIRTIO_F_VERSION_1,
        &device->Features
    );
    if (oserr != OS_EOK) {
        goto error;
    }

    oserr = VirtioBlkReadConfiguration(device);
    if (oserr != OS_EOK) {
        goto error;
    }

    if (device->Features & VIRTIO_BLK_F_RO) {
        device->Descriptor.Flags |= STORAGE_READONLY;
    }
    oserr = VirtioSplitQueueCreate(
        &device->Transport,
        0,
        VIRTIO_BLK_QUEUE_SIZE,
        &device->RequestQueue
    );
    if (oserr != OS_EOK) {
        goto error;
    }

    oserr = VirtioBlkRegisterInterrupt(device);
    if (oserr != OS_EOK) {
        goto error;
    }

    oserr = VirtioPciFinishInitialization(&device->Transport);
    if (oserr != OS_EOK) {
        goto error;
    }

    return device;

error:
    ERROR("Failed to initialize Virtio block device: %u", oserr);
    VirtioPciSetFailed(&device->Transport);
    VirtioBlkDeviceDestroy(device);
    return NULL;
}

void
VirtioBlkDeviceDestroy(
    _In_ VirtioBlkDevice_t* device)
{
    oserr_t oserr;

    if (device == NULL) {
        return;
    }
    TRACE("VirtioBlkDeviceDestroy(device=%u)", device->Descriptor.DeviceID);

    // Stop externally visible I/O before tearing down interrupts and queue DMA.
    __UnregisterStorage(device);
    VirtioBlkUnregisterInterrupt(device);

    if (device->Transport.Device != NULL) {
        oserr = VirtioBlkResetRequestQueue(device);
        if (oserr != OS_EOK) {
            // Freeing queue or transport memory while reset failed would leave
            // the device able to DMA into released memory. Leak deliberately
            // rather than turn a teardown failure into memory corruption.
            ERROR("Refusing unsafe Virtio block teardown after reset failure: %u", oserr);
            return;
        }
    }
    VirtioPciTransportDestroy(&device->Transport);
    free(device->BusDevice);
    free(device);
}

oserr_t
VirtioBlkDeviceHandleConfigurationChange(
    _InOut_ VirtioBlkDevice_t* device)
{
    uint64_t value;
    uint8_t  status;
    oserr_t  oserr;

    if (device == NULL) {
        return OS_EINVALPARAMS;
    }
    oserr = VirtioPciRegionRead(
        &device->Transport.Regions[VIRTIO_PCI_CAP_COMMON_CFG - 1],
        offsetof(VirtioPciCommonConfiguration_t, DeviceStatus),
        sizeof(uint8_t),
        &value
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    status = (uint8_t)value;
    if (status & VIRTIO_STATUS_DEVICE_NEEDS_RESET) {
        // Recovery cancels in-flight requests, renegotiates features, rebuilds
        // the queue, and only then returns the device to DRIVER_OK.
        return VirtioBlkRecoverDevice(device);
    }
    if (status & VIRTIO_STATUS_FAILED) {
        __UnregisterStorage(device);
        return OS_EDEVFAULT;
    }
    return VirtioBlkReadConfiguration(device);
}
