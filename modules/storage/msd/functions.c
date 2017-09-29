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

/* Sense Key Descriptions
 * Provides descriptive information about the sense codes */
const char* SenseKeys[] = {
    "No Sense",
    "Recovered Error - last command completed with some recovery action",
    "Not Ready - logical unit addressed cannot be accessed",
    "Medium Error - command terminated with a non-recovered error condition",
    "Hardware Error",
    "Illegal Request - illegal parameter in the command descriptor block",
    "Unit Attention - disc drive may have been reset.",
    "Data Protect - command read/write on a protected block",
    "Undefined",
    "Firmware Error",
    "Undefined",
    "Aborted Command - disc drive aborted the command",
    "Equal - SEARCH DATA command has satisfied an equal comparison",
    "Volume Overflow - buffered peripheral device has reached the end of medium partition",
    "Miscompare - source data did not match the data read from the medium",
    "Undefined"
};

uint32_t rev32(uint32_t dword)
{
    return ((dword >> 24) & 0x000000FF) 
        | ((dword >> 8) & 0x0000FF00) 
        | ((dword << 8) & 0x00FF0000) 
        | ((dword << 24) & 0xFF000000);
}

uint64_t rev64(uint64_t qword)
{
    uint64_t y;
    char* px = (char*)&qword;
    char* py = (char*)&y;
    for (int i = 0; i<sizeof(uint64_t); i++)
        py[i] = px[sizeof(uint64_t) - 1 - i];
    return y;
}

/* MsdReset
 * Performs an interface reset, only neccessary for bulk endpoints. */
OsStatus_t
MsdReset(
    _In_ MsdDevice_t *Device)
{
    // Variables
    UsbTransferStatus_t Status;

    // Reset is 
    // 0x21 | 0xFF | wIndex - Interface
    Status = UsbExecutePacket(Device->Base.Driver, Device->Base.Device, 
        Device->Base.Device, Device->Control,
        USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_INTERFACE,
        MSD_REQUEST_RESET, 0, 0, (uint16_t)Device->Base.Interface.Id, 0, NULL);

    // The value of the data toggle bits shall be preserved 
    // it says in the usbmassbulk spec
    if (Status != TransferFinished) {
        return OsError;
    }
    else {
        return OsSuccess;
    }
}

/* MsdRecoveryReset
 * Performs a full interface and endpoint reset, should only be used
 * for fatal interface errors. */
OsStatus_t
MsdRecoveryReset(
    _In_ MsdDevice_t *Device)
{
    // Debug
    TRACE("MsdRecoveryReset()");

    // Perform an initial reset
    if (MsdReset(Device) != OsSuccess) {
        ERROR("Failed to reset interface");
        return OsError;
    }

    // Clear HALT features on both in and out endpoints
    if (UsbClearFeature(Device->Base.Driver, Device->Base.Device, 
        Device->Base.Device, Device->Control, USBPACKET_DIRECTION_ENDPOINT,
        Device->In->Address, USB_FEATURE_HALT) != TransferFinished
        || UsbClearFeature(Device->Base.Driver, Device->Base.Device, 
        Device->Base.UsbDevice, Device->Control, USBPACKET_DIRECTION_ENDPOINT,
        Device->Out->Address, USB_FEATURE_HALT) != TransferFinished) {
        ERROR("Failed to reset endpoints");
        return OsError;
    }

    // Restore to initial configuration
    if (UsbSetConfiguration(Device->Base.Driver, Device->Base.Device, 
        Device->Base.Device, Device->Control, 1) != TransferFinished) {
        ERROR("Failed to restore device configuration to 1");
        return OsError;
    }

    /* Reset Toggles */
    Device->EpIn->Toggle = 0;
    Device->EpOut->Toggle = 0;

    // Reset interface again
    return MsdReset(Device);
}

/* MsdGetMaximumLunCount
 * Retrieves the maximum logical unit count for the given msd-device. */
