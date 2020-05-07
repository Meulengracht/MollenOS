/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - Mass Storage Device Driver (Generic)
 */
#define __TRACE

#include "msd.h"
#include <ddk/service.h>
#include <ddk/utils.h>
#include <gracht/server.h>
#include <internal/_ipc.h>
#include <internal/_utils.h>
#include <stdlib.h>

static const char *DeviceTypeStrings[TypeCount] = {
    "Floppy Drive",
    "Disk Drive",
    "Hard Drive"
};
static const char *DeviceProtocolStrings[ProtocolCount] = {
    "Unknown",
    "CB",
    "CBI",
    "Bulk"
};

static void
RegisterStorage(
    _In_ UUId_t       ProtocolServerId,
    _In_ UUId_t       DeviceId,
    _In_ unsigned int Flags)
{
    int                      status;
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    
    status = svc_storage_register(GetGrachtClient(), &msg,
        ProtocolServerId, DeviceId, Flags);
    gracht_vali_message_finish(&msg);
}

static void
UnregisterStorage(
    _In_ UUId_t       DeviceId,
    _In_ unsigned int Flags)
{
    int                      status;
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    
    status = svc_storage_unregister(GetGrachtClient(), &msg, DeviceId, Flags);
    gracht_vali_message_finish(&msg);
}

static void*
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
IsSupportedInterface(
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
GetDeviceProtocol(
    _In_ MsdDevice_t*                    device,
    _In_ usb_device_interface_setting_t* interface)
{
    // Set initial shared stuff
    device->Protocol                      = ProtocolUnknown;
    device->AlignedAccess                 = 0;
    device->Descriptor.SectorsPerCylinder = 64;
    device->Descriptor.SectorSize         = 512;
    device->InterfaceId                   = interface->base.NumInterface;
    
    // Determine type of msd
    if (interface->base.Subclass == MSD_SUBCLASS_FLOPPY) {
        device->Type = TypeFloppy;
        device->AlignedAccess = 1;
        device->Descriptor.SectorsPerCylinder = 18;
    }
    else if (interface->base.Subclass != MSD_SUBCLASS_ATAPI) {
        device->Type = TypeDiskDrive;
    }
    else {
        device->Type = TypeHardDrive;
    }
    
    // Determine type of protocol
    if (interface->base.Protocol == MSD_PROTOCOL_CBI) {
        device->Protocol = ProtocolCBI;
    }
    else if (interface->base.Protocol == MSD_PROTOCOL_CB) {
        device->Protocol = ProtocolCB;
    }
    else if (interface->base.Protocol == MSD_PROTOCOL_BULK_ONLY) {
        device->Protocol = ProtocolBulk;
    }
}

static void
GetDeviceConfiguration(
    _In_ MsdDevice_t* device)
{
    usb_device_configuration_t configuration;
    UsbTransferStatus_t        status;
    int                        i, j;
    
    status = UsbGetActiveConfigDescriptor(&device->Base.DeviceContext, &configuration);
    if (status != TransferFinished) {
        return;
    }
    
    // TODO support interface settings
    for (i = 0; i < configuration.base.NumInterfaces; i++) {
        usb_device_interface_setting_t* interface = &configuration.interfaces[i].settings[0];
        if (IsSupportedInterface(interface)) {
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
            GetDeviceProtocol(device, interface);
            break;
        }
    }
    
    UsbFreeConfigDescriptor(&configuration);
}

MsdDevice_t*
MsdDeviceCreate(
    _In_ UsbDevice_t* UsbDevice)
{
    MsdDevice_t* Device;

    // Debug
    TRACE("MsdDeviceCreate(DeviceId %u)", UsbDevice->Base.Id);

    // Allocate new resources
    Device = (MsdDevice_t*)malloc(sizeof(MsdDevice_t));
    if (!Device) {
        return NULL;
    }
    
    memset(Device, 0, sizeof(MsdDevice_t));
    memcpy(&Device->Base, UsbDevice, sizeof(UsbDevice_t));
    ELEMENT_INIT(&Device->Header, (uintptr_t)UsbDevice->Base.Id, &Device);
    
    GetDeviceConfiguration(Device);
    
    // Debug
    TRACE("MSD Device of Type %s and Protocol %s", 
        DeviceTypeStrings[Device->Type],
        DeviceProtocolStrings[Device->Protocol]);

    // Initialize the kind of profile we discovered
    if (MsdDeviceInitialize(Device) != OsSuccess) {
        ERROR("Failed to initialize the msd-device, missing support.");
        goto Error;
    }

    // Allocate reusable buffers
    if (dma_pool_allocate(UsbRetrievePool(), sizeof(MsdCommandBlock_t), 
        (void**)&Device->CommandBlock) != OsSuccess) {
        ERROR("Failed to allocate reusable buffer (command-block)");
        goto Error;
    }
    if (dma_pool_allocate(UsbRetrievePool(), sizeof(MsdCommandStatus_t), 
        (void**)&Device->StatusBlock) != OsSuccess) {
        ERROR("Failed to allocate reusable buffer (status-block)");
        goto Error;
    }

    if (MsdDeviceStart(Device) != OsSuccess) {
        ERROR("Failed to initialize the device");
        goto Error;
    }

    // Wait for the disk service to finish loading
    if (WaitForFileService(1000) != OsSuccess) {
        ERROR("[msd] disk ready but storage service did not start");
        // TODO: what do
        return Device;
    }

    RegisterStorage(GetNativeHandle(gracht_server_get_dgram_iod()),
        Device->Base.Base.Id, SVC_STORAGE_REGISTER_FLAGS_REMOVABLE);
    return Device;

Error:
    // Cleanup
    if (Device != NULL) {
        MsdDeviceDestroy(Device);
    }
    return NULL;
}

OsStatus_t
MsdDeviceDestroy(
    _In_ MsdDevice_t *Device)
{
    // Notify diskmanager
    UnregisterStorage(Device->Base.Base.Id, SVC_STORAGE_UNREGISTER_FLAGS_FORCED);

    // Flush existing requests?
    // @todo

    // Free reusable buffers
    if (Device->CommandBlock != NULL) {
        dma_pool_free(UsbRetrievePool(), (void*)Device->CommandBlock);
    }
    if (Device->StatusBlock != NULL) {
        dma_pool_free(UsbRetrievePool(), (void*)Device->StatusBlock);
    }

    // Free data allocated
    free(Device);
    return OsSuccess;
}
