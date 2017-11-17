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
#define __TRACE

/* Includes
 * - System */
#include <os/thread.h>
#include <os/utils.h>
#include <os/driver/usb.h>
#include "msd.h"

/* Protocol-Table
 * Used for setting up the different protocol implemtation 
 * function tables. */
__EXTERN MsdOperations_t BulkOperations;
__EXTERN MsdOperations_t UfiOperations;
static MsdOperations_t *ProtocolOperations[ProtocolCount] = {
    NULL,
    &UfiOperations,
    NULL,
    NULL,
    &BulkOperations
};

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

/* MsdDeviceInitialize 
 * Initializes and validates that the protocol has all neccessary
 * resources/endpoints/prerequisites for operation. */
OsStatus_t
MsdDeviceInitialize(
    _In_ MsdDevice_t *Device)
{
    // Install operations
    if (ProtocolOperations[Device->Protocol] == NULL) {
        ERROR("Support is not implemented for the protocol.");
        return OsError;
    }

    // Get them and initialize
    Device->Operations = ProtocolOperations[Device->Protocol];
    return Device->Operations->Initialize(Device);
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
    Status = UsbExecutePacket(Device->Base.DriverId, Device->Base.DeviceId, 
        &Device->Base.Device, Device->Control,
        USBPACKET_DIRECTION_IN | USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_INTERFACE,
        MSD_REQUEST_GET_MAX_LUN, 0, 0, (uint16_t)Device->Base.Interface.Id, 1, &MaxLuns);

    // If no multiple LUNS are supported, device may STALL
    // it says in the usbmassbulk spec 
    // but thats ok, it's not a functional stall, only command stall
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
    UsbTransferStatus_t Status;

    // Debug
    TRACE("MsdSCSICommandIn(Command %u, Start %u, Length %u)",
        ScsiCommand, LODWORD(SectorStart), DataLength);

    // Send the command
    Status = Device->Operations->SendCommand(Device, ScsiCommand, 
        SectorStart, DataAddress, DataLength);

    // Sanitize for any transport errors
    if (Status != TransferFinished) {
        ERROR("Failed to send the CBW command (in), transfer-code %u", Status);
        return Status;
    }

    // Do the data stage (shared for all protocol)
    if (DataLength != 0) {
        Status = Device->Operations->ReadData(Device, DataAddress, DataLength);
        if (Status != TransferFinished && Status != TransferStalled) {
            ERROR("Fatal error in ReadData, skipping status stage");
            return Status;
        }
    }

    // Perform the status stage
    return Device->Operations->GetStatus(Device);
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
    UsbTransferStatus_t Status;

    // It is invalid to send zero length packets for bulk
    if (DataLength == 0) {
        ERROR("Cannot write data of length 0 to MSD devices.");
        return TransferInvalid;
    }

    // Send the command
    Status = Device->Operations->SendCommand(Device, ScsiCommand, 
        SectorStart, DataAddress, DataLength);

    // Sanitize for any transport errors
    if (Status != TransferFinished) {
        ERROR("Failed to send the CBW command (out), transfer-code %u", Status);
        return Status;
    }

    Status = Device->Operations->WriteData(Device, DataAddress, DataLength);
    if (Status != TransferFinished && Status != TransferStalled) {
        ERROR("Fatal error in ReadData, skipping status stage");
        return Status;
    }

    // Perform the status stage
    return Device->Operations->GetStatus(Device);
}

/* MsdDevicePrepare
 * Ready's the device by performing TDR's and requesting sense-status. */
