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
 *  - Bulk Protocol Implementation
 */
//#define __TRACE

#define BULK_RESET      0x1
#define BULK_RESET_IN   0x2
#define BULK_RESET_OUT  0x4
#define BULK_RESET_ALL  (BULK_RESET | BULK_RESET_IN | BULK_RESET_OUT)

#include <usb/usb.h>
#include <ddk/utils.h>
#include "../msd.h"

oserr_t
BulkReset(
    _In_ MsdDevice_t *Device)
{
    enum USBTransferCode Status     = USBTRANSFERCODE_NAK;
    int                 Iterations = 0;

    // Reset is 
    // 0x21 | 0xFF | wIndex - Interface
    while (Status == USBTRANSFERCODE_NAK) {
        Status = UsbExecutePacket(&Device->Device->DeviceContext,
            USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_INTERFACE,
            MSD_REQUEST_RESET, 0, 0, (uint16_t)Device->InterfaceId, 0, NULL);
        Iterations++;
    }

    TRACE("Reset done after %i iterations", Iterations);
    if (Status != USBTRANSFERCODE_SUCCESS) {
        ERROR("Reset bulk returned error %u", Status);
        return OS_EUNKNOWN;
    }
    else {
        return OS_EOK;
    }
}

static oserr_t
ClearAndResetEndpoint(
    _In_ MsdDevice_t*               device,
    _In_ usb_endpoint_descriptor_t* endpoint)
{
    enum USBTransferCode status = UsbClearFeature(&device->Device->DeviceContext,
        USBPACKET_DIRECTION_ENDPOINT, USB_ENDPOINT_ADDRESS(endpoint->Address), 
        USB_FEATURE_HALT);
    if (status != USBTRANSFERCODE_SUCCESS) {
        return OS_EUNKNOWN;
    }
    
    return UsbEndpointReset(&device->Device->DeviceContext,
        USB_ENDPOINT_ADDRESS(endpoint->Address));
}

oserr_t
BulkResetRecovery(
    _In_ MsdDevice_t* device,
    _In_ int          resetType)
{
    oserr_t status;
    
    // Debug
    TRACE("MsdRecoveryReset(Type %i)", resetType);

    // Perform an initial reset
    if (resetType & BULK_RESET) {
        status = BulkReset(device);
        if (status != OS_EOK) {
            ERROR("Failed to reset bulk interface");
            return OS_EUNKNOWN;
        }
    }

    // Clear HALT/STALL features on both in and out endpoints
    if (resetType & BULK_RESET_IN) {
        status = ClearAndResetEndpoint(device, device->In);
        if (status != OS_EOK) {
            ERROR("Failed to clear STALL on endpoint (in)");
            return status;
        }
    }
    
    if (resetType & BULK_RESET_OUT) {
        status = ClearAndResetEndpoint(device, device->Out);
        if (status != OS_EOK) {
            ERROR("Failed to clear STALL on endpoint (out)");
            return status;
        }
    }
    return status;
}

void
BulkScsiCommandConstruct(
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
    CmdBlock->DataLength = DataLen;

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
            CmdBlock->CommandBytes[10] = ((DataLen >> 24) & 0xFF);
            CmdBlock->CommandBytes[11] = ((DataLen >> 16) & 0xFF);
            CmdBlock->CommandBytes[12] = ((DataLen >> 8) & 0xFF);
            CmdBlock->CommandBytes[13] = (DataLen & 0xFF);
        } break;

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
            CmdBlock->CommandBytes[4] = NumSectors;
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
            CmdBlock->CommandBytes[7] = ((NumSectors >> 8) & 0xFF);
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
            CmdBlock->CommandBytes[6] = ((NumSectors >> 24) & 0xFF);
            CmdBlock->CommandBytes[7] = ((NumSectors >> 16) & 0xFF);
            CmdBlock->CommandBytes[8] = ((NumSectors >> 8) & 0xFF);
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
            CmdBlock->CommandBytes[10] = ((NumSectors >> 24) & 0xFF);
            CmdBlock->CommandBytes[11] = ((NumSectors >> 16) & 0xFF);
            CmdBlock->CommandBytes[12] = ((NumSectors >> 8) & 0xFF);
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
            CmdBlock->CommandBytes[4] = NumSectors;
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
            CmdBlock->CommandBytes[7] = ((NumSectors >> 8) & 0xFF);
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
            CmdBlock->CommandBytes[6] = ((NumSectors >> 24) & 0xFF);
            CmdBlock->CommandBytes[7] = ((NumSectors >> 16) & 0xFF);
            CmdBlock->CommandBytes[8] = ((NumSectors >> 8) & 0xFF);
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
            CmdBlock->CommandBytes[10] = ((NumSectors >> 24) & 0xFF);
            CmdBlock->CommandBytes[11] = ((NumSectors >> 16) & 0xFF);
            CmdBlock->CommandBytes[12] = ((NumSectors >> 8) & 0xFF);
            CmdBlock->CommandBytes[13] = (NumSectors & 0xFF);
        } break;
    }
}

