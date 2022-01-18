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
 *  - UFI Protocol Implementation
 */
#define __TRACE

#include <usb/usb.h>
#include <ddk/utils.h>
#include "../msd.h"

void
UfiConstructCommand(
    _In_ MsdCommandBlockUFI_t *CmdBlock,
    _In_ uint8_t  ScsiCommand, 
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
    if (UsbEndpointReset(&Device->Base.DeviceContext,
        USB_ENDPOINT_ADDRESS(Device->In->Address)) != OsSuccess) {
        ERROR("Failed to reset endpoint (in)");
        return OsError;
    }
    if (UsbEndpointReset(&Device->Base.DeviceContext,
        USB_ENDPOINT_ADDRESS(Device->Out->Address)) != OsSuccess) {
        ERROR("Failed to reset endpoint (out)");
        return OsError;
    }

    return OsSuccess;
}

UsbTransferStatus_t 
UfiSendCommand(
    _In_ MsdDevice_t* Device,
    _In_ uint8_t      ScsiCommand,
    _In_ uint64_t     SectorStart,
    _In_ UUId_t       BufferHandle,
    _In_ size_t       BufferOffset,
    _In_ size_t       DataLength)
{
    MsdCommandBlockUFI_t UfiCommandBlock;
    UsbTransferStatus_t  Result;

    // Debug
    TRACE("UfiSendCommand(Command %u, Start %u, Length %u)",
        ScsiCommand, LODWORD(SectorStart), DataLength);

    // Construct our command build the usb transfer
    UfiConstructCommand(&UfiCommandBlock, ScsiCommand, SectorStart,
        DataLength, (uint16_t)Device->Descriptor.SectorSize);
    Result = UsbExecutePacket(&Device->Base.DeviceContext, 
        USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_INTERFACE, 0, 0, 0, 
        (uint16_t)Device->InterfaceId, 
        sizeof(MsdCommandBlockUFI_t), &UfiCommandBlock);

    // Sanitize for any transport errors
    if (Result != TransferFinished) {
        ERROR("Failed to send the CBW command, transfer-code %u", Result);
    }
    return Result;
}

UsbTransferStatus_t 
UfiReadData(
    _In_  MsdDevice_t* Device,
    _In_  UUId_t       BufferHandle,
    _In_  size_t       BufferOffset,
    _In_  size_t       DataLength,
    _Out_ size_t*      BytesRead)
{
    UsbTransferStatus_t Result;
    UsbTransfer_t       DataStage;

    UsbTransferInitialize(&DataStage, &Device->Base.DeviceContext, 
        Device->In, USB_TRANSFER_BULK, 0);
    UsbTransferIn(&DataStage, BufferHandle, BufferOffset, DataLength, 0);
    Result = UsbTransferQueue(&Device->Base.DeviceContext, &DataStage, BytesRead);
    
    // Sanitize for any transport errors
    if (Result != TransferFinished) {
        ERROR("Data-stage failed with status %u, cleaning up bulk-in", Result);
        // @todo handle
    }
    
    return Result;
}

UsbTransferStatus_t 
UfiWriteData(
    _In_  MsdDevice_t* Device,
    _In_  UUId_t       BufferHandle,
    _In_  size_t       BufferOffset,
    _In_  size_t       DataLength,
    _Out_ size_t*      BytesWritten)
{
    UsbTransferStatus_t Result;
    UsbTransfer_t       DataStage;

    // Perform the data-stage
    UsbTransferInitialize(&DataStage, &Device->Base.DeviceContext, 
        Device->Out, USB_TRANSFER_BULK, 0);
    UsbTransferOut(&DataStage, BufferHandle, BufferOffset, DataLength, 0);
    Result = UsbTransferQueue(&Device->Base.DeviceContext, &DataStage, BytesWritten);

    // Sanitize for any transport errors
    if (Result != TransferFinished) {
        ERROR("Data-stage failed with status %u, cleaning up bulk-out", Result);
        // @todo handle
    }
    
    return Result;
}

/* UfiGetStatus
 * The status stage is not used in the ufi-protocol. Not implemented. */
UsbTransferStatus_t 
UfiGetStatus(
    _In_ MsdDevice_t *Device)
{
    // Unused
    _CRT_UNUSED(Device);
    return TransferFinished;
}

MsdOperations_t UfiOperations = {
    UfiInitialize,
    UfiSendCommand,
    UfiReadData,
    UfiWriteData,
    UfiGetStatus
};
