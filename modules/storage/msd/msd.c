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

/* Includes
 * - System */
#include <os/utils.h>
#include <os/usb.h>
#include "msd.h"

/* Includes
 * - Library */
#include <stdlib.h>

/* Global
 * - String data and static data */
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

/* MsdDeviceCreate
 * Initializes a new msd-device from the given usb-device */
MsdDevice_t*
MsdDeviceCreate(
    _In_ MCoreUsbDevice_t *UsbDevice)
{
    // Variables
    MsdDevice_t *Device = NULL;
    int i;

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
    memset(Device, 0, sizeof(MsdDevice_t));
    memcpy(&Device->Base, UsbDevice, sizeof(MCoreUsbDevice_t));
    Device->Control = &UsbDevice->Endpoints[0];

    // Find neccessary endpoints
    for (i = 1; i < UsbDevice->Interface.Versions[0].EndpointCount + 1; i++) {
        if (UsbDevice->Endpoints[i].Type == EndpointInterrupt) {
            Device->Interrupt = &UsbDevice->Endpoints[i];
        }
        else if (UsbDevice->Endpoints[i].Type == EndpointBulk) {
            if (UsbDevice->Endpoints[i].Direction == USB_ENDPOINT_IN) {
                Device->In = &UsbDevice->Endpoints[i];
            }
            else if (UsbDevice->Endpoints[i].Direction == USB_ENDPOINT_OUT) {
                Device->Out = &UsbDevice->Endpoints[i];
            }
        }
    }

    // Set initial shared stuff
    Device->Protocol = ProtocolUnknown;
    Device->AlignedAccess = 0;
    Device->Descriptor.SectorsPerCylinder = 64;
    Device->Descriptor.SectorSize = 512;
    
    // Determine type of msd
    if (UsbDevice->Interface.Subclass == MSD_SUBCLASS_FLOPPY) {
        Device->Type = TypeFloppy;
        Device->AlignedAccess = 1;
        Device->Descriptor.SectorsPerCylinder = 18;
    }
    else if (UsbDevice->Interface.Subclass != MSD_SUBCLASS_ATAPI) {
        Device->Type = TypeDiskDrive;
    }
    else {
        Device->Type = TypeHardDrive;
    }

    // Determine type of protocol
    if (Device->Protocol == ProtocolUnknown) {
        if (UsbDevice->Interface.Protocol == MSD_PROTOCOL_CBI) {
            Device->Protocol = ProtocolCBI;
        }
        else if (UsbDevice->Interface.Protocol == MSD_PROTOCOL_CB) {
            Device->Protocol = ProtocolCB;
        }
        else if (UsbDevice->Interface.Protocol == MSD_PROTOCOL_BULK_ONLY) {
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
    if (BufferPoolAllocate(UsbRetrievePool(), sizeof(MsdCommandBlock_t), 
        (uintptr_t**)&Device->CommandBlock, &Device->CommandBlockAddress) != OsSuccess) {
        ERROR("Failed to allocate reusable buffer (command-block)");
        goto Error;
    }
    if (BufferPoolAllocate(UsbRetrievePool(), sizeof(MsdCommandStatus_t), 
        (uintptr_t**)&Device->StatusBlock, &Device->StatusBlockAddress) != OsSuccess) {
        ERROR("Failed to allocate reusable buffer (status-block)");
        goto Error;
    }

    // Perform setup
    if (MsdDeviceStart(Device) != OsSuccess) {
        ERROR("Failed to initialize the device");
        goto Error;
    }

    // Start out by initializing the contract
    InitializeContract(&Device->Contract, Device->Base.Base.Id, 1,
        ContractStorage, "MSD Storage Interface");

    // Register contract before interrupt
    if (RegisterContract(&Device->Contract) != OsSuccess) {
        ERROR("Failed to register storage contract for device");
    }

    // Notify diskmanager
    if (RegisterDisk(Device->Base.Base.Id, __DISK_REMOVABLE) != OsSuccess) {
        ERROR("Failed to register storage with storagemanager");
    }

    // Done
    return Device;

Error:
    // Cleanup
    if (Device != NULL) {
        MsdDeviceDestroy(Device);
    }

    // Done, return null
    return NULL;
}

/* MsdDeviceDestroy
 * Destroys an existing msd device instance and cleans up
 * any resources related to it */
OsStatus_t
MsdDeviceDestroy(
    _In_ MsdDevice_t *Device)
{
    // Notify diskmanager
    if (UnregisterDisk(Device->Base.Base.Id, __DISK_FORCED_REMOVE) != OsSuccess) {
        ERROR("Failed to unregister storage with storagemanager");
    }

    // Flush existing requests?
    // @todo

    // Free reusable buffers
    if (Device->CommandBlock != NULL) {
        BufferPoolFree(UsbRetrievePool(), (uintptr_t*)Device->CommandBlock);
    }
    if (Device->StatusBlock != NULL) {
        BufferPoolFree(UsbRetrievePool(), (uintptr_t*)Device->StatusBlock);
    }

    // Free data allocated
    free(Device);
    return OsSuccess;
}
