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
 * Mass Storage Device Driver (Generic)
 */
#define __TRACE

#include "msd.h"
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <internal/_gracht.h>
#include <internal/_utils.h>
#include <stdlib.h>

#include <sys_storage_service_client.h>

extern int __crt_get_server_iod(void);

static const char* g_deviceTypeNames[TypeCount] = {
    "Unknown",
    "Floppy Drive",
    "Disk Drive",
    "Hard Drive"
};
static const char* g_deviceProtocolNames[ProtocolCount] = {
    "Unknown",
    "CB",
    "CBI",
    "Bulk"
};

// Registers the disk with filed and waits for the result. This runs in the
// device registration job, which holds no driver lock, and filed answers
// before it sends its stat query back to this driver. A failure is only
// logged.
static void
__RegisterStorage(
        _In_ uuid_t       protocolServerId,
        _In_ uuid_t       deviceId,
        _In_ unsigned int flags)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                  result;
    oserr_t                  oserr;
    int                      status;

    status = sys_storage_register(GetGrachtClient(), &msg.base, protocolServerId, deviceId, flags);
    if (status != 0) {
        ERROR("[msd] failed to send register for device %u: %i", deviceId, status);
        return;
    }

    oserr = __GrachtAwaitCompleted(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    if (oserr != OS_EOK) {
        ERROR("[msd] no register result for device %u: %u", deviceId, oserr);
        return;
    }

    status = sys_storage_register_result(GetGrachtClient(), &msg.base, &result);
    if (status != 0) {
        ERROR("[msd] failed to read register result for device %u: %i", deviceId, status);
        return;
    }

    if (result != OS_EOK) {
        ERROR("[msd] filed rejected device %u: %u", deviceId, result);
    }
}

// Unregisters the disk without waiting for the result. MSDDeviceDestroy also
// runs from OnUnload at process exit, while the device list is locked, and
// the IPC response may never be read there. The request is abandoned so the client does
// not keep the response around.
static void
__UnregisterStorage(
        _In_ uuid_t  deviceId,
        _In_ uint8_t forced)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    int                      status;

    status = sys_storage_unregister(
        GetGrachtClient(),
        &msg.base,
        GetNativeHandle(__crt_get_server_iod()),
        deviceId,
        forced
    );
    if (status != 0) {
        ERROR("[msd] failed to send unregister for device %u: %i", deviceId, status);
        return;
    }

    status = gracht_client_abandon(GetGrachtClient(), &msg.base);
    if (status != 0) {
        WARNING("[msd] failed to drop the unregister result for device %u: %i", deviceId, status);
    }
}

static inline void*
memdup(void* mem, size_t size)
{
    void* dup = malloc(size);
    if (!dup) {
        return NULL;
    }
    memcpy(dup, mem, size);
    return dup;
}

static inline int
__IsSupportedInterface(
        _In_ usb_device_interface_setting_t* interface)
{
    // Verify class is MSD
    if (interface->base.Class != USB_CLASS_MSD) {
        return 0;
    }
    
    if (interface->base.Subclass == MSD_SUBCLASS_SCSI ||
        interface->base.Subclass == MSD_SUBCLASS_FLOPPY ||
        interface->base.Subclass == MSD_SUBCLASS_ATAPI) {
        return 1;
    }
    
    ERROR("Unsupported MSD Subclass 0x%x", interface->base.Subclass);
    return 0;
}

static inline void
__GetDeviceProtocol(
        _In_ MSDDevice_t*                    device,
        _In_ usb_device_interface_setting_t* interface)
{
    // Set the device IDs, interface number and default disk geometry.
    device->Protocol                      = ProtocolUnknown;
    device->AlignedAccess                 = 0;
    device->Descriptor.DeviceID           = device->Device->Base.Id;
    device->Descriptor.DriverID           = GetNativeHandle(__crt_get_server_iod());
    device->Descriptor.SectorsPerCylinder = 64;
    device->Descriptor.SectorSize         = 512;
    device->InterfaceId                   = interface->base.NumInterface;
    
    // Determine type of msd
    if (interface->base.Subclass == MSD_SUBCLASS_FLOPPY) {
        device->Type = TypeFloppy;
        device->AlignedAccess = 1;
        device->Descriptor.SectorsPerCylinder = 18;
    } else if (interface->base.Subclass != MSD_SUBCLASS_ATAPI) {
        device->Type = TypeDiskDrive;
    } else {
        device->Type = TypeHardDrive;
    }
    
    // Determine type of protocol
    if (interface->base.Protocol == MSD_PROTOCOL_CBI) {
        device->Protocol = ProtocolCBI;
    } else if (interface->base.Protocol == MSD_PROTOCOL_CB) {
        device->Protocol = ProtocolCB;
    } else if (interface->base.Protocol == MSD_PROTOCOL_BULK_ONLY) {
        device->Protocol = ProtocolBulk;
    }
}

