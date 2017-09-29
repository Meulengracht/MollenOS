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

/* Includes
 * - System */
#include <os/driver/usb.h>
#include "msd.h"

/* MsdDeviceCreate
 * Initializes a new msd-device from the given usb-device */
MsdDevice_t*
MsdDeviceCreate(
    _In_ MCoreUsbDevice_t *UsbDevice)
{
    // Variables
    MsdDevice_t *Device = NULL;
    int i;

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
    for (i = 1; i < UsbDevice->Interface.Versions[0].EndpointCount; i++) {
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
    
    // Sanitize found endpoints
    if (Device->In == NULL || Device->Out == NULL) {
        ERROR("Either in or out endpoint not available on device");
        goto Error;
    }

    // Determine type of flash drive
    if (UsbDevice->Interface.Subclass == MSD_SUBCLASS_FLOPPY) {
        Device->Type = FloppyDrive;
        Device->AlignedAccess = 1;
        Device->Descriptor.SectorsPerCylinder = 18;
    }
    else if (UsbDevice->Interface.Subclass == MSD_SUBCLASS_ATAPI) {
        Device->Type = DiskDrive;
        Device->AlignedAccess = 0;
        Device->Descriptor.SectorsPerCylinder = 64;
    }
    else {
        Device->Type = HardDrive;
        Device->AlignedAccess = 0;
        Device->Descriptor.SectorsPerCylinder = 64;
    }

    // Initialize the storage descriptor to default
    Device->Descriptor.SectorSize = 512;

    // If the type is of harddrive, reset bulk
    if (Device->Type == HardDrive) {
        MsdReset(Device);
    }

    // Reset data toggles

    /* Send Inquiry */
    ScsiInquiry_t InquiryData;
    if (UsbMsdSendSCSICommandIn(SCSI_INQUIRY, DevData, 0, &InquiryData, sizeof(ScsiInquiry_t))
        != TransferFinished)
        LogDebug("USBM", "Failed to execute Inquiry Command");

    // Perform the Test-Unit Ready command
    i = 3;
    if (Device->Type != HardDrive) {
        i = 30;
    }
    while (Device->IsReady == 0 && i != 0) {
        UsbMsdReadyDevice(Device);
        if (Device->IsReady == 1)
            break; 
        StallMs(100);
        i--;
    }

    /* Did we fail to ready device? */
    if (!DevData->IsReady) {
        LogDebug("USBM", "Failed to ready MSD device");
        return;
    }

    /* Read Capabilities 10
     * If it returns 0xFFFFFFFF 
     * Use Read Capabilities 16 */
    UsbMsdReadCapacity(DevData, StorageData);

    /* Debug */
    LogInformation("USBM", "MSD SectorCount: 0x%x, SectorSize: 0x%x",
        (size_t)StorageData->SectorCount, StorageData->SectorSize);

    /* Setup information */
    mDevice->VendorId = 0x8086;
    mDevice->DeviceId = 0x0;
    mDevice->Class = DEVICEMANAGER_LEGACY_CLASS;
    mDevice->Subclass = 0x00000018;

Error:


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
    // Flush existing requests?

    // Free data allocated
    free(Device);
}
