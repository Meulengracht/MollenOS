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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Mass Storage Device Driver (Generic)
 */

//#define __TRACE

#include "msd.h"
#include <ddk/utils.h>
#include <internal/_ipc.h>
#include <threads.h>

#include "ctt_driver_protocol_server.h"
#include "ctt_storage_protocol_server.h"

extern MsdOperations_t BulkOperations;
extern MsdOperations_t UfiOperations;
static MsdOperations_t *ProtocolOperations[ProtocolCount] = {
    NULL,
    &UfiOperations,
    &UfiOperations,
    &BulkOperations
};

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

OsStatus_t
MsdDeviceInitialize(
    _In_ MsdDevice_t *Device)
{
    // Install operations
    if (ProtocolOperations[Device->Protocol] == NULL) {
        ERROR("Support is not implemented for the protocol.");
        return OsNotSupported;
    }
    Device->Operations = ProtocolOperations[Device->Protocol];
    return Device->Operations->Initialize(Device);
}

OsStatus_t
MsdGetMaximumLunCount(
    _In_ MsdDevice_t *Device)
{
    UsbTransferStatus_t Status;
    uint8_t             MaxLuns;

    // Get Max LUNS is
    // 0xA1 | 0xFE | wIndex - Interface 
    // 1 Byte is expected back, values range between 0 - 15, 0-indexed
    Status = UsbExecutePacket(&Device->Base.DeviceContext,
        USBPACKET_DIRECTION_IN | USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_INTERFACE,
        MSD_REQUEST_GET_MAX_LUN, 0, 0, (uint16_t)Device->InterfaceId, 1, &MaxLuns);

    // If no multiple LUNS are supported, device may STALL it says in the usbmassbulk spec 
    // but thats ok, it's not a functional stall, only command stall
    if (Status == TransferFinished) {
        Device->Descriptor.LUNCount = (size_t)(MaxLuns & 0xF);
    }
    else {
        Device->Descriptor.LUNCount = 0;
    }
    return OsSuccess;
}

UsbTransferStatus_t 
MsdScsiCommand(
    _In_ MsdDevice_t* Device,
    _In_ int          Direction,
    _In_ uint8_t      ScsiCommand,
    _In_ uint64_t     SectorStart,
    _In_ UUId_t       BufferHandle,
    _In_ size_t       BufferOffset,
    _In_ size_t       DataLength)
{
    UsbTransferStatus_t Status         = { 0 };
    size_t              DataToTransfer = DataLength;
    int                 RetryCount     = 3;

    TRACE("MsdScsiCommand(Direction %i, Command 0x%x, Start %u, Length %u)",
        Direction, ScsiCommand, LODWORD(SectorStart), DataLength);

    // It is invalid to send zero length packets for bulk
    if (Direction == 1 && DataLength == 0) {
        ERROR("Cannot write data of length 0 to MSD devices.");
        return TransferInvalid;
    }

    // Send the command
    Status = Device->Operations->SendCommand(Device, ScsiCommand, 
        SectorStart, BufferHandle, BufferOffset, DataLength);
    if (Status != TransferFinished) {
        ERROR("Failed to send the CBW command, transfer-code %u", Status);
        return Status;
    }

    // Do the data stage (shared for all protocol)
    while (DataToTransfer != 0) {
        size_t BytesTransferred = 0;
        if (Direction == 0) Status = Device->Operations->ReadData(Device, BufferHandle, BufferOffset, DataToTransfer, &BytesTransferred);
        else                Status = Device->Operations->WriteData(Device, BufferHandle, BufferOffset, DataToTransfer, &BytesTransferred);
        if (Status != TransferFinished && Status != TransferStalled) {
            ERROR("Fatal error transfering data, skipping status stage");
            return Status;
        }
        if (Status == TransferStalled) {
            RetryCount--;
            if (RetryCount == 0) {
                ERROR("Fatal error transfering data, skipping status stage");
                return Status;
            }
        }
        DataToTransfer -= BytesTransferred;
        BufferOffset   += BytesTransferred;
    }
    return Device->Operations->GetStatus(Device);
}

