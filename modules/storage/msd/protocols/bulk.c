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
 *  - Bulk Protocol Implementation
 */
#define __TRACE

#define BULK_RESET      0x1
#define BULK_RESET_IN   0x2
#define BULK_RESET_OUT  0x4
#define BULK_RESET_ALL  (BULK_RESET | BULK_RESET_IN | BULK_RESET_OUT)

/* Includes
 * - System */
#include <os/thread.h>
#include <os/utils.h>
#include <os/driver/usb.h>
#include "../msd.h"

/* BulkReset
 * Performs an Bulk-Only Mass Storage Reset
 * Bulk endpoint data toggles and STALL conditions are preserved. */
OsStatus_t
BulkReset(
    _In_ MsdDevice_t *Device)
{
    // Variables
    UsbTransferStatus_t Status  = TransferNAK;
    int Iterations              = 0;

    // Reset is 
    // 0x21 | 0xFF | wIndex - Interface
    while (Status == TransferNAK) {
        Status = UsbExecutePacket(Device->Base.DriverId, Device->Base.DeviceId, 
            &Device->Base.Device, Device->Control,
            USBPACKET_DIRECTION_CLASS | USBPACKET_DIRECTION_INTERFACE,
            MSD_REQUEST_RESET, 0, 0, (uint16_t)Device->Base.Interface.Id, 0, NULL);
        Iterations++;
    }

    TRACE("Reset done after %i iterations", Iterations);
    if (Status != TransferFinished) {
        ERROR("Reset bulk returned error %u", Status);
        return OsError;
    }
    else {
        return OsSuccess;
    }
}

/* BulkResetRecovery
 * Performs a full interface and endpoint reset, should only be used
 * for fatal interface errors. */
OsStatus_t
BulkResetRecovery(
    _In_ MsdDevice_t *Device,
    _In_ int ResetType)
{
    // Debug
    TRACE("MsdRecoveryReset(Type %i)", ResetType);

    // Perform an initial reset
    if ((ResetType & BULK_RESET) 
        && BulkReset(Device) != OsSuccess) {
        ERROR("Failed to reset bulk interface");
        return OsError;
    }

    // Clear HALT/STALL features on both in and out endpoints
    if ((ResetType & BULK_RESET_IN) 
        && UsbClearFeature(Device->Base.DriverId, Device->Base.DeviceId, 
        &Device->Base.Device, Device->Control, USBPACKET_DIRECTION_ENDPOINT,
        Device->In->Address, USB_FEATURE_HALT) != TransferFinished) {
        ERROR("Failed to clear STALL on endpoint (in)");
        return OsError;
    }
    if ((ResetType & BULK_RESET_OUT) 
        && UsbClearFeature(Device->Base.DriverId, Device->Base.DeviceId, 
        &Device->Base.Device, Device->Control, USBPACKET_DIRECTION_ENDPOINT,
        Device->Out->Address, USB_FEATURE_HALT) != TransferFinished) {
        ERROR("Failed to clear STALL on endpoint (out)");
        return OsError;
    }

    // Reset interface again
    return OsSuccess;
}

/* BulkInitialize 
 * Validates the available endpoints and initializes the device. */