OsStatus_t
MsdGetMaximumLunCount(
    _In_ MsdDevice_t *Device)
{
    // Variables
    UsbTransferStatus_t Status;
    uint8_t MaxLuns;

    // Get Max LUNS is
    // 0xA1 | 0xFE | wIndex - Interface 
    // 1 Byte is expected back, values range between 0 - 15, 0-indexed
    Status = UsbExecutePacket(Device->Base.Driver, Device->Base.Device, 
        Device->Base.Device, Device->Control,
        USBPACKET_DIRECTION_IN | USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_INTERFACE,
        MSD_REQUEST_GET_MAX_LUN, 0, 0, (uint16_t)Device->Base.Interface.Id, 1, &MaxLuns);

    // If no multiple LUNS are supported, device may STALL
    // it says in the usbmassbulk spec 
    // but thats ok, it's not a functional stall
    if (Status == TransferFinished) {
        Device->Descriptor.LUNCount = (size_t)(MaxLuns & 0xF);
    }
    else {
        Device->Descriptor.LUNCount = 0;
    }

    // Done
    return OsSuccess;
}

/* MsdSCSICommmandConstruct
 * Constructs a new SCSI command structure from the information given. */
void
MsdSCSICommmandConstruct(
    _InOut_ MsdCommandBlock_t *CmdBlock,
    _In_ uint8_t ScsiCommand,
    _In_ uint64_t SectorLBA,
    _In_ uint32_t DataLen,
    _In_ uint16_t SectorSize)
{
    // Reset structure
    memset((void*)CmdBlock, 0, sizeof(MsdCommandBlock_t));

    // Set initial members
    CmdBlock->Signature = MSD_CBW_SIGNATURE;
    CmdBlock->Tag = MSD_TAG_SIGNATURE | ScsiCommand;
    CmdBlock->CommandBytes[0] = ScsiCommand;
    CmdBlock->DataLen = DataLen;

    // Switch between supported/implemented commands
    switch (ScsiCommand) {
        // Test Unit Ready - 6 (OUT)
        case SCSI_TEST_UNIT_READY: {
            CmdBlock->Flags = MSD_CBW_OUT;
            CmdBlock->Length = 6;
        } break;

        // Request Sense - 6 (IN)
        case SCSI_REQUEST_SENSE: {
            CmdBlock->Flags = MSD_CBW_IN;
            CmdBlock->Length = 6;
            CmdBlock->CommandBytes[4] = 18; // Response length
        } break;

        // Inquiry - 6 (IN)
        case SCSI_INQUIRY: {
            CmdBlock->Flags = MSD_CBW_IN;
            CmdBlock->Length = 6;
            CmdBlock->CommandBytes[4] = 36; // Response length
        } break;

        // Read Capacities - 10 (IN)
        case SCSI_READ_CAPACITY: {
            CmdBlock->Flags = MSD_CBW_IN;
            CmdBlock->Length = 10;

            // LBA
            CmdBlock->CommandBytes[2] = ((SectorLBA >> 24) & 0xFF);
            CmdBlock->CommandBytes[3] = ((SectorLBA >> 16) & 0xFF);
            CmdBlock->CommandBytes[4] = ((SectorLBA >> 8) & 0xFF);
            CmdBlock->CommandBytes[5] = (SectorLBA & 0xFF);
        } break;

        // Read Capacities - 16 (IN)
        case SCSI_READ_CAPACITY_16: {
            CmdBlock->Flags = MSD_CBW_IN;
            CmdBlock->Length = 16;
            CmdBlock->CommandBytes[1] = 0x10; // Service Action

            // LBA
            CmdBlock->CommandBytes[2] = ((SectorLBA >> 56) & 0xFF);
            CmdBlock->CommandBytes[3] = ((SectorLBA >> 48) & 0xFF);
            CmdBlock->CommandBytes[4] = ((SectorLBA >> 40) & 0xFF);
            CmdBlock->CommandBytes[5] = ((SectorLBA >> 32) & 0xFF);
            CmdBlock->CommandBytes[6] = ((SectorLBA >> 24) & 0xFF);
            CmdBlock->CommandBytes[7] = ((SectorLBA >> 16) & 0xFF);
            CmdBlock->CommandBytes[8] = ((SectorLBA >> 8) & 0xFF);
            CmdBlock->CommandBytes[9] = (SectorLBA & 0xFF);

            // Sector Count
            CmdBlock->CommandBytes[10] = ((DataLen << 24) & 0xFF);
            CmdBlock->CommandBytes[11] = ((DataLen << 16) & 0xFF);
            CmdBlock->CommandBytes[12] = ((DataLen << 8) & 0xFF);
            CmdBlock->CommandBytes[13] = (DataLen & 0xFF);
        }

        // Read - 6 (IN)
        case SCSI_READ_6: {
            uint8_t NumSectors = (uint8_t)(DataLen / SectorSize);
            if (DataLen % SectorSize) {
                NumSectors++;
            }
            CmdBlock->Flags = MSD_CBW_IN;
            CmdBlock->Length = 6;

            // LBA
            CmdBlock->CommandBytes[1] = ((SectorLBA >> 16) & 0x1F);
            CmdBlock->CommandBytes[2] = ((SectorLBA >> 8) & 0xFF);
            CmdBlock->CommandBytes[3] = (SectorLBA & 0xFF);

            // Sector Count
            CmdBlock->CommandBytes[4] = NumSectors & 0xFF;
        } break;

        // Read - 10 (IN)
        case SCSI_READ: {
            uint16_t NumSectors = (uint16_t)(DataLen / SectorSize);
            if (DataLen % SectorSize) {
                NumSectors++;
            }
            CmdBlock->Flags = MSD_CBW_IN;
            CmdBlock->Length = 10;

            // LBA
            CmdBlock->CommandBytes[2] = ((SectorLBA >> 24) & 0xFF);
            CmdBlock->CommandBytes[3] = ((SectorLBA >> 16) & 0xFF);
            CmdBlock->CommandBytes[4] = ((SectorLBA >> 8) & 0xFF);
            CmdBlock->CommandBytes[5] = (SectorLBA & 0xFF);

            // Sector Count
            CmdBlock->CommandBytes[7] = ((NumSectors << 8) & 0xFF);
            CmdBlock->CommandBytes[8] = (NumSectors & 0xFF);

        } break;

        // Read - 12 (IN)
        case SCSI_READ_12: {
            uint32_t NumSectors = (uint32_t)(DataLen / SectorSize);
            if (DataLen % SectorSize) {
                NumSectors++;
            }
            CmdBlock->Flags = MSD_CBW_IN;
            CmdBlock->Length = 12;

            // LBA
            CmdBlock->CommandBytes[2] = ((SectorLBA >> 24) & 0xFF);
            CmdBlock->CommandBytes[3] = ((SectorLBA >> 16) & 0xFF);
            CmdBlock->CommandBytes[4] = ((SectorLBA >> 8) & 0xFF);
            CmdBlock->CommandBytes[5] = (SectorLBA & 0xFF);

            // Sector Count
            CmdBlock->CommandBytes[6] = ((NumSectors << 24) & 0xFF);
            CmdBlock->CommandBytes[7] = ((NumSectors << 16) & 0xFF);
            CmdBlock->CommandBytes[8] = ((NumSectors << 8) & 0xFF);
            CmdBlock->CommandBytes[9] = (NumSectors & 0xFF);
        } break;

        // Read - 16 (IN)
        case SCSI_READ_16: {
            uint32_t NumSectors = (uint32_t)(DataLen / SectorSize);
            if (DataLen % SectorSize) {
                NumSectors++;
            }
            CmdBlock->Flags = MSD_CBW_IN;
            CmdBlock->Length = 16;

            // LBA
            CmdBlock->CommandBytes[2] = ((SectorLBA >> 56) & 0xFF);
            CmdBlock->CommandBytes[3] = ((SectorLBA >> 48) & 0xFF);
            CmdBlock->CommandBytes[4] = ((SectorLBA >> 40) & 0xFF);
            CmdBlock->CommandBytes[5] = ((SectorLBA >> 32) & 0xFF);
            CmdBlock->CommandBytes[6] = ((SectorLBA >> 24) & 0xFF);
            CmdBlock->CommandBytes[7] = ((SectorLBA >> 16) & 0xFF);
            CmdBlock->CommandBytes[8] = ((SectorLBA >> 8) & 0xFF);
            CmdBlock->CommandBytes[9] = (SectorLBA & 0xFF);

            // Sector Count
            CmdBlock->CommandBytes[10] = ((NumSectors << 24) & 0xFF);
            CmdBlock->CommandBytes[11] = ((NumSectors << 16) & 0xFF);
            CmdBlock->CommandBytes[12] = ((NumSectors << 8) & 0xFF);
            CmdBlock->CommandBytes[13] = (NumSectors & 0xFF);
        } break;

        // Write - 6 (OUT)
        case SCSI_WRITE_6: {
            uint8_t NumSectors = (uint8_t)(DataLen / SectorSize);
            if (DataLen % SectorSize) {
                NumSectors++;
            }
            CmdBlock->Flags = MSD_CBW_OUT;
            CmdBlock->Length = 6;

            // LBA
            CmdBlock->CommandBytes[1] = ((SectorLBA >> 16) & 0x1F);
            CmdBlock->CommandBytes[2] = ((SectorLBA >> 8) & 0xFF);
            CmdBlock->CommandBytes[3] = (SectorLBA & 0xFF);

            // Sector Count
            CmdBlock->CommandBytes[4] = NumSectors & 0xFF;
        } break;

        // Write - 10 (OUT)
        case SCSI_WRITE: {
            uint16_t NumSectors = (uint16_t)(DataLen / SectorSize);
            if (DataLen % SectorSize) {
                NumSectors++;
            }
            CmdBlock->Flags = MSD_CBW_OUT;
            CmdBlock->Length = 10;

            // LBA
            CmdBlock->CommandBytes[2] = ((SectorLBA >> 24) & 0xFF);
            CmdBlock->CommandBytes[3] = ((SectorLBA >> 16) & 0xFF);
            CmdBlock->CommandBytes[4] = ((SectorLBA >> 8) & 0xFF);
            CmdBlock->CommandBytes[5] = (SectorLBA & 0xFF);

            // Sector Count
            CmdBlock->CommandBytes[7] = ((NumSectors << 8) & 0xFF);
            CmdBlock->CommandBytes[8] = (NumSectors & 0xFF);
        } break;

        // Write - 12 (OUT)
        case SCSI_WRITE_12: {
            uint32_t NumSectors = (uint32_t)(DataLen / SectorSize);
            if (DataLen % SectorSize) {
                NumSectors++;
            }
            CmdBlock->Flags = MSD_CBW_OUT;
            CmdBlock->Length = 12;

            // LBA
            CmdBlock->CommandBytes[2] = ((SectorLBA >> 24) & 0xFF);
            CmdBlock->CommandBytes[3] = ((SectorLBA >> 16) & 0xFF);
            CmdBlock->CommandBytes[4] = ((SectorLBA >> 8) & 0xFF);
            CmdBlock->CommandBytes[5] = (SectorLBA & 0xFF);

            // Sector Count
            CmdBlock->CommandBytes[6] = ((NumSectors << 24) & 0xFF);
            CmdBlock->CommandBytes[7] = ((NumSectors << 16) & 0xFF);
            CmdBlock->CommandBytes[8] = ((NumSectors << 8) & 0xFF);
            CmdBlock->CommandBytes[9] = (NumSectors & 0xFF);
        } break;

        // Write - 16 (OUT)
        case SCSI_WRITE_16: {
            uint32_t NumSectors = (uint32_t)(DataLen / SectorSize);
            if (DataLen % SectorSize) {
                NumSectors++;
            }
            CmdBlock->Flags = MSD_CBW_OUT;
            CmdBlock->Length = 16;

            // LBA
            CmdBlock->CommandBytes[2] = ((SectorLBA >> 56) & 0xFF);
            CmdBlock->CommandBytes[3] = ((SectorLBA >> 48) & 0xFF);
            CmdBlock->CommandBytes[4] = ((SectorLBA >> 40) & 0xFF);
            CmdBlock->CommandBytes[5] = ((SectorLBA >> 32) & 0xFF);
            CmdBlock->CommandBytes[6] = ((SectorLBA >> 24) & 0xFF);
            CmdBlock->CommandBytes[7] = ((SectorLBA >> 16) & 0xFF);
            CmdBlock->CommandBytes[8] = ((SectorLBA >> 8) & 0xFF);
            CmdBlock->CommandBytes[9] = (SectorLBA & 0xFF);

            // Sector Count
            CmdBlock->CommandBytes[10] = ((NumSectors << 24) & 0xFF);
            CmdBlock->CommandBytes[11] = ((NumSectors << 16) & 0xFF);
            CmdBlock->CommandBytes[12] = ((NumSectors << 8) & 0xFF);
            CmdBlock->CommandBytes[13] = (NumSectors & 0xFF);
        } break;
    }
}

