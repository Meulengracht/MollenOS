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
 */
#define __TRACE

#include <usb/usb.h>
#include <ddk/utils.h>
#include "../msd.h"

static void
__ConstructUFICommand(
    _In_ MsdCommandBlockUFI_t* cmdBlock,
    _In_ uint8_t               scsiCommand,
    _In_ uint64_t              sectorLba,
    _In_ uint32_t              dataLen,
    _In_ uint16_t              sectorSize)
{
    memset((void*)cmdBlock, 0, sizeof(MsdCommandBlockUFI_t));
    
    // Set initial members
    cmdBlock->CommandBytes[0] = scsiCommand;

    // Switch between supported/implemented commands
    switch (scsiCommand) {
        // Request Sense - 6 (IN)
        case SCSI_REQUEST_SENSE: {
            cmdBlock->CommandBytes[4] = 18; // Response Length
        } break;

        // Inquiry - 6 (IN)
        case SCSI_INQUIRY: {
            cmdBlock->CommandBytes[4] = 36; // Response Length
        } break;

        // Read Capacities - 10 (IN)
        case SCSI_READ_CAPACITY: {
            // LBA
            cmdBlock->CommandBytes[2] = ((sectorLba >> 24) & 0xFF);
            cmdBlock->CommandBytes[3] = ((sectorLba >> 16) & 0xFF);
            cmdBlock->CommandBytes[4] = ((sectorLba >> 8) & 0xFF);
            cmdBlock->CommandBytes[5] = (sectorLba & 0xFF);
        } break;

        // Read - 10 (IN)
        case SCSI_READ: {
            uint16_t NumSectors = (uint16_t)(dataLen / sectorSize);
            if (dataLen % sectorSize) {
                NumSectors++;
            }

            // LBA
            cmdBlock->CommandBytes[2] = ((sectorLba >> 24) & 0xFF);
            cmdBlock->CommandBytes[3] = ((sectorLba >> 16) & 0xFF);
            cmdBlock->CommandBytes[4] = ((sectorLba >> 8) & 0xFF);
            cmdBlock->CommandBytes[5] = (sectorLba & 0xFF);

            // Sector Count
            cmdBlock->CommandBytes[7] = ((NumSectors >> 8) & 0xFF);
            cmdBlock->CommandBytes[8] = (NumSectors & 0xFF);
        } break;

        // Write - 10 (OUT)
        case SCSI_WRITE: {
            uint16_t NumSectors = (uint16_t)(dataLen / sectorSize);
            if (dataLen % sectorSize) {
                NumSectors++;
            }

            // LBA
            cmdBlock->CommandBytes[2] = ((sectorLba >> 24) & 0xFF);
            cmdBlock->CommandBytes[3] = ((sectorLba >> 16) & 0xFF);
            cmdBlock->CommandBytes[4] = ((sectorLba >> 8) & 0xFF);
            cmdBlock->CommandBytes[5] = (sectorLba & 0xFF);

            // Sector Count
            cmdBlock->CommandBytes[7] = ((NumSectors >> 8) & 0xFF);
            cmdBlock->CommandBytes[8] = (NumSectors & 0xFF);
        } break;
    }
}

static oserr_t
__Initialize(
        _In_ MSDDevice_t* device)
{
    // Sanitize found endpoints
    if (device->In == NULL || device->Out == NULL) {
        ERROR("Either in or out endpoint not available on device");
        return OS_EUNKNOWN;
    }

    // If we are CBI and not CB, there must be interrupt
    if (device->Protocol == ProtocolCBI && device->Interrupt == NULL) {
        ERROR("Protocol is CBI, but interrupt endpoint does not exist");
        return OS_EUNKNOWN;
    }

    // Reset data toggles for bulk-endpoints
    if (UsbEndpointReset(&device->Device->DeviceContext,
        USB_ENDPOINT_ADDRESS(device->In->Address)) != OS_EOK) {
        ERROR("Failed to reset endpoint (in)");
        return OS_EUNKNOWN;
    }
    if (UsbEndpointReset(&device->Device->DeviceContext,
        USB_ENDPOINT_ADDRESS(device->Out->Address)) != OS_EOK) {
        ERROR("Failed to reset endpoint (out)");
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

static oserr_t
__SendCommand(
        _In_  MSDDevice_t*          device,
        _In_  uint8_t               scsiCommand,
        _In_  uint64_t              sectorStart,
        _In_  size_t                dataLength,
        _Out_ enum USBTransferCode* transferCodeOut)
{
    MsdCommandBlockUFI_t ufiCommandBlock;
    enum USBTransferCode transferCode;

    TRACE("__SendCommand(Command %u, Start %u, Length %u)",
          scsiCommand, LODWORD(sectorStart), dataLength);

    // Construct our command build the usb transfer
    __ConstructUFICommand(
            &ufiCommandBlock,
            scsiCommand,
            sectorStart,
            dataLength,
            (uint16_t)device->Descriptor.SectorSize
    );
    transferCode = UsbExecutePacket(
            &device->Device->DeviceContext,
            USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_INTERFACE,
            0,
            0,
            0,
            (uint16_t)device->InterfaceId,
            sizeof(MsdCommandBlockUFI_t),
            &ufiCommandBlock
    );

    // Sanitize for any transport errors
    if (transferCode != USBTRANSFERCODE_SUCCESS) {
        ERROR("Failed to send the CBW command, transfer-code %u", transferCode);
    }
    *transferCodeOut = transferCode;
    return OS_EOK;
}

static oserr_t
__ReadData(
        _In_  MSDDevice_t*          device,
        _In_  uuid_t                bufferHandle,
        _In_  size_t                bufferOffset,
        _In_  size_t                dataLength,
        _Out_ enum USBTransferCode* transferCodeOut,
        _Out_ size_t*               bytesReadOut)
{
    enum USBTransferCode transferCode;
    USBTransfer_t        DataStage;
    oserr_t              oserr;

    UsbTransferInitialize(
            &DataStage,
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
            &DataStage,
            &transferCode,
            bytesReadOut
    );
    if (oserr != OS_EOK || transferCode != USBTRANSFERCODE_SUCCESS) {
        ERROR("Data-stage failed with status %u/%u, cleaning up bulk-in", oserr, transferCode);
        // @todo handle
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
    USBTransfer_t        dataStage;
    oserr_t              oserr;

    // Perform the data-stage
    UsbTransferInitialize(
            &dataStage,
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
            &dataStage,
            &transferCode,
            bytesWrittenOut
    );
    if (oserr != OS_EOK || transferCode != USBTRANSFERCODE_SUCCESS) {
        ERROR("Data-stage failed with status %u/%u, cleaning up bulk-out", oserr, transferCode);
        // @todo handle
    }

    *transferCodeOut = transferCode;
    return oserr;
}

static oserr_t
__GetStatus(
        _In_  MSDDevice_t*          device,
        _Out_ enum USBTransferCode* transferCodeOut)
{
    _CRT_UNUSED(device);
    *transferCodeOut = USBTRANSFERCODE_SUCCESS;
    return OS_EOK;
}

MSDOperations_t UfiOperations = {
        __Initialize,
        __SendCommand,
        __ReadData,
        __WriteData,
        __GetStatus
};