OsStatus_t
MsdDevicePrepare(
    MsdDevice_t *Device)
{
    ScsiSense_t *SenseBlock = NULL;
    int ResponseCode, SenseKey;

    // Debug
    TRACE("MsdPrepareDevice()");

    // Allocate memory buffer
    if (dma_pool_allocate(UsbRetrievePool(), sizeof(ScsiSense_t), 
        (void**)&SenseBlock) != OsSuccess) {
        ERROR("Failed to allocate buffer (sense)");
        return OsError;
    }

    // Don't use test-unit-ready for UFI
    if (Device->Protocol != ProtocolCB && Device->Protocol != ProtocolCBI) {
        if (MsdScsiCommand(Device, 0, SCSI_TEST_UNIT_READY, 0, 0, 0, 0)
                != TransferFinished) {
            ERROR("Failed to perform test-unit-ready command");
            dma_pool_free(UsbRetrievePool(), (void*)SenseBlock);
            Device->IsReady = 0;
            return OsError;
        }
    }

    // Now request the sense-status
    if (MsdScsiCommand(Device, 0, SCSI_REQUEST_SENSE, 0, 
        dma_pool_handle(UsbRetrievePool()), dma_pool_offset(UsbRetrievePool(), SenseBlock), 
        sizeof(ScsiSense_t)) != TransferFinished) {
        ERROR("Failed to perform sense command");
        dma_pool_free(UsbRetrievePool(), (void*)SenseBlock);
        Device->IsReady = 0;
        return OsError;
    }

    // Extract sense-codes and key
    ResponseCode = SCSI_SENSE_RESPONSECODE(SenseBlock->ResponseStatus);
    SenseKey = SCSI_SENSE_KEY(SenseBlock->Flags);
    dma_pool_free(UsbRetrievePool(), (void*)SenseBlock);

    // Must be either 0x70, 0x71, 0x72, 0x73
    if (ResponseCode >= 0x70 && ResponseCode <= 0x73) {
        TRACE("Sense Status: %s", SenseKeys[SenseKey]);
    }
    else {
        ERROR("Invalid Response Code: 0x%x", ResponseCode);
        Device->IsReady = 0;
        return OsError;
    }

    // Mark ready and return
    Device->IsReady = 1;
    return OsSuccess;
}

OsStatus_t 
MsdReadCapabilities(
    _In_ MsdDevice_t *Device)
{
    StorageDescriptor_t *Descriptor = &Device->Descriptor;
    uint32_t *CapabilitesPointer = NULL;

    // Allocate buffer
    if (dma_pool_allocate(UsbRetrievePool(), sizeof(ScsiExtendedCaps_t), 
        (void**)&CapabilitesPointer) != OsSuccess) {
        ERROR("Failed to allocate buffer (caps)");
        return OsError;
    }

    // Perform caps-command
    if (MsdScsiCommand(Device, 0, SCSI_READ_CAPACITY, 0, 
            dma_pool_handle(UsbRetrievePool()), dma_pool_offset(UsbRetrievePool(), CapabilitesPointer), 
            8) != TransferFinished) {
        dma_pool_free(UsbRetrievePool(), (void*)CapabilitesPointer);
        return OsError;
    }

    // If the size equals max, then we need to use extended
    // capabilities
    if (CapabilitesPointer[0] == 0xFFFFFFFF) {
        // Variables
        ScsiExtendedCaps_t *ExtendedCaps = (ScsiExtendedCaps_t*)CapabilitesPointer;

        // Perform extended-caps read command
        if (MsdScsiCommand(Device, 0, SCSI_READ_CAPACITY_16, 0, 
                dma_pool_handle(UsbRetrievePool()), dma_pool_offset(UsbRetrievePool(), CapabilitesPointer),
                sizeof(ScsiExtendedCaps_t)) != TransferFinished) {
            dma_pool_free(UsbRetrievePool(), (void*)CapabilitesPointer);
            return OsError;
        }

        // Capabilities are returned in reverse byte-order
        Descriptor->SectorCount = rev64(ExtendedCaps->SectorCount) + 1;
        Descriptor->SectorSize = rev32(ExtendedCaps->SectorSize);
        TRACE("[msd] [read_capabilities] sectorCount %llu, sectorSize %u", Descriptor->SectorCount,
            Descriptor->SectorSize);
        Device->IsExtended = 1;
        dma_pool_free(UsbRetrievePool(), (void*)CapabilitesPointer);
        return OsSuccess;
    }

    // Capabilities are returned in reverse byte-order
    Descriptor->SectorCount = (uint64_t)rev32(CapabilitesPointer[0]) + 1;
    Descriptor->SectorSize = rev32(CapabilitesPointer[1]);
    TRACE("[msd] [read_capabilities] 0x%llx sectorCount %llu, sectorSize %u",
        &Descriptor->SectorCount, Descriptor->SectorCount, Descriptor->SectorSize);
    dma_pool_free(UsbRetrievePool(), (void*)CapabilitesPointer);
    return OsSuccess;
}