/* MsdSCSICommmandConstructUFI
 * Constructs a new SCSI command structure from the information given. */
void
MsdSCSICommmandConstructUFI(
    MsdCommandBlockUFI_t *CmdBlock,
    uint8_t ScsiCommand, 
    uint64_t SectorLBA, 
    uint32_t DataLen, 
    uint16_t SectorSize)
{
    // Reset structure
    memset((void*)CmdBlock, 0, sizeof(MsdCommandBlockUFI_t));
    
    // Set initial members
    CmdBlock->CommandBytes[0] = ScsiCommand;

    // Switch between supported/implemented commands
    switch (ScsiCommand) {
        // Request Sense - 6 (IN)
        case SCSI_REQUEST_SENSE: {
            CmdBlock->CommandBytes[4] = 18; // Response Length
        } break;

        // Inquiry - 6 (IN)
        case SCSI_INQUIRY: {
            CmdBlock->CommandBytes[4] = 36; // Response Length
        } break;

        // Read Capacities - 10 (IN)
        case SCSI_READ_CAPACITY: {
            // LBA
            CmdBlock->CommandBytes[2] = ((SectorLBA >> 24) & 0xFF);
            CmdBlock->CommandBytes[3] = ((SectorLBA >> 16) & 0xFF);
            CmdBlock->CommandBytes[4] = ((SectorLBA >> 8) & 0xFF);
            CmdBlock->CommandBytes[5] = (SectorLBA & 0xFF);
        } break;

        // Read - 10 (IN)
        case SCSI_READ: {
            uint16_t NumSectors = (uint16_t)(DataLen / SectorSize);
            if (DataLen % SectorSize) {
                NumSectors++;
            }

            // LBA
            CmdBlock->CommandBytes[2] = ((SectorLBA >> 24) & 0xFF);
            CmdBlock->CommandBytes[3] = ((SectorLBA >> 16) & 0xFF);
            CmdBlock->CommandBytes[4] = ((SectorLBA >> 8) & 0xFF);
            CmdBlock->CommandBytes[5] = (SectorLBA & 0xFF);

            // Sector Count
            CmdBlock->CommandBytes[7] = ((NumSectors << 8) & 0xFF);
            CmdBlock->CommandBytes[8] = (NumSectors & 0xFF);
        } break;

        // Write - 10 (OUT)
        case SCSI_WRITE: {
            uint16_t NumSectors = (uint16_t)(DataLen / SectorSize);
            if (DataLen % SectorSize) {
                NumSectors++;
            }

            // LBA
            CmdBlock->CommandBytes[2] = ((SectorLBA >> 24) & 0xFF);
            CmdBlock->CommandBytes[3] = ((SectorLBA >> 16) & 0xFF);
            CmdBlock->CommandBytes[4] = ((SectorLBA >> 8) & 0xFF);
            CmdBlock->CommandBytes[5] = (SectorLBA & 0xFF);

            // Sector Count
            CmdBlock->CommandBytes[7] = ((NumSectors << 8) & 0xFF);
            CmdBlock->CommandBytes[8] = (NumSectors & 0xFF);
        } break;
    }
}

