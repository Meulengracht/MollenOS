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
 *  - UFI Protocol Implementation
 */
#define __TRACE

/* Includes
 * - System */
#include <os/utils.h>
#include <os/driver/usb.h>
#include "../msd.h"

/* UfiConstructCommand
 * Constructs a new SCSI command structure from the information given. */
void
UfiConstructCommand(
    _InOut_ MsdCommandBlockUFI_t *CmdBlock,
    _In_ uint8_t ScsiCommand, 
    _In_ uint64_t SectorLBA, 
    _In_ uint32_t DataLen, 
    _In_ uint16_t SectorSize)
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
            CmdBlock->CommandBytes[7] = ((NumSectors >> 8) & 0xFF);
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
            CmdBlock->CommandBytes[7] = ((NumSectors >> 8) & 0xFF);
            CmdBlock->CommandBytes[8] = (NumSectors & 0xFF);
        } break;
    }
}

/* UfiInitialize 
 * Validates the available endpoints and initializes the device. */
OsStatus_t
UfiInitialize(
    _In_ MsdDevice_t *Device)
{
    // Sanitize found endpoints
    if (Device->In == NULL || Device->Out == NULL) {
        ERROR("Either in or out endpoint not available on device");
        return OsError;
    }

    // If we are CBI and not CB, there must be interrupt
    if (Device->Protocol == ProtocolCBI && Device->Interrupt == NULL) {
        ERROR("Protocol is CBI, but interrupt endpoint does not exist");
        return OsError;
    }

    // Reset data toggles for bulk-endpoints
    if (UsbEndpointReset(Device->Base.DriverId, Device->Base.DeviceId, 
        &Device->Base.Device, Device->In) != OsSuccess) {
        ERROR("Failed to reset endpoint (in)");
        return OsError;
    }
    if (UsbEndpointReset(Device->Base.DriverId, Device->Base.DeviceId, 
        &Device->Base.Device, Device->Out) != OsSuccess) {
        ERROR("Failed to reset endpoint (out)");
        return OsError;
    }

    // Done
    return OsSuccess;
}

/* UfiSendCommand
 * Sends a new command on the ufi protocol. */
UsbTransferStatus_t 
UfiSendCommand(
    _In_ MsdDevice_t *Device,
    _In_ uint8_t ScsiCommand,
    _In_ uint64_t SectorStart,
    _In_ uintptr_t DataAddress,
    _In_ size_t DataLength)
{
    // Variables
    MsdCommandBlockUFI_t UfiCommandBlock;
    UsbTransferResult_t Result  = { 0 };

    // Debug
    TRACE("UfiSendCommand(Command %u, Start %u, Length %u)",
        ScsiCommand, LODWORD(SectorStart), DataLength);

    // Construct our command build the usb transfer
    UfiConstructCommand(&UfiCommandBlock, ScsiCommand, SectorStart,
        DataLength, (uint16_t)Device->Descriptor.SectorSize);
    Result.Status = UsbExecutePacket(Device->Base.DriverId, Device->Base.DeviceId,
        &Device->Base.Device, Device->Control, 
        USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_INTERFACE, 0, 0, 0, 
        (uint16_t)Device->Base.Interface.Id, 
        sizeof(MsdCommandBlockUFI_t), &UfiCommandBlock);

    // Sanitize for any transport errors
    if (Result.Status != TransferFinished) {
        ERROR("Failed to send the CBW command, transfer-code %u", Result.Status);
    }
    return Result.Status;
}

/* UfiReadData
 * Tries to read a ufi data response from the device. */
UsbTransferStatus_t 
UfiReadData(
    _In_ MsdDevice_t *Device,
    _In_ uintptr_t DataAddress,
    _In_ size_t DataLength,
    _Out_ size_t *BytesRead)
{
    // Variables
    UsbTransferResult_t Result  = { 0 };
    UsbTransfer_t DataStage     = { 0 };

    // Perform the transfer
    UsbTransferInitialize(&DataStage, &Device->Base.Device, 
        Device->In, BulkTransfer);
    UsbTransferIn(&DataStage, DataAddress, DataLength, 0);
    UsbTransferQueue(Device->Base.DriverId, Device->Base.DeviceId, 
        &DataStage, &Result);
    
    // Sanitize for any transport errors
    if (Result.Status != TransferFinished) {
        ERROR("Data-stage failed with status %u, cleaning up bulk-in", Result.Status);
        // @todo handle
    }

    // Return state and update out
    *BytesRead = Result.BytesTransferred;
    return Result.Status;
}

/* UfiWriteData
 * Tries to write a bulk data packet to the device. */
UsbTransferStatus_t 
UfiWriteData(
    _In_ MsdDevice_t *Device,
    _In_ uintptr_t DataAddress,
    _In_ size_t DataLength,
    _Out_ size_t *BytesWritten)
{
    // Variables
    UsbTransferResult_t Result  = { 0 };
    UsbTransfer_t DataStage     = { 0 };

    // Perform the data-stage
    UsbTransferInitialize(&DataStage, &Device->Base.Device, 
        Device->Out, BulkTransfer);
    UsbTransferOut(&DataStage, DataAddress, DataLength, 0);
    UsbTransferQueue(Device->Base.DriverId, Device->Base.DeviceId, 
        &DataStage, &Result);

    // Sanitize for any transport errors
    if (Result.Status != TransferFinished) {
        ERROR("Data-stage failed with status %u, cleaning up bulk-out", Result.Status);
        // @todo handle
    }

    // Return state and update out
    *BytesWritten = Result.BytesTransferred;
    return Result.Status;
}

/* UfiGetStatus
 * The status stage is not used in the ufi-protocol. Not
 * implemented. */
UsbTransferStatus_t 
UfiGetStatus(
    _In_ MsdDevice_t *Device)
{
    // Unused
    _CRT_UNUSED(Device);
    return TransferFinished;
}

/* Global 
 * - Static function table */
MsdOperations_t UfiOperations = {
    UfiInitialize,
    UfiSendCommand,
    UfiReadData,
    UfiWriteData,
    UfiGetStatus
};