OsStatus_t
MsdDeviceStart(
    _In_ MsdDevice_t* msdDevice)
{
    UsbTransferStatus_t transferStatus;
    ScsiInquiry_t*      inquiryData = NULL;
    int i;

    // How many iterations of device-ready?
    // Floppys need a lot longer to spin up
    i = (msdDevice->Protocol != ProtocolCB && msdDevice->Protocol != ProtocolCBI) ? 30 : 3;

    // Allocate space for inquiry
    if (dma_pool_allocate(UsbRetrievePool(), sizeof(ScsiInquiry_t), (void**)&inquiryData) != OsSuccess) {
        ERROR("Failed to allocate buffer (inquiry)");
        return OsError;
    }

    // Perform inquiry
    transferStatus = MsdScsiCommand(msdDevice, 0, SCSI_INQUIRY, 0,
                                    dma_pool_handle(UsbRetrievePool()), dma_pool_offset(UsbRetrievePool(), inquiryData), sizeof(ScsiInquiry_t));
    if (transferStatus != TransferFinished) {
        ERROR("Failed to perform the inquiry command on device: %u", transferStatus);
        dma_pool_free(UsbRetrievePool(), (void*)inquiryData);
        return OsError;
    }

    // Perform the Test-Unit Ready command
    while (msdDevice->IsReady == 0 && i != 0) {
        MsdDevicePrepare(msdDevice);
        if (msdDevice->IsReady == 1) {
            break; 
        }
        thrd_sleepex(100);
        i--;
    }

    // Sanitize the resulting ready state, we need it 
    // ready otherwise we can't use it
    if (!msdDevice->IsReady) {
        ERROR("Failed to ready device");
        dma_pool_free(UsbRetrievePool(), (void*)inquiryData);
        return OsError;
    }
    dma_pool_free(UsbRetrievePool(), (void*)inquiryData);
    return MsdReadCapabilities(msdDevice);
}

static void
SelectScsiTransferCommand(
    _In_  MsdDevice_t* device,
    _In_  int          direction,
    _Out_ uint8_t*     commandOut,
    _Out_ size_t*      maxSectorsCountOut)
{
    // Detect limits based on type of device and protocol
    if (device->Protocol == ProtocolCB || device->Protocol == ProtocolCBI) {
        *commandOut         = (direction == __STORAGE_OPERATION_READ) ? SCSI_READ_6 : SCSI_WRITE_6;
        *maxSectorsCountOut = UINT8_MAX;
    }
    else if (!device->IsExtended) {
        *commandOut         = (direction == __STORAGE_OPERATION_READ) ? SCSI_READ : SCSI_WRITE;
        *maxSectorsCountOut = UINT16_MAX;
    }
    else {
        *commandOut         = (direction == __STORAGE_OPERATION_READ) ? SCSI_READ_16 : SCSI_WRITE_16;
        *maxSectorsCountOut = UINT32_MAX;
    }
}

