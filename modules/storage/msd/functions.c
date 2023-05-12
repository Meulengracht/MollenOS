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
 */

//#define __TRACE
#define __need_minmax
#include "msd.h"
#include <ddk/utils.h>
#include <os/handle.h>
#include <os/shm.h>
#include <threads.h>
#include <stdio.h>

#include <ctt_driver_service_server.h>
#include <ctt_storage_service_server.h>

extern MSDOperations_t BulkOperations;
extern MSDOperations_t UfiOperations;
static const MSDOperations_t* g_protocolOperations[ProtocolCount] = {
    NULL,
    &UfiOperations,
    &UfiOperations,
    &BulkOperations
};

const char* g_senseKeys[] = {
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

static void
__flipbuffer(
        _In_ uint8_t* buffer,
        _In_ size_t   length)
{
    size_t pairs = length / 2;
    size_t i;

    // Iterate pairs in string, and swap
    for (i = 0; i < pairs; i++) {
        uint8_t temp      = buffer[i * 2];
        buffer[i * 2]     = buffer[i * 2 + 1];
        buffer[i * 2 + 1] = temp;
    }

    // Zero terminate by trimming trailing spaces
    for (i = (length - 1); i > 0; i--) {
        if (buffer[i] != ' ' && buffer[i] != '\0') {
            i += 1;
            if (i < length) {
                buffer[i] = '\0';
            }
            break;
        }
    }
}

oserr_t
MSDDeviceInitialize(
        _In_ MSDDevice_t* device)
{
    if (!g_protocolOperations[device->Protocol]) {
        ERROR("Support is not implemented for the protocol.");
        return OS_ENOTSUPPORTED;
    }

    device->Operations = g_protocolOperations[device->Protocol];
    return device->Operations->Initialize(device);
}

oserr_t
MSDQueryMaximumLUNCount(
        _In_ MSDDevice_t* device)
{
    enum USBTransferCode transferCode;
    uint8_t              maxLuns;

    // Get Max LUNS is
    // 0xA1 | 0xFE | wIndex - Interface 
    // 1 Byte is expected back, values range between 0 - 15, 0-indexed
    transferCode = UsbExecutePacket(
            &device->Device->DeviceContext,
            USBPACKET_DIRECTION_IN | USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_INTERFACE,
            MSD_REQUEST_GET_MAX_LUN,
            0,
            0,
            (uint16_t)device->InterfaceId,
            1,
            &maxLuns
    );

    // If no multiple LUNS are supported, device may STALL it says in the usbmassbulk spec 
    // but that's ok, it's not a functional stall, only command stall
    if (transferCode == USBTRANSFERCODE_SUCCESS) {
        device->Descriptor.LUNCount = (size_t)(maxLuns & 0xF);
    } else {
        device->Descriptor.LUNCount = 0;
    }
    return OS_EOK;
}

static oserr_t
__ExecuteCommand(
        _In_ MSDDevice_t*           device,
        _In_ int                    direction,
        _In_ uint8_t                scsiCommand,
        _In_ uint64_t               sectorStart,
        _In_ uuid_t                 bufferHandle,
        _In_ size_t                 bufferOffset,
        _In_ size_t                 dataLength,
        _Out_ enum USBTransferCode* transferCodeOut)
{
    enum USBTransferCode transferCode;
    oserr_t              oserr;
    size_t               bytesLeft  = dataLength;
    int                  RetryCount = 3;

    TRACE("__ExecuteCommand(Direction %i, Command 0x%x, Start %u, Length %u)",
          direction, scsiCommand, LODWORD(sectorStart), dataLength);
    if (direction != __STORAGE_OPERATION_READ && direction != __STORAGE_OPERATION_WRITE) {
        ERROR("__ExecuteCommand: unsupported direction");
        return OS_EINVALPARAMS;
    }

    // It is invalid to send zero length packets for bulk
    if (direction == __STORAGE_OPERATION_WRITE && dataLength == 0) {
        ERROR("__ExecuteCommand: cannot write data of length 0 to MSD devices.");
        return OS_EINVALPARAMS;
    }

    // Send the command, as there is no buffer provided here, we don't have to
    // worry about incompatible buffers.
    oserr = device->Operations->SendCommand(
            device,
            scsiCommand,
            sectorStart,
            dataLength,
            &transferCode
    );
    if (oserr != OS_EOK || transferCode != USBTRANSFERCODE_SUCCESS) {
        ERROR("__ExecuteCommand: failed to send the CBW command, transfer-code %u/%u", oserr, transferCode);
        *transferCodeOut = transferCode;
        return oserr;
    }

    // Do the data stage (shared for all protocol)
    while (bytesLeft != 0) {
        size_t bytesTransferred = 0;
        if (direction == __STORAGE_OPERATION_READ) {
            oserr = device->Operations->ReadData(
                    device,
                    bufferHandle,
                    bufferOffset,
                    bytesLeft,
                    &transferCode,
                    &bytesTransferred
            );
        } else if (direction == __STORAGE_OPERATION_WRITE) {
            oserr = device->Operations->WriteData(
                    device,
                    bufferHandle,
                    bufferOffset,
                    bytesLeft,
                    &transferCode,
                    &bytesTransferred
            );
        }
        if (oserr != OS_EOK) {
            return oserr;
        }
        TRACE("__ExecuteCommand: bytes-transferred: 0x%llx", bytesTransferred);

        if (transferCode != USBTRANSFERCODE_SUCCESS && transferCode != USBTRANSFERCODE_STALL) {
            ERROR("__ExecuteCommand: failed to transfer data, skipping status stage");
            *transferCodeOut = transferCode;
            return OS_EOK;
        }

        if (transferCode == USBTRANSFERCODE_STALL) {
            RetryCount--;
            if (RetryCount == 0) {
                ERROR("__ExecuteCommand: failed to transfer data, skipping status stage");
                *transferCodeOut = transferCode;
                return OS_EOK;
            }
            continue;
        }

        bytesLeft -= bytesTransferred;
        bufferOffset += bytesTransferred;
    }
    return device->Operations->GetStatus(device, transferCodeOut);
}

static oserr_t
__RequestSenseReady(
        MSDDevice_t* device)
{
    ScsiSense_t*         senseBlock = NULL;
    int                  responseCode;
    enum USBTransferCode transferCode;
    oserr_t              oserr;

    TRACE("MsdPrepareDevice()");

    oserr = dma_pool_allocate(
            UsbRetrievePool(),
            sizeof(ScsiSense_t),
            (void**)&senseBlock
    );
    if (oserr != OS_EOK) {
        ERROR("MsdPrepareDevice: failed to allocate buffer (sense)");
        return oserr;
    }

    // Don't use test-unit-ready for UFI
    if (device->Protocol != ProtocolCB && device->Protocol != ProtocolCBI) {
        oserr = __ExecuteCommand(
                device,
                __STORAGE_OPERATION_READ,
                SCSI_TEST_UNIT_READY,
                0,
                0,
                0,
                0,
                &transferCode
        );
        if (oserr != OS_EOK || transferCode != USBTRANSFERCODE_SUCCESS) {
            ERROR("MsdPrepareDevice: Failed to perform test-unit-ready command");
            dma_pool_free(UsbRetrievePool(), (void*)senseBlock);
            device->IsReady = 0;
            return oserr;
        }
    }

    // Now request the sense-status
    oserr = __ExecuteCommand(
            device,
            __STORAGE_OPERATION_READ,
            SCSI_REQUEST_SENSE,
            0,
            dma_pool_handle(UsbRetrievePool()),
            dma_pool_offset(UsbRetrievePool(), senseBlock),
            sizeof(ScsiSense_t),
            &transferCode
    );
    if (oserr != OS_EOK || transferCode != USBTRANSFERCODE_SUCCESS) {
        ERROR("MsdPrepareDevice: failed to perform sense command");
        dma_pool_free(UsbRetrievePool(), (void*)senseBlock);
        device->IsReady = 0;
        return oserr;
    }

    // Extract sense-codes and key
    responseCode = SCSI_SENSE_RESPONSECODE(senseBlock->ResponseStatus);
    dma_pool_free(UsbRetrievePool(), (void*)senseBlock);

    // Must be either 0x70, 0x71, 0x72, 0x73
    if (responseCode >= 0x70 && responseCode <= 0x73) {
        TRACE("Sense Status: %s", g_senseKeys[SCSI_SENSE_KEY(senseBlock->Flags)]);
    } else {
        ERROR("Invalid Response Code: 0x%x", responseCode);
        device->IsReady = 0;
        return OS_EUNKNOWN;
    }

    // Mark ready and return
    device->IsReady = 1;
    return OS_EOK;
}

static oserr_t
__ReadCapabilities(
        _In_ MSDDevice_t* device)
{
    StorageDescriptor_t* descriptor = &device->Descriptor;
    uint32_t*            capabilitesPointer = NULL;
    oserr_t              oserr;
    enum USBTransferCode transferCode;

    oserr = dma_pool_allocate(
            UsbRetrievePool(),
            sizeof(ScsiExtendedCaps_t),
            (void**)&capabilitesPointer
    );
    if (oserr != OS_EOK) {
        ERROR("__ReadCapabilities: failed to allocate buffer");
        return oserr;
    }

    oserr = __ExecuteCommand(
            device,
            __STORAGE_OPERATION_READ,
            SCSI_READ_CAPACITY,
            0,
            dma_pool_handle(UsbRetrievePool()),
            dma_pool_offset(UsbRetrievePool(), capabilitesPointer),
            8,
            &transferCode
    );
    if (oserr != OS_EOK || transferCode != USBTRANSFERCODE_SUCCESS) {
        dma_pool_free(UsbRetrievePool(), (void*)capabilitesPointer);
        return oserr;
    }

    // If the size equals max, then we need to use extended
    // capabilities
    if (capabilitesPointer[0] == 0xFFFFFFFF) {
        ScsiExtendedCaps_t* extendedCaps = (ScsiExtendedCaps_t*)capabilitesPointer;

        // Perform extended-caps read command
        oserr = __ExecuteCommand(
                device,
                __STORAGE_OPERATION_READ,
                SCSI_READ_CAPACITY_16,
                0,
                dma_pool_handle(UsbRetrievePool()),
                dma_pool_offset(UsbRetrievePool(), capabilitesPointer),
                sizeof(ScsiExtendedCaps_t),
                &transferCode
        );
        if (oserr != OS_EOK || transferCode != USBTRANSFERCODE_SUCCESS) {
            dma_pool_free(UsbRetrievePool(), (void*)capabilitesPointer);
            return oserr;
        }

        // Capabilities are returned in reverse byte-order
        descriptor->SectorCount = rev64(extendedCaps->SectorCount) + 1;
        descriptor->SectorSize = rev32(extendedCaps->SectorSize);
        TRACE("__ReadCapabilities: sectorCount=%llu, sectorSize=%u", descriptor->SectorCount,
              descriptor->SectorSize);
        device->IsExtended = 1;
        dma_pool_free(UsbRetrievePool(), (void*)capabilitesPointer);
        return OS_EOK;
    }

    // Capabilities are returned in reverse byte-order
    descriptor->SectorCount = (uint64_t)rev32(capabilitesPointer[0]) + 1;
    descriptor->SectorSize = rev32(capabilitesPointer[1]);
    TRACE("__ReadCapabilities: sectorCount=%llu, sectorSize=%u",
          descriptor->SectorCount, descriptor->SectorSize);
    dma_pool_free(UsbRetrievePool(), (void*)capabilitesPointer);
    return OS_EOK;
}

oserr_t
MSDDeviceStart(
        _In_ MSDDevice_t* device)
{
    enum USBTransferCode transferCode;
    ScsiInquiry_t*       inquiryData = NULL;
    int                  i;
    oserr_t              oserr;

    // How many iterations of device-ready?
    // Floppys need a lot longer to spin up
    i = (device->Protocol != ProtocolCB && device->Protocol != ProtocolCBI) ? 30 : 3;

    oserr = dma_pool_allocate(
            UsbRetrievePool(),
            sizeof(ScsiInquiry_t),
            (void**)&inquiryData
    );
    if (oserr != OS_EOK) {
        ERROR("MSDDeviceStart: failed to allocate buffer");
        return OS_EUNKNOWN;
    }

    oserr = __ExecuteCommand(
            device,
            __STORAGE_OPERATION_READ,
            SCSI_INQUIRY,
            0,
            dma_pool_handle(UsbRetrievePool()),
            dma_pool_offset(UsbRetrievePool(), inquiryData),
            sizeof(ScsiInquiry_t),
            &transferCode
    );
    if (oserr != OS_EOK || transferCode != USBTRANSFERCODE_SUCCESS) {
        ERROR("MSDDeviceStart: failed to perform the inquiry command on device: %u", transferCode);
        dma_pool_free(UsbRetrievePool(), (void*)inquiryData);
        return oserr;
    }

    // Perform the Test-Unit Ready command
    while (device->IsReady == 0 && i != 0) {
        __RequestSenseReady(device);
        if (device->IsReady == 1) {
            break; 
        }
        thrd_sleep(&(struct timespec) { .tv_nsec = 100 * NSEC_PER_MSEC }, NULL);
        i--;
    }

    // Sanitize the resulting ready state, we need it 
    // ready otherwise we can't use it
    if (!device->IsReady) {
        ERROR("MSDDeviceStart: failed to ready device");
        dma_pool_free(UsbRetrievePool(), (void*)inquiryData);
        return OS_EUNKNOWN;
    }

    dma_pool_free(UsbRetrievePool(), (void*)inquiryData);
    return __ReadCapabilities(device);
}

static void
__SelectScsiTransferCommand(
        _In_  MSDDevice_t* device,
        _In_  int          direction,
        _Out_ uint8_t*     commandOut,
        _Out_ size_t*      maxSectorsCountOut)
{
    // Detect limits based on type of device and protocol
    if (device->Protocol == ProtocolCB || device->Protocol == ProtocolCBI) {
        *commandOut         = (direction == __STORAGE_OPERATION_READ) ? SCSI_READ_6 : SCSI_WRITE_6;
        *maxSectorsCountOut = UINT8_MAX;
    } else if (!device->IsExtended) {
        *commandOut         = (direction == __STORAGE_OPERATION_READ) ? SCSI_READ : SCSI_WRITE;
        *maxSectorsCountOut = UINT16_MAX;
    } else {
        *commandOut         = (direction == __STORAGE_OPERATION_READ) ? SCSI_READ_16 : SCSI_WRITE_16;
        *maxSectorsCountOut = UINT32_MAX;
    }
}

static oserr_t
__TransferSectors(
        _In_  MSDDevice_t* device,
        _In_  int          direction,
        _In_  uint64_t     sector,
        _In_  uuid_t       bufferHandle,
        _In_  unsigned int bufferOffset,
        _In_  size_t       sectorCount,
        _Out_ size_t*      sectorsTransferred)
{
    enum USBTransferCode transferCode;
    oserr_t              oserr;
    size_t               sectorsToBeTransferred;
    uint8_t              command;
    size_t               maxSectorsPerCommand;

    TRACE("__TransferSectors(direction=%u, sector=%llu, count=%" PRIuIN ")",
        direction, sector, sectorCount);

    // Protect against bad start sector
    if (sector >= device->Descriptor.SectorCount) {
        return OS_EINVALPARAMS;
    }

    // Of course it's possible that the requester is requesting too much data in one
    // go, so we will have to clamp some of the values. Is the sector valid first of all?
    TRACE("__TransferSectors: max sector available: %" PRIuIN, device->Descriptor.SectorCount);
    sectorsToBeTransferred = sectorCount;
    if ((sector + sectorsToBeTransferred) >= device->Descriptor.SectorCount) {
        sectorsToBeTransferred = device->Descriptor.SectorCount - sector;
    }
    
    // Detect limits based on type of device and protocol
    __SelectScsiTransferCommand(device, direction, &command, &maxSectorsPerCommand);
    sectorsToBeTransferred = MIN(sectorsToBeTransferred, maxSectorsPerCommand);

    // protect against zero reads
    if (sectorsToBeTransferred == 0) {
        return OS_EINVALPARAMS;
    }

    TRACE("__TransferSectors: command %u, max sectors for command %u", command, maxSectorsPerCommand);
    oserr = __ExecuteCommand(
            device,
            direction,
            command,
            sector,
            bufferHandle,
            bufferOffset,
            sectorsToBeTransferred * device->Descriptor.SectorSize,
            &transferCode
    );
    if (oserr != OS_EOK || transferCode != USBTRANSFERCODE_SUCCESS) {
        if (sectorsTransferred) {
            *sectorsTransferred = 0;
        }
        return oserr;
    } else if (sectorsTransferred) {
        *sectorsTransferred = sectorsToBeTransferred;
        if (device->StatusBlock->DataResidue) {
            // Data residue is in bytes not transferred as it does not seem
            // required that we transfer in sectors
            size_t SectorsLeft = DIVUP(device->StatusBlock->DataResidue, 
                device->Descriptor.SectorSize);
            *sectorsTransferred -= SectorsLeft;
        }
    }
    return OS_EOK;
}

void ctt_storage_transfer_invocation(struct gracht_message* message, const uuid_t deviceId,
                                     const enum sys_transfer_direction direction, const unsigned int sectorLow, const unsigned int sectorHigh,
                                     const uuid_t bufferId, const size_t offset, const size_t sectorCount)
{
    MSDDevice_t* device = MsdDeviceGet(deviceId);
    oserr_t      status;
    UInteger64_t sector;
    size_t       sectorsTransferred;
    
    sector.u.LowPart = sectorLow;
    sector.u.HighPart = sectorHigh;
    
    status = __TransferSectors(
            device,
            (int)direction,
            sector.QuadPart,
            bufferId,
            offset,
            sectorCount,
            &sectorsTransferred
    );
    ctt_storage_transfer_response(message, status, sectorsTransferred);
}