static void
__GetDeviceConfiguration(
        _In_ MSDDevice_t* device)
{
    usb_device_configuration_t configuration;
    enum USBTransferCode        status;
    int                        i, j;
    
    status = UsbGetActiveConfigDescriptor(&device->Device->DeviceContext, &configuration);
    if (status != USBTRANSFERCODE_SUCCESS) {
        ERROR("__GetDeviceConfiguration: failed to retrieve configuration descriptor %u", status);
        return;
    }
    
    // TODO support interface settings
    for (i = 0; i < configuration.base.NumInterfaces; i++) {
        usb_device_interface_setting_t* interface = &configuration.interfaces[i].settings[0];
        if (__IsSupportedInterface(interface)) {
            for (j = 0; j < interface->base.NumEndpoints; j++) {
                usb_endpoint_descriptor_t* endpoint = &interface->endpoints[j];
                if (USB_ENDPOINT_TYPE(endpoint) == USB_ENDPOINT_INTERRUPT) {
                    device->Interrupt = memdup(endpoint, sizeof(usb_endpoint_descriptor_t));
                }
                else if (USB_ENDPOINT_TYPE(endpoint) == USB_ENDPOINT_BULK) {
                    if (endpoint->Address & USB_ENDPOINT_ADDRESS_IN) {
                        device->In = memdup(endpoint, sizeof(usb_endpoint_descriptor_t));
                    }
                    else {
                        device->Out = memdup(endpoint, sizeof(usb_endpoint_descriptor_t));
                    }
                }
            }
            __GetDeviceProtocol(device, interface);
            break;
        }
    }
    
    UsbFreeConfigDescriptor(&configuration);
}

MSDDevice_t*
MSDDeviceCreate(
        _In_ UsbDevice_t* usbDevice)
{
    MSDDevice_t* msdDevice;

    // Debug
    TRACE("MSDDeviceCreate(DeviceId %u)", usbDevice->Base.Id);

    // Allocate new resources
    msdDevice = (MSDDevice_t*)malloc(sizeof(MSDDevice_t));
    if (!msdDevice) {
        return NULL;
    }
    memset(msdDevice, 0, sizeof(MSDDevice_t));

    ELEMENT_INIT(&msdDevice->Header, (uintptr_t)usbDevice->Base.Id, msdDevice);
    msdDevice->Device = usbDevice;

    if (usbDevice->Base.Identification.Serial) {
        strncpy(
                &msdDevice->Descriptor.Serial[0],
                usbDevice->Base.Identification.Serial,
                sizeof(msdDevice->Descriptor.Serial)
        );
    }
    if (usbDevice->Base.Identification.Product) {
        strncpy(
                &msdDevice->Descriptor.Model[0],
                usbDevice->Base.Identification.Product,
                sizeof(msdDevice->Descriptor.Model)
        );
    }

    __GetDeviceConfiguration(msdDevice);
    
    // Debug
    TRACE("MSD Device of Type %s and Protocol %s",
          g_deviceTypeNames[msdDevice->Type],
          g_deviceProtocolNames[msdDevice->Protocol]);

    // Initialize the protocol selected from the USB interface.
    if (MSDDeviceInitialize(msdDevice) != OS_EOK) {
        ERROR("Failed to initialize the msd-device, missing support.");
        goto Error;
    }

    // Allocate reusable buffers
    if (dma_pool_allocate(UsbRetrievePool(), sizeof(MsdCommandBlock_t), 
        (void**)&msdDevice->CommandBlock) != OS_EOK) {
        ERROR("Failed to allocate reusable buffer (command-block)");
        goto Error;
    }
    if (dma_pool_allocate(UsbRetrievePool(), sizeof(MsdCommandStatus_t), 
        (void**)&msdDevice->StatusBlock) != OS_EOK) {
        ERROR("Failed to allocate reusable buffer (status-block)");
        goto Error;
    }

    if (MSDDeviceStart(msdDevice) != OS_EOK) {
        ERROR("Failed to initialize the device");
        goto Error;
    }
    return msdDevice;

Error:
    // Cleanup
    MSDDeviceDestroy(msdDevice);
    return NULL;
}

void
MSDDeviceRegisterStorage(
        _In_ MSDDevice_t* msdDevice)
{
    uuid_t deviceId = msdDevice->Device->Base.Id;

    // Wait for the disk service to finish loading
    if (WaitForFileService(1000) != OS_EOK) {
        ERROR("MSDDeviceRegisterStorage file service did not start, device %u is not registered", deviceId);
        // TODO: what do
        return;
    }

    __RegisterStorage(
            GetNativeHandle(__crt_get_server_iod()),
            deviceId,
            SYS_STORAGE_FLAGS_REMOVABLE
    );
}

oserr_t
MSDDeviceDestroy(
        _In_ MSDDevice_t* msdDevice)
{
    // Notify diskmanager
    __UnregisterStorage(msdDevice->Device->Base.Id, 1);

    // Flush existing requests?
    // @todo

    // Free reusable buffers
    if (msdDevice->CommandBlock != NULL) {
        dma_pool_free(UsbRetrievePool(), (void*)msdDevice->CommandBlock);
    }
    if (msdDevice->StatusBlock != NULL) {
        dma_pool_free(UsbRetrievePool(), (void*)msdDevice->StatusBlock);
    }

    // Free data allocated
    free(msdDevice->Device);
    free(msdDevice);
    return OS_EOK;
}