/* MsdSanitizeResponse
 * Used for making sure the CSW we get back is valid */
UsbTransferStatus_t
MsdSanitizeResponse(
    _In_ MsdDevice_t *Device, 
    _In_ MsdCommandStatus_t *Csw)
{
    // Start out by checking if a phase error occured, in that case we are screwed
    if (Csw->Status == MSD_CSW_PHASE_ERROR) {
        MsdRecoveryReset(Device);
        return TransferInvalidData;
    }

    // Sanitize status
    if (Csw->Status != MSD_CSW_OK) {
        return TransferInvalidData;
    }

    // Sanitize signature/data integrity
    if (Csw->Signature != MSD_CSW_OK_SIGNATURE) {
        return TransferInvalidData;
    }
    
    // Sanitize tag/data integrity
    if ((Csw->Tag & 0xFFFFFF00) != MSD_TAG_SIGNATURE) {
        return TransferInvalidData;
    }

    // Everything should be fine
    return TransferFinished;
}

/* MsdSCSICommandIn
 * Perform an SCSI command of the type in. */
UsbTransferStatus_t 
MsdSCSICommandIn(
    _In_ MsdDevice_t *Device,
    _In_ uint8_t ScsiCommand,
    _In_ uint64_t SectorStart,
    _In_ uintptr_t DataAddress,
    _In_ size_t DataLength)
{
    // Variables
    UsbTransfer_t ControlTransfer;
    UsbTransfer_t DataTransfer;
    UsbTransferResult_t Result;

    // First step is to execute the control transfer, but we must handle
    // the default and UFI interface differently
    if (Device->Type == HardDrive) {
        // Construct our command build the usb transfer
        MsdSCSICommmandConstruct(Device->CommandBlock, ScsiCommand, SectorStart, 
            DataLength, (uint16_t)Device->Descriptor.SectorSize);
        UsbTransferInitialize(&ControlTransfer, Device->Base.Device, 
            Device->Out, BulkTransfer);
        UsbTransferOut(&ControlTransfer, Device->CommandBlockAddress, 
            sizeof(MsdCommandBlock_t), 0);
        UsbTransferQueue(Device->Base.Driver, Device->Base.Device, 
            &ControlTransfer, &Result);
    }
    else {
        // Variables
        MsdCommandBlockUFI_t UfiCommandBlock;

        // Construct our command build the usb transfer
        MsdSCSICommmandConstructUFI(&UfiCommandBlock, ScsiCommand, SectorStart
            DataLength, (uint16_t)Device->Descriptor.SectorSize);
        Result.Status = UsbExecutePacket(Device->Base.Driver, Device->Base.Device,
            Device->Base.Device, Device->Control, 
            USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_INTERFACE, 0, 0, 0, 
            (uint16_t)Device->Base.Interface.Id, 
            sizeof(MsdCommandBlockWrapUFI_t), &UfiCommandBlock);
    }

    // Sanitize for any transport errors
    if (Result.Status != TransferFinished) {
        if (Result.Status == TransferStalled) {
            MsdRecoveryReset(Device);
        }
        return Result.Status;
    }

    // The next step is construct and execute the data transfer
    // now that we have prepaired the device for control
    UsbTransferInitialize(&DataTransfer, Device->Base.Device, Device->In, BulkTransfer);
    if (DataLength != 0) {
        UsbTransferIn(&DataTransfer, DataAddress, DataLength, 0);
    }
    if (Device->Type == HardDrive) {
        UsbTransferIn(&DataTransfer, Device->StatusBlockAddress, sizeof(MsdCommandStatus_t), 0);
    }
    UsbTransferQueue(Device->Base.Driver, Device->Base.Device, &ControlTransfer, &Result);

    // Sanitize transfer response
    if (Result.Status != TransferFinished) {
        return Result.Status;
    }

    // If the transfer was not UFI sanitize the CSW
    if (Device->Type == HardDrive) {
        return MsdSanitizeResponse(Device, Device->StatusBlock);
    }
    else {
        return Result.Status;
    }
}

