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

extern MsdOperations_t  BulkOperations;
extern MsdOperations_t  UfiOperations;
static const MsdOperations_t* g_protocolOperations[ProtocolCount] = {
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
MsdDeviceInitialize(
    _In_ MsdDevice_t *Device)
{
    if (!g_protocolOperations[Device->Protocol]) {
        ERROR("Support is not implemented for the protocol.");
        return OS_ENOTSUPPORTED;
    }

    Device->Operations = g_protocolOperations[Device->Protocol];
    return Device->Operations->Initialize(Device);
}

oserr_t
MsdGetMaximumLunCount(
    _In_ MsdDevice_t *Device)
{
    enum USBTransferCode Status;
    uint8_t             MaxLuns;

    // Get Max LUNS is
    // 0xA1 | 0xFE | wIndex - Interface 
    // 1 Byte is expected back, values range between 0 - 15, 0-indexed
    Status = UsbExecutePacket(&Device->Device->DeviceContext,
        USBPACKET_DIRECTION_IN | USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_INTERFACE,
        MSD_REQUEST_GET_MAX_LUN, 0, 0, (uint16_t)Device->InterfaceId, 1, &MaxLuns);

    // If no multiple LUNS are supported, device may STALL it says in the usbmassbulk spec 
    // but thats ok, it's not a functional stall, only command stall
    if (Status == USBTRANSFERCODE_SUCCESS) {
        Device->Descriptor.LUNCount = (size_t)(MaxLuns & 0xF);
    } else {
        Device->Descriptor.LUNCount = 0;
    }
    return OS_EOK;
}

static oserr_t
__ExecuteCommand(
        _In_ MsdDevice_t*           device,
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

    // It is invalid to send zero length packets for bulk
    if (direction == 1 && dataLength == 0) {
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
        if (direction == 0) {
            oserr = device->Operations->ReadData(
                    device,
                    bufferHandle,
                    bufferOffset,
                    bytesLeft,
                    &transferCode,
                    &bytesTransferred
            );
        } else {
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
            // If this error is not OK, then it was not a transport error, but rather something
            // critical with our setup. There is only one error we can fix, and that's if the buffer
            // was invalid. That means the user-supplied buffer was using an offset that causes the
            // buffer to cross boundary, and this is not always supported.
            if (oserr == OS_EBUFFER) {
                // Allocate a temporary transport buffer, there will be no chance of crossing boundaries
                // here as we always transport in sector sizes.
                OSHandle_t tempBuffer;
                oserr = SHMCreate(
                        &(SHM_t) {
                            .Flags = SHM_CONFORM | SHM_COMMIT,
                            .Conformity = device->Conformity,
                            .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                            .Size = dataLength
                        },
                        &tempBuffer
                );
                if (oserr != OS_EOK) {
                    ERROR("__ExecuteCommand: failed to allocate transfer buffer for command");
                    return oserr;
                }

                // Reperform the transfer

                // Copy the data....????

                OSHandleDestroy(&tempBuffer);
            }
            return oserr;
        }

        if (transferCode != USBTRANSFERCODE_SUCCESS && transferCode != USBTRANSFERCODE_STALL) {
            ERROR("Fatal error transfering data, skipping status stage");
            return transferCode;
        }
        if (transferCode == USBTRANSFERCODE_STALL) {
            RetryCount--;
            if (RetryCount == 0) {
                ERROR("Fatal error transfering data, skipping status stage");
                return transferCode;
            }
        }
        bytesLeft -= bytesTransferred;
        bufferOffset += bytesTransferred;
    }
    return device->Operations->GetStatus(device, transferCodeOut);
}

oserr_t
MsdDevicePrepare(
    MsdDevice_t *Device)
{
    ScsiSense_t *SenseBlock = NULL;
    int ResponseCode, SenseKey;

    // Debug
    TRACE("MsdPrepareDevice()");

    // Allocate memory buffer
    if (dma_pool_allocate(UsbRetrievePool(), sizeof(ScsiSense_t), 
        (void**)&SenseBlock) != OS_EOK) {
        ERROR("Failed to allocate buffer (sense)");
        return OS_EUNKNOWN;
    }

    // Don't use test-unit-ready for UFI
    if (Device->Protocol != ProtocolCB && Device->Protocol != ProtocolCBI) {
        if (__ExecuteCommand(Device, 0, SCSI_TEST_UNIT_READY, 0, 0, 0, 0)
            != USBTRANSFERCODE_SUCCESS) {
            ERROR("Failed to perform test-unit-ready command");
            dma_pool_free(UsbRetrievePool(), (void*)SenseBlock);
            Device->IsReady = 0;
            return OS_EUNKNOWN;
        }
    }

    // Now request the sense-status
    if (__ExecuteCommand(Device, 0, SCSI_REQUEST_SENSE, 0,
                         dma_pool_handle(UsbRetrievePool()), dma_pool_offset(UsbRetrievePool(), SenseBlock),
                         sizeof(ScsiSense_t)) != USBTRANSFERCODE_SUCCESS) {
        ERROR("Failed to perform sense command");
        dma_pool_free(UsbRetrievePool(), (void*)SenseBlock);
        Device->IsReady = 0;
        return OS_EUNKNOWN;
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
        return OS_EUNKNOWN;
    }

    // Mark ready and return
    Device->IsReady = 1;
    return OS_EOK;
}

oserr_t
MsdReadCapabilities(
    _In_ MsdDevice_t *Device)
{
    StorageDescriptor_t* descriptor = &Device->Descriptor;
    uint32_t*            capabilitesPointer = NULL;

    if (dma_pool_allocate(UsbRetrievePool(), sizeof(ScsiExtendedCaps_t), 
        (void**)&capabilitesPointer) != OS_EOK) {
        ERROR("Failed to allocate buffer (caps)");
        return OS_EUNKNOWN;
    }

    // Perform caps-command
    if (__ExecuteCommand(Device, 0, SCSI_READ_CAPACITY, 0,
                         dma_pool_handle(UsbRetrievePool()), dma_pool_offset(UsbRetrievePool(), capabilitesPointer),
                         8) != USBTRANSFERCODE_SUCCESS) {
        dma_pool_free(UsbRetrievePool(), (void*)capabilitesPointer);
        return OS_EUNKNOWN;
    }

    // If the size equals max, then we need to use extended
    // capabilities
    if (capabilitesPointer[0] == 0xFFFFFFFF) {
        ScsiExtendedCaps_t *ExtendedCaps = (ScsiExtendedCaps_t*)capabilitesPointer;

        // Perform extended-caps read command
        if (__ExecuteCommand(Device, 0, SCSI_READ_CAPACITY_16, 0,
                             dma_pool_handle(UsbRetrievePool()), dma_pool_offset(UsbRetrievePool(), capabilitesPointer),
                             sizeof(ScsiExtendedCaps_t)) != USBTRANSFERCODE_SUCCESS) {
            dma_pool_free(UsbRetrievePool(), (void*)capabilitesPointer);
            return OS_EUNKNOWN;
        }

        // Capabilities are returned in reverse byte-order
        descriptor->SectorCount = rev64(ExtendedCaps->SectorCount) + 1;
        descriptor->SectorSize = rev32(ExtendedCaps->SectorSize);
        TRACE("[msd] [read_capabilities] sectorCount %llu, sectorSize %u", descriptor->SectorCount,
              descriptor->SectorSize);
        Device->IsExtended = 1;
        dma_pool_free(UsbRetrievePool(), (void*)capabilitesPointer);
        return OS_EOK;
    }

    // Capabilities are returned in reverse byte-order
    descriptor->SectorCount = (uint64_t)rev32(capabilitesPointer[0]) + 1;
    descriptor->SectorSize = rev32(capabilitesPointer[1]);
    TRACE("[msd] [read_capabilities] 0x%llx sectorCount %llu, sectorSize %u",
          &descriptor->SectorCount, descriptor->SectorCount, descriptor->SectorSize);
    dma_pool_free(UsbRetrievePool(), (void*)capabilitesPointer);
    return OS_EOK;
}

oserr_t
MsdDeviceStart(
    _In_ MsdDevice_t* device)
{
    enum USBTransferCode transferStatus;
    ScsiInquiry_t*      inquiryData = NULL;
    int                 i;

    // How many iterations of device-ready?
    // Floppys need a lot longer to spin up
    i = (device->Protocol != ProtocolCB && device->Protocol != ProtocolCBI) ? 30 : 3;

    // Allocate space for inquiry
    if (dma_pool_allocate(UsbRetrievePool(), sizeof(ScsiInquiry_t), (void**)&inquiryData) != OS_EOK) {
        ERROR("Failed to allocate buffer (inquiry)");
        return OS_EUNKNOWN;
    }

    // Perform inquiry
    transferStatus = __ExecuteCommand(device, 0, SCSI_INQUIRY, 0,
                                      dma_pool_handle(UsbRetrievePool()),
                                      dma_pool_offset(UsbRetrievePool(), inquiryData),
                                      sizeof(ScsiInquiry_t));
    if (transferStatus != USBTRANSFERCODE_SUCCESS) {
        ERROR("Failed to perform the inquiry command on device: %u", transferStatus);
        dma_pool_free(UsbRetrievePool(), (void*)inquiryData);
        return OS_EUNKNOWN;
    }

    // Perform the Test-Unit Ready command
    while (device->IsReady == 0 && i != 0) {
        MsdDevicePrepare(device);
        if (device->IsReady == 1) {
            break; 
        }
        thrd_sleep(&(struct timespec) { .tv_nsec = 100 * NSEC_PER_MSEC }, NULL);
        i--;
    }

    // Sanitize the resulting ready state, we need it 
    // ready otherwise we can't use it
    if (!device->IsReady) {
        ERROR("Failed to ready device");
        dma_pool_free(UsbRetrievePool(), (void*)inquiryData);
        return OS_EUNKNOWN;
    }


    dma_pool_free(UsbRetrievePool(), (void*)inquiryData);
    return MsdReadCapabilities(device);
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
    } else if (!device->IsExtended) {
        *commandOut         = (direction == __STORAGE_OPERATION_READ) ? SCSI_READ : SCSI_WRITE;
        *maxSectorsCountOut = UINT16_MAX;
    } else {
        *commandOut         = (direction == __STORAGE_OPERATION_READ) ? SCSI_READ_16 : SCSI_WRITE_16;
        *maxSectorsCountOut = UINT32_MAX;
    }
}

