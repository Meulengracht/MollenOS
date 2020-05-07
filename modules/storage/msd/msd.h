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

#ifndef _USB_MSD_H_
#define _USB_MSD_H_

#include <os/osdefs.h>
#include "../scsi/scsi.h"
#include <ds/list.h>

#include <ddk/usbdevice.h>
#include <ddk/storage.h>

/* MSD Subclass Definitions 
 * Contains generic magic constants and definitions */
#define MSD_SUBCLASS_RBC			    0x01    // Reduced Block Command Set
#define MSD_SUBCLASS_ATAPI			    0x02
#define MSD_SUBCLASS_FLOPPY			    0x04
#define MSD_SUBCLASS_SCSI			    0x06
#define MSD_SUBCLASS_LSD_FS			    0x07	// This means we have to negotiate access before using SCSI
#define MSD_SUBCLASS_IEEE_1667		    0x08

/* MSD Protocol Definitions 
 * Contains generic magic constants and definitions */
#define MSD_PROTOCOL_CBI			    0x00	// Control/Bulk/Interrupt Transport (With Command Completion Interrupt)
#define MSD_PROTOCOL_CB		            0x01	// Control/Bulk/Interrupt Transport (With NO Command Completion Interrupt)
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

PACKED_TYPESTRUCT(MsdCommandBlock, {
	uint32_t Signature; //Must Contain 0x43425355 (little-endian)
	uint32_t Tag;
	uint32_t DataLength;
	uint8_t  Flags;
	uint8_t  Lun; // Bits 0-3
	uint8_t  Length; // Length of this structure, bits 0-4
	uint8_t  CommandBytes[16];
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

PACKED_TYPESTRUCT(MsdCommandBlockUFI, {
	uint8_t CommandBytes[12];
});

PACKED_TYPESTRUCT(MsdCommandStatus, {
	uint32_t Signature; // Must Contain 0x53425355 (little-endian)
	uint32_t Tag;
	uint32_t DataResidue; // The difference in data transfered
	uint8_t  Status;
});

/* MsdCommandStatus::Signature
 * Contains definitions and bitfield definitions for MsdCommandStatus::Signature */
#define MSD_CSW_OK_SIGNATURE	        0x53425355

/* MsdCommandStatus::Status
 * Contains definitions and bitfield definitions for MsdCommandStatus::Status */
#define MSD_CSW_OK				        0x0
#define MSD_CSW_FAIL			        0x1
#define MSD_CSW_PHASE_ERROR		        0x2

typedef enum _MsdDeviceType {
    TypeFloppy,
	TypeDiskDrive,
	TypeHardDrive,
    TypeCount
} MsdDeviceType_t;

typedef enum _MsdProtocolType {
    ProtocolUnknown,
    ProtocolCB,
	ProtocolCBI,
	ProtocolBulk,
    ProtocolCount
} MsdProtocolType_t;

typedef struct _MsdDevice MsdDevice_t;
typedef struct _MsdOperations {
    OsStatus_t          (*Initialize)(MsdDevice_t*);
    UsbTransferStatus_t (*SendCommand)(MsdDevice_t*, uint8_t, uint64_t, UUId_t, size_t, size_t);
    UsbTransferStatus_t (*ReadData)(MsdDevice_t*, UUId_t, size_t, size_t, size_t*);
    UsbTransferStatus_t (*WriteData)(MsdDevice_t*, UUId_t, size_t, size_t, size_t*);
    UsbTransferStatus_t (*GetStatus)(MsdDevice_t*);
} MsdOperations_t;

typedef struct _MsdDevice {
    UsbDevice_t         Base;
    StorageDescriptor_t Descriptor;
    element_t           Header;
    MsdDeviceType_t     Type;
    MsdProtocolType_t   Protocol;
    MsdOperations_t*    Operations;

	int IsReady;
	int IsExtended;
    int AlignedAccess;

    // Reusable buffers
    MsdCommandBlock_t*  CommandBlock;
    MsdCommandStatus_t* StatusBlock;
    
    uint8_t                    InterfaceId;
    usb_endpoint_descriptor_t* In;
	usb_endpoint_descriptor_t* Out;
	usb_endpoint_descriptor_t* Interrupt;
} MsdDevice_t;

/* MsdDeviceCreate
 * Initializes a new msd-device from the given usb-device */
__EXTERN MsdDevice_t*
MsdDeviceCreate(
    _In_ UsbDevice_t *UsbDevice);

/* MsdDeviceDestroy
 * Destroys an existing msd device instance and cleans up
 * any resources related to it */
__EXTERN OsStatus_t
MsdDeviceDestroy(
    _In_ MsdDevice_t *Device);

/* MsdDeviceInitialize 
 * Initializes and validates that the protocol has all neccessary
 * resources/endpoints/prerequisites for operation. */
__EXTERN OsStatus_t
MsdDeviceInitialize(
    _In_ MsdDevice_t *Device);

/* MsdDeviceStart
 * Initializes the device by performing one-time setup and reading device
 * capabilities and features. */
__EXTERN OsStatus_t
MsdDeviceStart(
    _In_ MsdDevice_t *Device);

__EXTERN MsdDevice_t*
MsdDeviceGet(
    _In_ UUId_t deviceId);
#endif // !_USB_MSD_H_