/* MsdSCSICommandOut
 * Perform an SCSI command of the type out. */
UsbTransferStatus_t 
MsdSCSICommandOut(
    _In_ MsdDevice_t *Device,
    _In_ uint8_t ScsiCommand,
    _In_ uint64_t SectorStart,
    _In_ uintptr_t DataAddress,
    _In_ size_t DataLength)
{
    // Variables
    UsbTransfer_t DataTransfer;
    UsbTransfer_t StatusTransfer;
    UsbTransferResult_t Result;

    // First step is to execute the data transfer, but we must handle
    // the default and UFI interface differently
    if (Device->Type == HardDrive) {
        // Construct our command build the usb transfer
        MsdSCSICommmandConstruct(Device->CommandBlock, ScsiCommand, SectorStart, 
            DataLength, (uint16_t)Device->Descriptor.SectorSize);
        UsbTransferInitialize(&DataTransfer, Device->Base.Device, 
            Device->Out, BulkTransfer);
        UsbTransferOut(&DataTransfer, Device->CommandBlockAddress, 
            sizeof(MsdCommandBlock_t), 0);
        UsbTransferOut(&DataTransfer, DataAddress, DataLength, 0);
        UsbTransferQueue(Device->Base.Driver, Device->Base.Device, 
            &DataTransfer, &Result);
    }
    else {
        // Variables
        MsdCommandBlockUFI_t UfiCommandBlock;

        // Construct our command build the usb transfer
        MsdSCSICommmandConstructUFI(&UfiCommandBlock, ScsiCommand, SectorStart
            DataLength, (uint16_t)Device->Descriptor.SectorSize);
        Result.Status = UsbExecutePacket(Device->Base.Driver, Device->Base.Device,
            Device->Base.Device, Device->Control, 
            USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_INTERFACE, 0, 0, 0, 
            (uint16_t)Device->Base.Interface.Id, 
            sizeof(MsdCommandBlockWrapUFI_t), &UfiCommandBlock);

        // Sanitize transfer status
        if (Result.Status != TransferFinished) {
            return Result.Status;
        }

        // Execute the data stage
        UsbTransferInitialize(&DataTransfer, Device->Base.Device, 
            Device->Out, BulkTransfer);
        UsbTransferOut(&DataTransfer, DataAddress, DataLength, 0);
        UsbTransferQueue(Device->Base.Driver, Device->Base.Device, 
            &DataTransfer, &Result);

        // Return the result
        return Result.Status;
    }

    // Sanitize for any transport errors
    if (Result.Status != TransferFinished) {
        if (Result.Status == TransferStalled) {
            MsdRecoveryReset(Device);
        }
        return Result.Status;
    }

    // The next step is construct and execute the status transfer
    // now that we have executed the data transfer
    UsbTransferInitialize(&StatusTransfer, Device->Base.Device, Device->In, BulkTransfer);
    UsbTransferIn(&StatusTransfer, Device->StatusBlockAddress, sizeof(MsdCommandStatus_t), 0);
    UsbTransferQueue(Device->Base.Driver, Device->Base.Device, &StatusTransfer, &Result);

    // If the transfer was not UFI sanitize the CSW
    if (Device->Type == HardDrive) {
        return MsdSanitizeResponse(Device, Device->StatusBlock);
    }
    else {
        return Result.Status;
    }
}

