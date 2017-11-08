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

#ifndef _USB_MSD_H_
#define _USB_MSD_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include "../scsi/scsi.h"

/* Includes
 * - Interfaces */
#include <os/driver/contracts/base.h>
#include <os/driver/contracts/usbhost.h>
#include <os/driver/contracts/usbdevice.h>
#include <os/driver/contracts/storage.h>

/* MSD Subclass Definitions 
 * Contains generic magic constants and definitions */
#define MSD_SUBCLASS_RBC			    0x01
#define MSD_SUBCLASS_ATAPI			    0x02
#define MSD_SUBCLASS_FLOPPY			    0x04
#define MSD_SUBCLASS_SCSI			    0x06
#define MSD_SUBCLASS_LSD_FS			    0x07	// This means we have to negotiate access before using SCSI
#define MSD_SUBCLASS_IEEE_1667		    0x08

/* MSD Protocol Definitions 
 * Contains generic magic constants and definitions */
#define MSD_PROTOCOL_CBI			    0x00	// Control/Bulk/Interrupt Transport (With Command Completion Interrupt)
#define MSD_PROTOCOL_CBI_NOINT		    0x01	// Control/Bulk/Interrupt Transport (With NO Command Completion Interrupt)
#define MSD_PROTOCOL_BULK_ONLY		    0x50	// Bulk Only Transfers
#define MSD_PROTOCOL_UAS			    0x62	// UAS

/* MSD bRequest Definitions 
 * Contains generic magic constants and definitions */
#define MSD_REQUEST_ADSC			    0x00    // Accept Device-Specific Command
#define MSD_REQUEST_GET_REQUESTS	    0xFC
#define MSD_REQUEST_PUT_REQUESTS	    0xFD
#define MSD_REQUEST_GET_MAX_LUN		    0xFE
#define MSD_REQUEST_RESET			    0xFF    // Bulk Only MSD

#define MSD_TAG_SIGNATURE		        0xB00B1E00

/* MsdCommandBlock
 * Wrapper structure for representing a SCSI command */
PACKED_TYPESTRUCT(MsdCommandBlock, {
	uint32_t                        Signature; //Must Contain 0x43425355 (little-endian)
	uint32_t                        Tag;
	uint32_t                        DataLength;
	uint8_t                         Flags;
	uint8_t                         Lun; // Bits 0-3
	uint8_t                         Length; // Length of this structure, bits 0-4
	uint8_t                         CommandBytes[16];
});

/* MsdCommandBlock::Signature
 * Contains definitions and bitfield definitions for MsdCommandBlock::Signature */
#define MSD_CBW_SIGNATURE		        0x43425355

/* MsdCommandBlock::Lun
 * Contains definitions and bitfield definitions for MsdCommandBlock::Lun */
#define MSD_CBW_LUN(Lun)		        (Lun & 0xF)

/* MsdCommandBlock::Length
 * Contains definitions and bitfield definitions for MsdCommandBlock::Length */
#define MSD_CBW_LEN(Length)		        (Length & 0x1F)

/* MsdCommandBlock::Flags
 * Contains definitions and bitfield definitions for MsdCommandBlock::Flags */
#define MSD_CBW_OUT				        0x0
#define MSD_CBW_IN				        0x80

/* MsdCommandBlockUFI
 * Wrapper structure for representing a UFI SCSI command.
 * Unified Floppy Interface (Usb Floppy Drives) */
PACKED_TYPESTRUCT(MsdCommandBlockUFI, {
	uint8_t                         CommandBytes[12];
});

/* MsdCommandStatus
 * Wrapper structure for representing a SCSI command response. */
PACKED_TYPESTRUCT(MsdCommandStatus, {
	uint32_t                        Signature; // Must Contain 0x53425355 (little-endian)
	uint32_t                        Tag;
	uint32_t                        DataResidue; // The difference in data transfered
	uint8_t                         Status;
});

/* MsdCommandStatus::Signature
 * Contains definitions and bitfield definitions for MsdCommandStatus::Signature */
#define MSD_CSW_OK_SIGNATURE	        0x53425355

/* MsdCommandStatus::Status
 * Contains definitions and bitfield definitions for MsdCommandStatus::Status */
#define MSD_CSW_OK				        0x0
#define MSD_CSW_FAIL			        0x1
#define MSD_CSW_PHASE_ERROR		        0x2

/* MsdDeviceType
 * The different types of usb msd devices. Generic flash drives
 * are represented as hard-drives. */
typedef enum _MsdDeviceType {
	ProtocolUFI,
	ProtocolCBI,
	ProtocolBulk
} MsdDeviceType_t;

/* MsdDevice
 * Represents a mass storage device. */
typedef struct _MsdDevice {
    MCoreUsbDevice_t             Base;
    MContract_t                  Contract;
    StorageDescriptor_t          Descriptor;
    MsdDeviceType_t              Type;
	int                          IsReady;
	int                          IsExtended;
    int                          AlignedAccess;

    // Reusable buffers
    MsdCommandBlock_t           *CommandBlock;
    uintptr_t                    CommandBlockAddress;
    MsdCommandStatus_t          *StatusBlock;
    uintptr_t                    StatusBlockAddress;
    
    // CBI Information
    UsbHcEndpointDescriptor_t   *Control;
    UsbHcEndpointDescriptor_t   *In;
	UsbHcEndpointDescriptor_t   *Out;
	UsbHcEndpointDescriptor_t   *Interrupt;
} MsdDevice_t;

/* MsdDeviceCreate
 * Initializes a new msd-device from the given usb-device */
__EXTERN
MsdDevice_t*
MsdDeviceCreate(
    _In_ MCoreUsbDevice_t *UsbDevice);

/* MsdDeviceDestroy
 * Destroys an existing msd device instance and cleans up
 * any resources related to it */
__EXTERN
OsStatus_t
MsdDeviceDestroy(
    _In_ MsdDevice_t *Device);

/* MsdResetBulk
 * Performs an Bulk-Only Mass Storage Reset
 * Bulk endpoint data toggles and STALL conditions are preserved. */
__EXTERN
OsStatus_t
MsdResetBulk(
    _In_ MsdDevice_t *Device);

/* MsdRecoveryReset
 * Performs a full interface and endpoint reset, should only be used
 * for fatal interface errors. */
__EXTERN
OsStatus_t
MsdRecoveryReset(
    _In_ MsdDevice_t *Device);

/* MsdGetMaximumLunCount
 * Retrieves the maximum logical unit count for the given msd-device. */
__EXTERN
OsStatus_t
MsdGetMaximumLunCount(
    _In_ MsdDevice_t *Device);

/* MsdSetup
 * Initializes the device by performing one-time setup and reading device
 * capabilities and features. */
__EXTERN
OsStatus_t
MsdSetup(
    _In_ MsdDevice_t *Device);

/* MsdReadSectors
 * Read a given amount of sectors (bytes/sector-size) from the MSD. */
__EXTERN
OsStatus_t
MsdReadSectors(
    _In_ MsdDevice_t *Device,
    _In_ uint64_t SectorStart, 
    _In_ uintptr_t BufferAddress,
    _In_ size_t BufferLength,
    _Out_ size_t *BytesRead);

/* MsdWriteSectors
 * Write a given amount of sectors (bytes/sector-size) to the MSD. */
__EXTERN
OsStatus_t
MsdWriteSectors(
    _In_ MsdDevice_t *Device,
    _In_ uint64_t SectorStart, 
    _In_ uintptr_t BufferAddress,
    _In_ size_t BufferLength,
    _Out_ size_t *BytesWritten);

#endif // !_USB_MSD_H_