OsStatus_t
BulkInitialize(
    _In_ MsdDevice_t *Device)
{
    // Sanitize found endpoints
    if (Device->In == NULL || Device->Out == NULL) {
        ERROR("Either in or out endpoint not available on device");
        return OsError;
    }

    // Perform a bulk reset
    if (BulkReset(Device) != OsSuccess) {
        ERROR("Failed to reset the bulk interface");
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

/* MsdSanitizeResponse
 * Used for making sure the CSW we get back is valid */
UsbTransferStatus_t
MsdSanitizeResponse(
    _In_ MsdDevice_t *Device, 
    _In_ MsdCommandStatus_t *Csw)
{
    // Start out by checking if a phase error occured, in that case we are screwed
    if (Csw->Status == MSD_CSW_PHASE_ERROR) {
        ERROR("Phase error returned in CSW.");
        if (BulkResetRecovery(Device, BULK_RESET_ALL) != OsSuccess) {
            ERROR("Failed to perform a recovery reset.");
        }
        return TransferInvalid;
    }

    // Sanitize signature/data integrity
    if (Csw->Signature != MSD_CSW_OK_SIGNATURE) {
        ERROR("CSW: Signature is invalid: 0x%x", Csw->Signature);
        return TransferInvalid;
    }
    
    // Sanitize tag/data integrity
    if ((Csw->Tag & 0xFFFFFF00) != MSD_TAG_SIGNATURE) {
        ERROR("CSW: Tag is invalid: 0x%x", Csw->Tag);
        return TransferInvalid;
    }

    // Sanitize status
    if (Csw->Status != MSD_CSW_OK) {
        ERROR("CSW: Status is invalid: 0x%x", Csw->Status);
        return TransferInvalid;
    }

    // Everything should be fine
    return TransferFinished;
}

/* BulkSendCommand
 * Sends a new command on the bulk protocol. */
UsbTransferStatus_t 
BulkSendCommand(
    _In_ MsdDevice_t *Device,
    _In_ uint8_t ScsiCommand,
    _In_ uint64_t SectorStart,
    _In_ uintptr_t DataAddress,
    _In_ size_t DataLength)
{
    // Variables
    UsbTransferResult_t Result  = { 0 };
    UsbTransfer_t CommandStage  = { 0 };

    // Debug
    TRACE("BulkSendCommand(Command %u, Start %u, Length %u)",
        ScsiCommand, LODWORD(SectorStart), DataLength);

    // Construct our command build the usb transfer
    MsdSCSICommmandConstruct(Device->CommandBlock, ScsiCommand, SectorStart, 
        DataLength, (uint16_t)Device->Descriptor.SectorSize);
    UsbTransferInitialize(&CommandStage, &Device->Base.Device, 
        Device->Out, BulkTransfer);
    UsbTransferOut(&CommandStage, Device->CommandBlockAddress, 
        sizeof(MsdCommandBlock_t), 0);
    UsbTransferQueue(Device->Base.DriverId, Device->Base.DeviceId, 
        &CommandStage, &Result);

    // Sanitize for any transport errors
    if (Result.Status != TransferFinished) {
        ERROR("Failed to send the CBW command, transfer-code %u", Result.Status);
        if (Result.Status == TransferStalled) {
            ERROR("Performing a recovery-reset on device.");
            if (BulkResetRecovery(Device, BULK_RESET_ALL) != OsSuccess) {
                ERROR("Failed to reset device, it is now unusable.");
            }
        }
    }
    return Result.Status;
}

/* BulkReadData
 * Tries to read a bulk data response from the device. */
UsbTransferStatus_t 
BulkReadData(
    _In_ MsdDevice_t *Device,
    _In_ uintptr_t DataAddress,
    _In_ size_t DataLength)
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
    // The host shall accept the data received.
    // The host shall clear the Bulk-In pipe.
    if (Result.Status != TransferFinished) {
        ERROR("Data-stage failed with status %u, cleaning up bulk-in", Result.Status);
        if (Result.Status == TransferStalled) {
            BulkResetRecovery(Device, BULK_RESET_IN);
        }
        else {
            // Fatal error
            BulkResetRecovery(Device, BULK_RESET_ALL);
        }
    }

    // Return state
    return Result.Status;
}

/* BulkWriteData
 * Tries to write a bulk data packet to the device. */
UsbTransferStatus_t 
BulkWriteData(
    _In_ MsdDevice_t *Device,
    _In_ uintptr_t DataAddress,
    _In_ size_t DataLength)
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
    // The host shall accept the data received.
    // The host shall clear the Bulk-In pipe.
    if (Result.Status != TransferFinished) {
        ERROR("Data-stage failed with status %u, cleaning up bulk-out", Result.Status);
        if (Result.Status == TransferStalled) {
            BulkResetRecovery(Device, BULK_RESET_OUT);
        }
        else {
            // Fatal error
            BulkResetRecovery(Device, BULK_RESET_ALL);
        }
    }

    // Return state
    return Result.Status;
}

/* BulkGetStatus
 * Tries to retrieve a command-status response from the device. 
 * This will be retried up to MSD_CSW_RETRIES count. */
UsbTransferStatus_t 
BulkGetStatus(
    _In_ MsdDevice_t *Device)
{
    // Variables
    UsbTransferResult_t Result  = { 0 };
    UsbTransfer_t StatusStage   = { 0 };

    // Debug
    TRACE("BulkGetStatus()");

    // Perform the transfer
    UsbTransferInitialize(&StatusStage, &Device->Base.Device, 
        Device->In, BulkTransfer);
    UsbTransferIn(&StatusStage, Device->StatusBlockAddress, 
        sizeof(MsdCommandStatus_t), 0);
    UsbTransferQueue(Device->Base.DriverId, Device->Base.DeviceId, 
        &StatusStage, &Result);

    // Sanitize for any transport errors
    // On a STALL condition receiving the CSW, then:
    // The host shall clear the Bulk-In pipe.
    // The host shall again attempt to receive the CSW.
    // If the host receives a CSW which is not valid, 
    // then the host shall perform a Reset Recovery. If the host receives
    // a CSW which is not meaningful, then the host may perform a Reset Recovery.
    // retries @todo
    if (Result.Status != TransferFinished) {
        ERROR("Failed to retrieve the CSW block, transfer-code %u", Result.Status);
    }
    else {
        Result.Status = MsdSanitizeResponse(Device, Device->StatusBlock);
    }
    return Result.Status;
}

/* Global 
 * - Static function table */
MsdOperations_t BulkOperations = {
    BulkInitialize,
    BulkSendCommand,
    BulkReadData,
    BulkWriteData,
    BulkGetStatus
};