/* A combination of Test-Device-Ready & Requst Sense */
OsStatus_t
UsbMsdReadyDevice(
    MsdDevice_t *Device)
{
    // Variables
    ScsiSense_t *SenseBlock = NULL;
    uintptr_t *SenseBlockPhysical = 0;

    /* We don't use TDR on UFI's */
    if (Device->Type == HardDrive) {
        if (MsdSCSICommandIn(SCSI_TEST_UNIT_READY, Device, 0, NULL, 0)
            != TransferFinished) {
            LogFatal("USBM", "Failed to test");
            Device->IsReady = 0;
            return;
        }
    }

    /* Request Sense this time */
    if (MsdSCSICommandIn(SCSI_REQUEST_SENSE, Device, 0, &SenseBlock, sizeof(ScsiSense_t))
        != TransferFinished) {
        LogFatal("USBM", "Failed to sense");
        Device->IsReady = 0;
        return;
    }

    /* Sanity Sense */
    uint32_t ResponseCode = SenseBlock.ResponseStatus & 0x7F;
    uint32_t SenseKey = SenseBlock.Flags & 0xF;

    /* Must be either 0x70, 0x71, 0x72, 0x73 */
    if (ResponseCode >= 0x70 && ResponseCode <= 0x73) {
        LogInformation("USBM", "Sense Status: %s", SenseKeys[SenseKey]);
    }
    else {
        LogFatal("USBM", "Invalid Response Code: 0x%x", ResponseCode);
        Device->IsReady = 0;
        return;
    }
    Device->IsReady = 1;
}