OsStatus_t
MsdDevicePrepare(
    MsdDevice_t *Device)
{
    // Variables
    ScsiSense_t *SenseBlock = NULL;
    uintptr_t SenseBlockPhysical = 0;
    int ResponseCode, SenseKey;

    // Debug
    TRACE("MsdPrepareDevice()");

    // Allocate memory buffer
    if (BufferPoolAllocate(UsbRetrievePool(), sizeof(ScsiSense_t), 
        (uintptr_t**)&SenseBlock, &SenseBlockPhysical) != OsSuccess) {
        ERROR("Failed to allocate buffer (sense)");
        return OsError;
    }

    // Don't use test-unit-ready for UFI
    if (Device->Protocol != ProtocolUFI) {
        if (MsdSCSICommandIn(Device, SCSI_TEST_UNIT_READY, 0, 0, 0)
                != TransferFinished) {
            ERROR("Failed to perform test-unit-ready command");
            BufferPoolFree(UsbRetrievePool(), (uintptr_t*)SenseBlock);
            Device->IsReady = 0;
            return OsError;
        }
    }

    // Now request the sense-status
    if (MsdSCSICommandIn(Device, SCSI_REQUEST_SENSE, 0, 
        SenseBlockPhysical, sizeof(ScsiSense_t)) != TransferFinished) {
        ERROR("Failed to perform sense command");
        BufferPoolFree(UsbRetrievePool(), (uintptr_t*)SenseBlock);
        Device->IsReady = 0;
        return OsError;
    }

    // Extract sense-codes and key
    ResponseCode = SCSI_SENSE_RESPONSECODE(SenseBlock->ResponseStatus);
    SenseKey = SCSI_SENSE_KEY(SenseBlock->Flags);

    // Cleanup
    BufferPoolFree(UsbRetrievePool(), (uintptr_t*)SenseBlock);

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

/* MsdReadCapabilities
 * Reads the geometric capabilities of the device as sector-count and sector-size. */
OsStatus_t 
MsdReadCapabilities(
    _In_ MsdDevice_t *Device)
{
    // Variables
    StorageDescriptor_t *Descriptor = &Device->Descriptor;
    uint32_t *CapabilitesPointer = NULL;
    uintptr_t CapabilitiesAddress = 0;

    // Allocate buffer
    if (BufferPoolAllocate(UsbRetrievePool(), sizeof(ScsiExtendedCaps_t), 
        (uintptr_t**)&CapabilitesPointer, &CapabilitiesAddress) != OsSuccess) {
        ERROR("Failed to allocate buffer (caps)");
        return OsError;
    }

    // Perform caps-command
    if (MsdSCSICommandIn(Device, SCSI_READ_CAPACITY, 0, 
        CapabilitiesAddress, 8) != TransferFinished) {
        BufferPoolFree(UsbRetrievePool(), (uintptr_t*)CapabilitesPointer);
        return OsError;
    }

    // If the size equals max, then we need to use extended
    // capabilities
    if (CapabilitesPointer[0] == 0xFFFFFFFF) {
        // Variables
        ScsiExtendedCaps_t *ExtendedCaps = (ScsiExtendedCaps_t*)CapabilitesPointer;

        // Perform extended-caps read command
        if (MsdSCSICommandIn(Device, SCSI_READ_CAPACITY_16, 0, 
            CapabilitiesAddress, sizeof(ScsiExtendedCaps_t)) != TransferFinished) {
            BufferPoolFree(UsbRetrievePool(), (uintptr_t*)CapabilitesPointer);
            return OsError;
        }

        // Capabilities are returned in reverse byte-order
        Descriptor->SectorCount = rev64(ExtendedCaps->SectorCount) + 1;
        Descriptor->SectorSize = rev32(ExtendedCaps->SectorSize);
        Device->IsExtended = 1;
        BufferPoolFree(UsbRetrievePool(), (uintptr_t*)CapabilitesPointer);
        return OsSuccess;
    }

    // Capabilities are returned in reverse byte-order
    Descriptor->SectorCount = (uint64_t)rev32(CapabilitesPointer[0]) + 1;
    Descriptor->SectorSize = rev32(CapabilitesPointer[1]);
    BufferPoolFree(UsbRetrievePool(), (uintptr_t*)CapabilitesPointer);
    return OsSuccess;
}

/* MsdDeviceStart
 * Initializes the device by performing one-time setup and reading device
 * capabilities and features. */
OsStatus_t
MsdDeviceStart(
    _In_ MsdDevice_t *Device)
{
    // Variables
    UsbTransferStatus_t Status  = TransferNotProcessed;
    ScsiInquiry_t *InquiryData  = NULL;
    uintptr_t InquiryAddress    = 0;
    int i;

    // How many iterations of device-ready?
    // Floppys need a lot longer to spin up
    i = (Device->Protocol == ProtocolUFI) ? 30 : 3;

    // Allocate space for inquiry
    if (BufferPoolAllocate(UsbRetrievePool(), sizeof(ScsiInquiry_t), 
        (uintptr_t**)&InquiryData, &InquiryAddress) != OsSuccess) {
        ERROR("Failed to allocate buffer (inquiry)");
        return OsError;
    }

    // Perform inquiry
    Status = MsdSCSICommandIn(Device, SCSI_INQUIRY, 0, 
        InquiryAddress, sizeof(ScsiInquiry_t));
    if (Status != TransferFinished) {
        ERROR("Failed to perform the inquiry command on device: %u", Status);
        BufferPoolFree(UsbRetrievePool(), (uintptr_t*)InquiryData);
        return OsError;
    }

    // Perform the Test-Unit Ready command
    while (Device->IsReady == 0 && i != 0) {
        MsdDevicePrepare(Device);
        if (Device->IsReady == 1) {
            break; 
        }
        ThreadSleep(100);
        i--;
    }

    // Sanitize the resulting ready state, we need it 
    // ready otherwise we can't use it
    if (!Device->IsReady) {
        ERROR("Failed to ready device");
        BufferPoolFree(UsbRetrievePool(), (uintptr_t*)InquiryData);
        return OsError;
    }

    // Cleanup the inquiry data
    BufferPoolFree(UsbRetrievePool(), (uintptr_t*)InquiryData);

    // Last thing to do is to read caps
    return MsdReadCapabilities(Device);
}

/* MsdReadSectors
 * Read a given amount of sectors (bytes/sector-size) from the MSD. */
OsStatus_t
MsdReadSectors(
    _In_ MsdDevice_t *Device,
    _In_ uint64_t SectorStart, 
    _In_ uintptr_t BufferAddress,
    _In_ size_t BufferLength,
    _Out_ size_t *BytesRead)
{
    // Variables
    UsbTransferStatus_t Result;

    // Debug
    TRACE("MsdReadSectors(Sector %u, Length %u, Address 0x%x)",
        LODWORD(SectorStart), BufferLength, BufferAddress);

    // Perform the read command
    Result = MsdSCSICommandIn(Device, 
        Device->IsExtended == 0 ? SCSI_READ : SCSI_READ_16,
        SectorStart, BufferAddress, BufferLength);

    TRACE("Read %u bytes", (BufferLength - Device->StatusBlock->DataResidue));

    // Sanitize result
    if (Result != TransferFinished) {
        if (BytesRead != NULL) {
            *BytesRead = 0;
        }
        return OsError;
    }
    else {
        // Calculate the number of bytes (not)transferred
        if (BytesRead != NULL) {
            *BytesRead = (BufferLength - Device->StatusBlock->DataResidue);
        }
        return OsSuccess;
    }
}

/* MsdWriteSectors
 * Write a given amount of sectors (bytes/sector-size) to the MSD. */
OsStatus_t
MsdWriteSectors(
    _In_ MsdDevice_t *Device,
    _In_ uint64_t SectorStart, 
    _In_ uintptr_t BufferAddress,
    _In_ size_t BufferLength,
    _Out_ size_t *BytesWritten)
{
    // Variables
    UsbTransferStatus_t Result;

    // Perform the read command
    Result = MsdSCSICommandOut(Device, 
        Device->IsExtended == 0 ? SCSI_WRITE : SCSI_WRITE_16,
        SectorStart, BufferAddress, BufferLength);

    // Sanitize result
    if (Result != TransferFinished) {
        if (BytesWritten != NULL) {
            *BytesWritten = 0;
        }
        return OsError;
    }
    else {
        // Calculate the number of bytes (not)transferred
        if (BytesWritten != NULL) {
            *BytesWritten = (BufferLength - Device->StatusBlock->DataResidue);
        }
        return OsSuccess;
    }
}