oserr_t
MsdTransferSectors(
        _In_  MsdDevice_t* device,
        _In_  int          direction,
        _In_  uint64_t     sector,
        _In_  uuid_t       bufferHandle,
        _In_  unsigned int bufferOffset,
        _In_  size_t       sectorCount,
        _Out_ size_t*      sectorsTransferred)
{
    enum USBTransferCode transferStatus;
    size_t              sectorsToBeTransferred;
    uint8_t             command;
    size_t              maxSectorsPerCommand;

    TRACE("[msd_transfer] direction %u, sector %llu, count %" PRIuIN,
        direction, sector, sectorCount);

    // Protect against bad start sector
    if (sector >= device->Descriptor.SectorCount) {
        return OS_EINVALPARAMS;
    }

    // Of course it's possible that the requester is requesting too much data in one
    // go, so we will have to clamp some of the values. Is the sector valid first of all?
    TRACE("msd_transfer max sector available: %" PRIuIN, device->Descriptor.SectorCount);
    sectorsToBeTransferred = sectorCount;
    if ((sector + sectorsToBeTransferred) >= device->Descriptor.SectorCount) {
        sectorsToBeTransferred = device->Descriptor.SectorCount - sector;
    }
    
    // Detect limits based on type of device and protocol
    SelectScsiTransferCommand(device, direction, &command, &maxSectorsPerCommand);
    sectorsToBeTransferred = MIN(sectorsToBeTransferred, maxSectorsPerCommand);

    // protect against zero reads
    if (sectorsToBeTransferred == 0) {
        return OS_EINVALPARAMS;
    }

    TRACE("[msd_transfer] command %u, max sectors for command %u", command, maxSectorsPerCommand);
    transferStatus = __ExecuteCommand(
            device,
            direction == __STORAGE_OPERATION_WRITE,
            command,
            sector,
            bufferHandle,
            bufferOffset,
            sectorsToBeTransferred * device->Descriptor.SectorSize);
    if (transferStatus != USBTRANSFERCODE_SUCCESS) {
        if (sectorsTransferred) {
            *sectorsTransferred = 0;
        }
        return OS_EUNKNOWN;
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
    MsdDevice_t*    device = MsdDeviceGet(deviceId);
    oserr_t      status;
    UInteger64_t sector;
    size_t          sectorsTransferred;
    
    sector.u.LowPart = sectorLow;
    sector.u.HighPart = sectorHigh;
    
    status = MsdTransferSectors(device, (int)direction, sector.QuadPart,
        bufferId, offset, sectorCount,
        &sectorsTransferred);
    ctt_storage_transfer_response(message, status, sectorsTransferred);
}
