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

MsdDevice_t*
MsdDeviceCreate(
    _In_ MCoreUsbDevice_t* UsbDevice)
{
    MsdDevice_t* Device = NULL;
    int          i;

    // Debug
    TRACE("MsdDeviceCreate(DeviceId %u)", UsbDevice->Base.Id);

    // Validate the kind of msd device, we don't support all kinds
    if (UsbDevice->Interface.Subclass != MSD_SUBCLASS_SCSI
        && UsbDevice->Interface.Subclass != MSD_SUBCLASS_FLOPPY
        && UsbDevice->Interface.Subclass != MSD_SUBCLASS_ATAPI) {
        ERROR("Unsupported MSD Subclass 0x%x", UsbDevice->Interface.Subclass);
        goto Error;
    }

    // Allocate new resources
    Device = (MsdDevice_t*)malloc(sizeof(MsdDevice_t));
    if (!Device) {
        return NULL;
    }
    
    memset(Device, 0, sizeof(MsdDevice_t));
    memcpy(&Device->Base, UsbDevice, sizeof(MCoreUsbDevice_t));
    
    ELEMENT_INIT(&Device->Header, (uintptr_t)UsbDevice->Base.Id, &Device);
    Device->Control = &Device->Base.Endpoints[0];

    // Find neccessary endpoints
    for (i = 1; i < Device->Base.Interface.Versions[0].EndpointCount + 1; i++) {
        if (Device->Base.Endpoints[i].Type == EndpointInterrupt) {
            Device->Interrupt = &Device->Base.Endpoints[i];
        }
        else if (Device->Base.Endpoints[i].Type == EndpointBulk) {
            if (Device->Base.Endpoints[i].Direction == USB_ENDPOINT_IN) {
                Device->In = &Device->Base.Endpoints[i];
            }
            else if (Device->Base.Endpoints[i].Direction == USB_ENDPOINT_OUT) {
                Device->Out = &Device->Base.Endpoints[i];
            }
        }
    }

    // Set initial shared stuff
    Device->Protocol = ProtocolUnknown;
    Device->AlignedAccess = 0;
    Device->Descriptor.SectorsPerCylinder = 64;
    Device->Descriptor.SectorSize = 512;
    
    // Determine type of msd
    if (Device->Base.Interface.Subclass == MSD_SUBCLASS_FLOPPY) {
        Device->Type = TypeFloppy;
        Device->AlignedAccess = 1;
        Device->Descriptor.SectorsPerCylinder = 18;
    }
    else if (Device->Base.Interface.Subclass != MSD_SUBCLASS_ATAPI) {
        Device->Type = TypeDiskDrive;
    }
    else {
        Device->Type = TypeHardDrive;
    }

    // Determine type of protocol
    if (Device->Protocol == ProtocolUnknown) {
        if (Device->Base.Interface.Protocol == MSD_PROTOCOL_CBI) {
            Device->Protocol = ProtocolCBI;
        }
        else if (Device->Base.Interface.Protocol == MSD_PROTOCOL_CB) {
            Device->Protocol = ProtocolCB;
        }
        else if (Device->Base.Interface.Protocol == MSD_PROTOCOL_BULK_ONLY) {
            Device->Protocol = ProtocolBulk;
        }
    }
    
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
