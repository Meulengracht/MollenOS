/**
 * Copyright 2023, Philip Meulengracht
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

static oserr_t
__ResetBulk(
        _In_ MSDDevice_t* device)
{
    enum USBTransferCode code  = USBTRANSFERCODE_NAK;
    int                  tries = 0;

    // Reset is 
    // 0x21 | 0xFF | wIndex - Interface
    while (code == USBTRANSFERCODE_NAK) {
        code = UsbExecutePacket(
                &device->Device->DeviceContext,
                USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_INTERFACE,
                MSD_REQUEST_RESET,
                0, 0,
                (uint16_t)device->InterfaceId,
                0,
                NULL
        );
        tries++;
    }

    TRACE("__ResetBulk: done after %i iterations", tries);
    if (code != USBTRANSFERCODE_SUCCESS) {
        ERROR("__ResetBulk: failed to reset interface: %u", code);
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

static oserr_t
__ClearAndResetEndpoint(
        _In_ MSDDevice_t*               device,
        _In_ usb_endpoint_descriptor_t* endpoint)
{
    enum USBTransferCode status = UsbClearFeature(
            &device->Device->DeviceContext,
            USBPACKET_DIRECTION_ENDPOINT,
            USB_ENDPOINT_ADDRESS(endpoint->Address),
            USB_FEATURE_HALT
    );
    if (status != USBTRANSFERCODE_SUCCESS) {
        ERROR("__ClearAndResetEndpoint: failed to clear HALT: %u", status);
        return OS_EUNKNOWN;
    }
    
    return UsbEndpointReset(
            &device->Device->DeviceContext,
            USB_ENDPOINT_ADDRESS(endpoint->Address)
    );
}

static oserr_t
__ResetRecovery(
        _In_ MSDDevice_t* device,
        _In_ int          resetType)
{
    oserr_t oserr = OS_EOK;
    TRACE("__ResetRecovery(type=%i)", resetType);

    // Perform an initial reset
    if (resetType & BULK_RESET) {
        oserr = __ResetBulk(device);
        if (oserr != OS_EOK) {
            ERROR("__ResetRecovery: failed to reset bulk interface: %u", oserr);
            return oserr;
        }
    }

    // Clear HALT/STALL features on both in and out endpoints
    if (resetType & BULK_RESET_IN) {
        oserr = __ClearAndResetEndpoint(device, device->In);
        if (oserr != OS_EOK) {
            ERROR("__ResetRecovery: failed to clear STALL on endpoint (in)");
            return oserr;
        }
    }
    
    if (resetType & BULK_RESET_OUT) {
        oserr = __ClearAndResetEndpoint(device, device->Out);
        if (oserr != OS_EOK) {
            ERROR("__ResetRecovery: failed to clear STALL on endpoint (out)");
            return oserr;
        }
    }
    return oserr;
}

static void
__ConstructSCSICommand(
    _In_ MsdCommandBlock_t* commandBlock,
    _In_ uint8_t            scsiCommand,
    _In_ uint64_t           sectorLba,
    _In_ uint32_t           dataLen,
    _In_ uint16_t           sectorSize)
{
    memset((void*)commandBlock, 0, sizeof(MsdCommandBlock_t));
    commandBlock->Signature = MSD_CBW_SIGNATURE;
    commandBlock->Tag       = MSD_TAG_SIGNATURE | scsiCommand;
    commandBlock->CommandBytes[0] = scsiCommand;
    commandBlock->DataLength = dataLen;

    // Switch between supported/implemented commands
    switch (scsiCommand) {
        // Test Unit Ready - 6 (OUT)
        case SCSI_TEST_UNIT_READY: {
            commandBlock->Flags  = MSD_CBW_OUT;
            commandBlock->Length = 6;
        } break;

        // Request Sense - 6 (IN)
        case SCSI_REQUEST_SENSE: {
            commandBlock->Flags  = MSD_CBW_IN;
            commandBlock->Length = 6;
            commandBlock->CommandBytes[4] = 18; // Response length
        } break;

        // Inquiry - 6 (IN)
        case SCSI_INQUIRY: {
            commandBlock->Flags  = MSD_CBW_IN;
            commandBlock->Length = 6;
            commandBlock->CommandBytes[4] = 36; // Response length
        } break;

        // Read Capacities - 10 (IN)
        case SCSI_READ_CAPACITY: {
            commandBlock->Flags  = MSD_CBW_IN;
            commandBlock->Length = 10;

            // LBA
            commandBlock->CommandBytes[2] = ((sectorLba >> 24) & 0xFF);
            commandBlock->CommandBytes[3] = ((sectorLba >> 16) & 0xFF);
            commandBlock->CommandBytes[4] = ((sectorLba >> 8) & 0xFF);
            commandBlock->CommandBytes[5] = (sectorLba & 0xFF);
        } break;

        // Read Capacities - 16 (IN)
        case SCSI_READ_CAPACITY_16: {
            commandBlock->Flags  = MSD_CBW_IN;
            commandBlock->Length = 16;
            commandBlock->CommandBytes[1] = 0x10; // Service Action

            // LBA
            commandBlock->CommandBytes[2] = ((sectorLba >> 56) & 0xFF);
            commandBlock->CommandBytes[3] = ((sectorLba >> 48) & 0xFF);
            commandBlock->CommandBytes[4] = ((sectorLba >> 40) & 0xFF);
            commandBlock->CommandBytes[5] = ((sectorLba >> 32) & 0xFF);
            commandBlock->CommandBytes[6] = ((sectorLba >> 24) & 0xFF);
            commandBlock->CommandBytes[7] = ((sectorLba >> 16) & 0xFF);
            commandBlock->CommandBytes[8] = ((sectorLba >> 8) & 0xFF);
            commandBlock->CommandBytes[9] = (sectorLba & 0xFF);

            // Sector Count
            commandBlock->CommandBytes[10] = ((dataLen >> 24) & 0xFF);
            commandBlock->CommandBytes[11] = ((dataLen >> 16) & 0xFF);
            commandBlock->CommandBytes[12] = ((dataLen >> 8) & 0xFF);
            commandBlock->CommandBytes[13] = (dataLen & 0xFF);
        } break;

        // Read - 6 (IN)
        case SCSI_READ_6: {
            uint8_t NumSectors = (uint8_t)(dataLen / sectorSize);
            if (dataLen % sectorSize) {
                NumSectors++;
            }
            commandBlock->Flags  = MSD_CBW_IN;
            commandBlock->Length = 6;

            // LBA
            commandBlock->CommandBytes[1] = ((sectorLba >> 16) & 0x1F);
            commandBlock->CommandBytes[2] = ((sectorLba >> 8) & 0xFF);
            commandBlock->CommandBytes[3] = (sectorLba & 0xFF);

            // Sector Count
            commandBlock->CommandBytes[4] = NumSectors;
        } break;

        // Read - 10 (IN)
        case SCSI_READ: {
            uint16_t NumSectors = (uint16_t)(dataLen / sectorSize);
            if (dataLen % sectorSize) {
                NumSectors++;
            }
            commandBlock->Flags  = MSD_CBW_IN;
            commandBlock->Length = 10;

            // LBA
            commandBlock->CommandBytes[2] = ((sectorLba >> 24) & 0xFF);
            commandBlock->CommandBytes[3] = ((sectorLba >> 16) & 0xFF);
            commandBlock->CommandBytes[4] = ((sectorLba >> 8) & 0xFF);
            commandBlock->CommandBytes[5] = (sectorLba & 0xFF);

            // Sector Count
            commandBlock->CommandBytes[7] = ((NumSectors >> 8) & 0xFF);
            commandBlock->CommandBytes[8] = (NumSectors & 0xFF);
        } break;

        // Read - 12 (IN)
        case SCSI_READ_12: {
            uint32_t NumSectors = (uint32_t)(dataLen / sectorSize);
            if (dataLen % sectorSize) {
                NumSectors++;
            }
            commandBlock->Flags  = MSD_CBW_IN;
            commandBlock->Length = 12;

            // LBA
            commandBlock->CommandBytes[2] = ((sectorLba >> 24) & 0xFF);
            commandBlock->CommandBytes[3] = ((sectorLba >> 16) & 0xFF);
            commandBlock->CommandBytes[4] = ((sectorLba >> 8) & 0xFF);
            commandBlock->CommandBytes[5] = (sectorLba & 0xFF);

            // Sector Count
            commandBlock->CommandBytes[6] = ((NumSectors >> 24) & 0xFF);
            commandBlock->CommandBytes[7] = ((NumSectors >> 16) & 0xFF);
            commandBlock->CommandBytes[8] = ((NumSectors >> 8) & 0xFF);
            commandBlock->CommandBytes[9] = (NumSectors & 0xFF);
        } break;

        // Read - 16 (IN)
        case SCSI_READ_16: {
            uint32_t NumSectors = (uint32_t)(dataLen / sectorSize);
            if (dataLen % sectorSize) {
                NumSectors++;
            }
            commandBlock->Flags  = MSD_CBW_IN;
            commandBlock->Length = 16;

            // LBA
            commandBlock->CommandBytes[2] = ((sectorLba >> 56) & 0xFF);
            commandBlock->CommandBytes[3] = ((sectorLba >> 48) & 0xFF);
            commandBlock->CommandBytes[4] = ((sectorLba >> 40) & 0xFF);
            commandBlock->CommandBytes[5] = ((sectorLba >> 32) & 0xFF);
            commandBlock->CommandBytes[6] = ((sectorLba >> 24) & 0xFF);
            commandBlock->CommandBytes[7] = ((sectorLba >> 16) & 0xFF);
            commandBlock->CommandBytes[8] = ((sectorLba >> 8) & 0xFF);
            commandBlock->CommandBytes[9] = (sectorLba & 0xFF);

            // Sector Count
            commandBlock->CommandBytes[10] = ((NumSectors >> 24) & 0xFF);
            commandBlock->CommandBytes[11] = ((NumSectors >> 16) & 0xFF);
            commandBlock->CommandBytes[12] = ((NumSectors >> 8) & 0xFF);
            commandBlock->CommandBytes[13] = (NumSectors & 0xFF);
        } break;

        // Write - 6 (OUT)
        case SCSI_WRITE_6: {
            uint8_t NumSectors = (uint8_t)(dataLen / sectorSize);
            if (dataLen % sectorSize) {
                NumSectors++;
            }
            commandBlock->Flags  = MSD_CBW_OUT;
            commandBlock->Length = 6;

            // LBA
            commandBlock->CommandBytes[1] = ((sectorLba >> 16) & 0x1F);
            commandBlock->CommandBytes[2] = ((sectorLba >> 8) & 0xFF);
            commandBlock->CommandBytes[3] = (sectorLba & 0xFF);

            // Sector Count
            commandBlock->CommandBytes[4] = NumSectors;
        } break;

        // Write - 10 (OUT)
        case SCSI_WRITE: {
            uint16_t NumSectors = (uint16_t)(dataLen / sectorSize);
            if (dataLen % sectorSize) {
                NumSectors++;
            }
            commandBlock->Flags  = MSD_CBW_OUT;
            commandBlock->Length = 10;

            // LBA
            commandBlock->CommandBytes[2] = ((sectorLba >> 24) & 0xFF);
            commandBlock->CommandBytes[3] = ((sectorLba >> 16) & 0xFF);
            commandBlock->CommandBytes[4] = ((sectorLba >> 8) & 0xFF);
            commandBlock->CommandBytes[5] = (sectorLba & 0xFF);

            // Sector Count
            commandBlock->CommandBytes[7] = ((NumSectors >> 8) & 0xFF);
            commandBlock->CommandBytes[8] = (NumSectors & 0xFF);
        } break;

        // Write - 12 (OUT)
        case SCSI_WRITE_12: {
            uint32_t NumSectors = (uint32_t)(dataLen / sectorSize);
            if (dataLen % sectorSize) {
                NumSectors++;
            }
            commandBlock->Flags  = MSD_CBW_OUT;
            commandBlock->Length = 12;

            // LBA
            commandBlock->CommandBytes[2] = ((sectorLba >> 24) & 0xFF);
            commandBlock->CommandBytes[3] = ((sectorLba >> 16) & 0xFF);
            commandBlock->CommandBytes[4] = ((sectorLba >> 8) & 0xFF);
            commandBlock->CommandBytes[5] = (sectorLba & 0xFF);

            // Sector Count
            commandBlock->CommandBytes[6] = ((NumSectors >> 24) & 0xFF);
            commandBlock->CommandBytes[7] = ((NumSectors >> 16) & 0xFF);
            commandBlock->CommandBytes[8] = ((NumSectors >> 8) & 0xFF);
            commandBlock->CommandBytes[9] = (NumSectors & 0xFF);
        } break;

        // Write - 16 (OUT)
        case SCSI_WRITE_16: {
            uint32_t NumSectors = (uint32_t)(dataLen / sectorSize);
            if (dataLen % sectorSize) {
                NumSectors++;
            }
            commandBlock->Flags  = MSD_CBW_OUT;
            commandBlock->Length = 16;

            // LBA
            commandBlock->CommandBytes[2] = ((sectorLba >> 56) & 0xFF);
            commandBlock->CommandBytes[3] = ((sectorLba >> 48) & 0xFF);
            commandBlock->CommandBytes[4] = ((sectorLba >> 40) & 0xFF);
            commandBlock->CommandBytes[5] = ((sectorLba >> 32) & 0xFF);
            commandBlock->CommandBytes[6] = ((sectorLba >> 24) & 0xFF);
            commandBlock->CommandBytes[7] = ((sectorLba >> 16) & 0xFF);
            commandBlock->CommandBytes[8] = ((sectorLba >> 8) & 0xFF);
            commandBlock->CommandBytes[9] = (sectorLba & 0xFF);

            // Sector Count
            commandBlock->CommandBytes[10] = ((NumSectors >> 24) & 0xFF);
            commandBlock->CommandBytes[11] = ((NumSectors >> 16) & 0xFF);
            commandBlock->CommandBytes[12] = ((NumSectors >> 8) & 0xFF);
            commandBlock->CommandBytes[13] = (NumSectors & 0xFF);
        } break;
        default:
            break;
    }
}

static oserr_t
__Initialize(
        _In_ MSDDevice_t* device)
{
    oserr_t oserr;

    // Sanitize found endpoints
    if (device->In == NULL || device->Out == NULL) {
        ERROR("Either in or out endpoint not available on device");
        return OS_ENOTSUPPORTED;
    }

    oserr = __ResetBulk(device);
    if (oserr != OS_EOK) {
        ERROR("__Initialize: failed to reset the bulk interface");
        return oserr;
    }

    // Reset data toggles for bulk-endpoints
    oserr = UsbEndpointReset(
            &device->Device->DeviceContext,
            USB_ENDPOINT_ADDRESS(device->In->Address)
    );
    if (oserr != OS_EOK) {
        ERROR("__Initialize: failed to reset endpoint (in)");
        return oserr;
    }

    oserr = UsbEndpointReset(
            &device->Device->DeviceContext,
            USB_ENDPOINT_ADDRESS(device->Out->Address)
    );
    if (oserr != OS_EOK) {
        ERROR("__Initialize: failed to reset endpoint (out)");
    }
    return oserr;
}

static enum USBTransferCode
__VerifyCSWStatus(
        _In_ MSDDevice_t*        device,
        _In_ MsdCommandStatus_t* csw)
{
    // Check for phase errors
    if (csw->Status == MSD_CSW_PHASE_ERROR) {
        ERROR("Phase error returned in CSW.");
        return USBTRANSFERCODE_INVALID;
    }

    // Sanitize signature/data integrity
    if (csw->Signature != MSD_CSW_OK_SIGNATURE) {
        ERROR("CSW: Signature is invalid: 0x%x", csw->Signature);
        return USBTRANSFERCODE_INVALID;
    }
    
    // Sanitize tag/data integrity
    if ((csw->Tag & 0xFFFFFF00) != MSD_TAG_SIGNATURE) {
        ERROR("CSW: Tag is invalid: 0x%x", csw->Tag);
        return USBTRANSFERCODE_INVALID;
    }

    // Sanitize status
    if (csw->Status != MSD_CSW_OK) {
        ERROR("CSW: Status is invalid: 0x%x", csw->Status);
        return USBTRANSFERCODE_INVALID;
    }

    // Everything should be fine
    return USBTRANSFERCODE_SUCCESS;
}

static oserr_t
__SendCommand(
        _In_  MSDDevice_t*          device,
        _In_  uint8_t               scsiCommand,
        _In_  uint64_t              sectorStart,
        _In_  size_t                dataLength,
        _Out_ enum USBTransferCode* transferCodeOut)
{
    enum USBTransferCode transferCode;
    USBTransfer_t        CommandStage;
    size_t               bytesTransferred;
    oserr_t              oserr;

    TRACE("__SendCommand(Command %u, Start %u, Length %u)",
          scsiCommand, LODWORD(sectorStart), dataLength);

    // Construct our command build the usb transfer
    __ConstructSCSICommand(
            device->CommandBlock,
            scsiCommand,
            sectorStart,
            dataLength,
            (uint16_t)device->Descriptor.SectorSize
    );

    UsbTransferInitialize(
            &CommandStage,
            &device->Device->DeviceContext,
            device->Out,
            USBTRANSFER_TYPE_BULK,
            USBTRANSFER_DIRECTION_OUT,
            0,
            dma_pool_handle(UsbRetrievePool()),
            dma_pool_offset(UsbRetrievePool(), device->CommandBlock),
            sizeof(MsdCommandBlock_t)
    );

    oserr = UsbTransferQueue(
            &device->Device->DeviceContext,
            &CommandStage,
            &transferCode,
            &bytesTransferred
    );
    if (oserr != OS_EOK || transferCode != USBTRANSFERCODE_SUCCESS) {
        ERROR("Failed to send the CBW command, transfer-code %u/%u", oserr, transferCode);
        if (transferCode == USBTRANSFERCODE_STALL) {
            ERROR("Performing a recovery-reset on device.");
            if (__ResetRecovery(device, BULK_RESET_ALL) != OS_EOK) {
                ERROR("Failed to reset device, it is now unusable.");
            }
        }
    }
    *transferCodeOut = transferCode;
    return oserr;
}

static oserr_t
__ReadData(
        _In_  MSDDevice_t*          device,
        _In_  uuid_t                bufferHandle,
        _In_  size_t                bufferOffset,
        _In_  size_t                dataLength,
        _Out_ enum USBTransferCode* transferCodeOut,
        _Out_ size_t*               bytesRead)
{
    enum USBTransferCode transferCode;
    USBTransfer_t        dataStage;
    oserr_t              oserr;
    TRACE("__ReadData(length=%u)", dataLength);

    UsbTransferInitialize(
            &dataStage,
            &device->Device->DeviceContext,
            device->In,
            USBTRANSFER_TYPE_BULK,
            USBTRANSFER_DIRECTION_IN,
            0,
            bufferHandle,
            bufferOffset,
            dataLength
    );

    oserr = UsbTransferQueue(
            &device->Device->DeviceContext,
            &dataStage,
            &transferCode,
            bytesRead
    );
    // Sanitize for any transport errors
    // The host shall accept the data received.
    // The host shall clear the Bulk-In pipe.
    if (oserr != OS_EOK || transferCode != USBTRANSFERCODE_SUCCESS) {
        ERROR("Data-stage failed with status %u/%u, cleaning up bulk-in", oserr, transferCode);
        if (transferCode == USBTRANSFERCODE_STALL) {
            __ResetRecovery(device, BULK_RESET_IN);
        } else {
            // Fatal error
            __ResetRecovery(device, BULK_RESET_ALL);
        }
    }
    *transferCodeOut = transferCode;
    return oserr;
}

static oserr_t
__WriteData(
        _In_  MSDDevice_t*          device,
        _In_  uuid_t                bufferHandle,
        _In_  size_t                bufferOffset,
        _In_  size_t                dataLength,
        _Out_ enum USBTransferCode* transferCodeOut,
        _Out_ size_t*               bytesWrittenOut)
{
    enum USBTransferCode transferCode;
    USBTransfer_t        DataStage;
    oserr_t              oserr;
    TRACE("__WriteData(length=%u)", dataLength);

    // Perform the data-stage
    UsbTransferInitialize(
            &DataStage,
            &device->Device->DeviceContext,
            device->Out,
            USBTRANSFER_TYPE_BULK,
            USBTRANSFER_DIRECTION_OUT,
            0,
            bufferHandle,
            bufferOffset,
            dataLength
    );

    oserr = UsbTransferQueue(
            &device->Device->DeviceContext,
            &DataStage,
            &transferCode,
            bytesWrittenOut
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Sanitize for any transport errors
    // The host shall accept the data received.
    // The host shall clear the Bulk-In pipe.
    if (transferCode != USBTRANSFERCODE_SUCCESS) {
        ERROR("Data-stage failed with status %u/%u, cleaning up bulk-out", oserr, transferCode);
        if (transferCode == USBTRANSFERCODE_STALL) {
            __ResetRecovery(device, BULK_RESET_OUT);
        } else {
            __ResetRecovery(device, BULK_RESET_ALL);
        }
    }
    *transferCodeOut = transferCode;
    return oserr;
}

static oserr_t
__GetStatus(
        _In_  MSDDevice_t*          device,
        _Out_ enum USBTransferCode* transferCodeOut)
{
    enum USBTransferCode transferCode;
    USBTransfer_t        statusStage;
    size_t               bytesTransferred;
    oserr_t              oserr;
    TRACE("__GetStatus()");

    UsbTransferInitialize(
            &statusStage,
            &device->Device->DeviceContext,
            device->In,
            USBTRANSFER_TYPE_BULK,
            USBTRANSFER_DIRECTION_IN,
            0,
            dma_pool_handle(UsbRetrievePool()),
            dma_pool_offset(UsbRetrievePool(), device->StatusBlock),
            sizeof(MsdCommandStatus_t)
    );

    oserr = UsbTransferQueue(
            &device->Device->DeviceContext,
            &statusStage,
            &transferCode,
            &bytesTransferred
    );
    // Sanitize for any transport errors
    // On a STALL condition receiving the CSW, then:
    // The host shall clear the Bulk-In pipe.
    // The host shall again attempt to receive the CSW.
    if (oserr != OS_EOK || transferCode != USBTRANSFERCODE_SUCCESS) {
        if (oserr == OS_EOK && transferCode == USBTRANSFERCODE_STALL) {
            oserr = __ResetRecovery(device, BULK_RESET_IN);
            if (oserr != OS_EOK) {
                return oserr;
            }
            return __GetStatus(device, transferCodeOut);
        } else {
            ERROR("Failed to retrieve the CSW block, transfer-code %u", transferCode);
        }

        *transferCodeOut = transferCode;
        return oserr;
    }

    // If the host receives a CSW which is not valid,
    // then the host shall perform a Reset Recovery. If the host receives
    // a CSW which is not meaningful, then the host may perform a Reset Recovery.
    *transferCodeOut = __VerifyCSWStatus(device, device->StatusBlock);
    return oserr;
}

MSDOperations_t BulkOperations = {
        __Initialize,
        __SendCommand,
        __ReadData,
        __WriteData,
        __GetStatus
};