/* Read Capacity of a device */
void UsbMsdReadCapacity(MsdDevice_t *Device, MCoreStorageDevice_t *sDevice)
{
    /* Buffer to store data */
    uint32_t CapBuffer[2] = { 0 };

    /* Send Command */
    if (UsbMsdSendSCSICommandIn(SCSI_READ_CAPACITY, Device, 0, &CapBuffer, 8)
        != TransferFinished)
        return;

    /* We have to switch byte order, but first,
     * lets sanity */
    if (CapBuffer[0] == 0xFFFFFFFF)
    {
        /* CapBuffer16 */
        ScsiExtendedCaps_t ExtendedCaps;

        /* Do extended */
        if (UsbMsdSendSCSICommandIn(SCSI_READ_CAPACITY_16, Device, 0, &ExtendedCaps, sizeof(ScsiExtendedCaps_t))
            != TransferFinished)
            return;

        /* Reverse Byte Order */
        sDevice->SectorCount = rev64(ExtendedCaps.SectorCount) + 1;
        sDevice->SectorSize = rev32(ExtendedCaps.SectorSize);
        Device->SectorSize = sDevice->SectorSize;
        Device->IsExtended = 1;

        /* Done */
        return;
    }

    /* Reverse Byte Order */
    sDevice->SectorCount = (uint64_t)rev32(CapBuffer[0]) + 1;
    sDevice->SectorSize = rev32(CapBuffer[1]);
    Device->SectorSize = sDevice->SectorSize;
}

/* Read & Write Sectors */
int
UsbMsdReadSectors(
    MsdDevice_t *Device, 
    uint64_t SectorLBA, 
    void *Buffer, 
    size_t BufferLength)
{
    /* Store result */
    UsbTransferStatus_t Result;

    /* Send */
    Result = UsbMsdSendSCSICommandIn(MsdData->IsExtended == 0 ? SCSI_READ : SCSI_READ_16,
        MsdData, SectorLBA, Buffer, BufferLength);

    /* Sanity */
    if (Result != TransferFinished)
        return -1;
    else
        return 0;
}

int
UsbMsdWriteSectors(
    MsdDevice_t *Device, 
    uint64_t SectorLBA, 
    void *Buffer, 
    size_t BufferLength)
{
    /* Store result */
    UsbTransferStatus_t Result;

    /* Send */
    Result = UsbMsdSendSCSICommandOut(MsdData->IsExtended == 0 ? SCSI_WRITE : SCSI_WRITE_16,
        MsdData, SectorLBA, Buffer, BufferLength);

    /* Sanity */
    if (Result != TransferFinished)
        return -1;
    else
        return 0;
}