oserr_t
BulkInitialize(
    _In_ MsdDevice_t *Device)
{
    // Sanitize found endpoints
    if (Device->In == NULL || Device->Out == NULL) {
        ERROR("Either in or out endpoint not available on device");
        return OS_EUNKNOWN;
    }

    // Perform a bulk reset
    if (BulkReset(Device) != OS_EOK) {
        ERROR("Failed to reset the bulk interface");
        return OS_EUNKNOWN;
    }

    // Reset data toggles for bulk-endpoints
    if (UsbEndpointReset(&Device->Device->DeviceContext,
            USB_ENDPOINT_ADDRESS(Device->In->Address)) != OS_EOK) {
        ERROR("Failed to reset endpoint (in)");
        return OS_EUNKNOWN;
    }
    if (UsbEndpointReset(&Device->Device->DeviceContext, 
            USB_ENDPOINT_ADDRESS(Device->Out->Address)) != OS_EOK) {
        ERROR("Failed to reset endpoint (out)");
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

enum USBTransferCode
MsdSanitizeResponse(
    _In_ MsdDevice_t *Device, 
    _In_ MsdCommandStatus_t *Csw)
{
    // Check for phase errors
    if (Csw->Status == MSD_CSW_PHASE_ERROR) {
        ERROR("Phase error returned in CSW.");
        return USBTRANSFERCODE_INVALID;
    }

    // Sanitize signature/data integrity
    if (Csw->Signature != MSD_CSW_OK_SIGNATURE) {
        ERROR("CSW: Signature is invalid: 0x%x", Csw->Signature);
        return USBTRANSFERCODE_INVALID;
    }
    
    // Sanitize tag/data integrity
    if ((Csw->Tag & 0xFFFFFF00) != MSD_TAG_SIGNATURE) {
        ERROR("CSW: Tag is invalid: 0x%x", Csw->Tag);
        return USBTRANSFERCODE_INVALID;
    }

    // Sanitize status
    if (Csw->Status != MSD_CSW_OK) {
        ERROR("CSW: Status is invalid: 0x%x", Csw->Status);
        return USBTRANSFERCODE_INVALID;
    }

    // Everything should be fine
    return USBTRANSFERCODE_SUCCESS;
}

enum USBTransferCode
BulkSendCommand(
        _In_ MsdDevice_t *Device,
        _In_ uint8_t      ScsiCommand,
        _In_ uint64_t     SectorStart,
        _In_ uuid_t       BufferHandle,
        _In_ size_t       BufferOffset,
        _In_ size_t       DataLength)
{
    enum USBTransferCode Result;
    USBTransfer_t       CommandStage;
    size_t              bytesTransferred;

    // Debug
    TRACE("BulkSendCommand(Command %u, Start %u, Length %u)",
        ScsiCommand, LODWORD(SectorStart), DataLength);

    // Construct our command build the usb transfer
    BulkScsiCommandConstruct(Device->CommandBlock, ScsiCommand, SectorStart, 
        DataLength, (uint16_t)Device->Descriptor.SectorSize);
    UsbTransferInitialize(
            &CommandStage,
            &Device->Device->DeviceContext,
            Device->Out,
            USBTRANSFER_TYPE_BULK,
            USBTRANSFER_DIRECTION_OUT,
            0,
            dma_pool_handle(UsbRetrievePool()),
            dma_pool_offset(UsbRetrievePool(), Device->CommandBlock),
            sizeof(MsdCommandBlock_t)
    );
    Result = UsbTransferQueue(&Device->Device->DeviceContext, &CommandStage, &bytesTransferred);

    // Sanitize for any transport errors
    if (Result != USBTRANSFERCODE_SUCCESS) {
        ERROR("Failed to send the CBW command, transfer-code %u", Result);
        if (Result == USBTRANSFERCODE_STALL) {
            ERROR("Performing a recovery-reset on device.");
            if (BulkResetRecovery(Device, BULK_RESET_ALL) != OS_EOK) {
                ERROR("Failed to reset device, it is now unusable.");
            }
        }
    }
    return Result;
}

/* BulkReadData
 * Tries to read a bulk data response from the device. */
enum USBTransferCode
BulkReadData(
        _In_  MsdDevice_t* Device,
        _In_  uuid_t       BufferHandle,
        _In_  size_t       BufferOffset,
        _In_  size_t       DataLength,
        _Out_ size_t*      BytesRead)
{
    enum USBTransferCode transferStatus;
    USBTransfer_t       dataStage;
    size_t              bytesTransferred = 0;

    UsbTransferInitialize(
            &dataStage,
            &Device->Device->DeviceContext,
            Device->In,
            USBTRANSFER_TYPE_BULK,
            USBTRANSFER_DIRECTION_IN,
            0,
            BufferHandle,
            BufferOffset,
            DataLength
    );
    transferStatus = UsbTransferQueue(&Device->Device->DeviceContext, &dataStage, &bytesTransferred);
    
    // Sanitize for any transport errors
    // The host shall accept the data received.
    // The host shall clear the Bulk-In pipe.
    if (transferStatus != USBTRANSFERCODE_SUCCESS) {
        ERROR("Data-stage failed with status %i, cleaning up bulk-in", transferStatus);
        if (transferStatus == USBTRANSFERCODE_STALL) {
            BulkResetRecovery(Device, BULK_RESET_IN);
        }
        else {
            // Fatal error
            BulkResetRecovery(Device, BULK_RESET_ALL);
        }
    }

    // Return state and update out
    *BytesRead = bytesTransferred;
    return transferStatus;
}

enum USBTransferCode
BulkWriteData(
        _In_  MsdDevice_t* Device,
        _In_  uuid_t       BufferHandle,
        _In_  size_t       BufferOffset,
        _In_  size_t       DataLength,
        _Out_ size_t*      BytesWritten)
{
    enum USBTransferCode Result;
    USBTransfer_t       DataStage;
    size_t              bytesTransferred;

    // Perform the data-stage
    UsbTransferInitialize(
            &DataStage,
            &Device->Device->DeviceContext,
            Device->Out,
            USBTRANSFER_TYPE_BULK,
            USBTRANSFER_DIRECTION_OUT,
            0,
            BufferHandle,
            BufferOffset,
            DataLength
    );
    Result = UsbTransferQueue(&Device->Device->DeviceContext, &DataStage, &bytesTransferred);

    // Sanitize for any transport errors
    // The host shall accept the data received.
    // The host shall clear the Bulk-In pipe.
    if (Result != USBTRANSFERCODE_SUCCESS) {
        ERROR("Data-stage failed with status %u, cleaning up bulk-out", Result);
        if (Result == USBTRANSFERCODE_STALL) {
            BulkResetRecovery(Device, BULK_RESET_OUT);
        }
        else {
            // Fatal error
            BulkResetRecovery(Device, BULK_RESET_ALL);
        }
    }

    // Return state
    *BytesWritten = bytesTransferred;
    return Result;
}

enum USBTransferCode
BulkGetStatus(
    _In_ MsdDevice_t* Device)
{
    enum USBTransferCode Result;
    USBTransfer_t       StatusStage;
    size_t              bytesTransferred;
    TRACE("BulkGetStatus()");

    UsbTransferInitialize(
            &StatusStage,
            &Device->Device->DeviceContext,
            Device->In,
            USBTRANSFER_TYPE_BULK,
            USBTRANSFER_DIRECTION_IN,
            0,
            dma_pool_handle(UsbRetrievePool()),
            dma_pool_offset(UsbRetrievePool(), Device->StatusBlock),
            sizeof(MsdCommandStatus_t)
    );
    Result = UsbTransferQueue(&Device->Device->DeviceContext, &StatusStage, &bytesTransferred);

    // Sanitize for any transport errors
    // On a STALL condition receiving the CSW, then:
    // The host shall clear the Bulk-In pipe.
    // The host shall again attempt to receive the CSW.
    if (Result != USBTRANSFERCODE_SUCCESS) {
        if (Result == USBTRANSFERCODE_STALL) {
            BulkResetRecovery(Device, BULK_RESET_IN);
            return BulkGetStatus(Device);
        }
        else {
            ERROR("Failed to retrieve the CSW block, transfer-code %u", Result);
        }
        return Result;
    }
    else {        
        // If the host receives a CSW which is not valid, 
        // then the host shall perform a Reset Recovery. If the host receives
        // a CSW which is not meaningful, then the host may perform a Reset Recovery.
        return MsdSanitizeResponse(Device, Device->StatusBlock);
    }
}

MsdOperations_t BulkOperations = {
    BulkInitialize,
    BulkSendCommand,
    BulkReadData,
    BulkWriteData,
    BulkGetStatus
};