OsStatus_t
MsdTransferSectors(
    _In_  MsdDevice_t* device,
    _In_  int          direction,
    _In_  uint64_t     sector,
    _In_  UUId_t       bufferHandle,
    _In_  unsigned int bufferOffset,
    _In_  size_t       sectorCount,
    _Out_ size_t*      sectorsTransferred)
{
    UsbTransferStatus_t transferStatus;
    size_t              sectorsToBeTransferred;
    uint8_t             command;
    size_t              maxSectorsPerCommand;

    TRACE("[msd_transfer] direction %u, sector %llu, count %" PRIuIN, 
        direction, sector, sectorCount);

    // Protect against bad start sector
    if (sector >= device->Descriptor.SectorCount) {
        return OsInvalidParameters;
    }

    // Of course it's possible that the requester is requesting too much data in one
    // go, so we will have to clamp some of the values. Is the sector valid first of all?
    sectorsToBeTransferred = sectorCount;
    if ((sector + sectorsToBeTransferred) >= device->Descriptor.SectorCount) {
        sectorsToBeTransferred = device->Descriptor.SectorCount - sector;
    }
    
    // Detect limits based on type of device and protocol
    SelectScsiTransferCommand(device, direction, &command, &maxSectorsPerCommand);
    sectorsToBeTransferred = MIN(sectorsToBeTransferred, maxSectorsPerCommand);

    // protect against zero reads
    if (sectorsToBeTransferred == 0) {
        return OsInvalidParameters;
    }

    TRACE("[msd_transfer] command %u, max sectors for command %u", command, maxSectorsPerCommand);
    transferStatus = MsdScsiCommand(
            device,
            direction == __STORAGE_OPERATION_WRITE,
            command,
            sector,
            bufferHandle,
            bufferOffset,
            sectorsToBeTransferred * device->Descriptor.SectorSize);
    if (transferStatus != TransferFinished) {
        if (sectorsTransferred) {
            *sectorsTransferred = 0;
        }
        return OsError;
    }
    else if (sectorsTransferred) {
        *sectorsTransferred = sectorsToBeTransferred;
        if (device->StatusBlock->DataResidue) {
            // Data residue is in bytes not transferred as it does not seem
            // required that we transfer in sectors
            size_t SectorsLeft = DIVUP(device->StatusBlock->DataResidue, 
                device->Descriptor.SectorSize);
            *sectorsTransferred -= SectorsLeft;
        }
    }
    return OsSuccess;
}

void ctt_storage_transfer_async_callback(
        _In_ struct gracht_recv_message*             message,
        _In_ struct ctt_storage_transfer_async_args* args)
{
    MsdDevice_t*    device = MsdDeviceGet(args->device_id);
    OsStatus_t      status;
    LargeUInteger_t sector;
    size_t          sectorsTransferred;
    
    sector.u.LowPart = args->sector_lo;
    sector.u.HighPart = args->sector_hi;
    
    status = MsdTransferSectors(device, args->direction, sector.QuadPart,
        args->buffer_id, args->buffer_offset, args->sector_count,
        &sectorsTransferred);
    if (status != OsSuccess) {
        // @todo
    }
}

void ctt_storage_transfer_callback(struct gracht_recv_message* message, struct ctt_storage_transfer_args* args)
{
    MsdDevice_t*    device = MsdDeviceGet(args->device_id);
    OsStatus_t      status;
    LargeUInteger_t sector;
    size_t          sectorsTransferred;
    
    sector.u.LowPart = args->sector_lo;
    sector.u.HighPart = args->sector_hi;
    
    status = MsdTransferSectors(device, args->direction, sector.QuadPart,
        args->buffer_id, args->buffer_offset, args->sector_count,
        &sectorsTransferred);
    ctt_storage_transfer_response(message, status, sectorsTransferred);
}

void ctt_usbhost_event_queue_status_callback(
    struct ctt_usbhost_queue_status_event* args)
{
    
}
