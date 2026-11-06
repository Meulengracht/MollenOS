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

#ifndef _USB_MSD_H_
#define _USB_MSD_H_

#include <os/osdefs.h>
#include <os/types/device.h>
#include "../scsi/scsi.h"
#include <ds/list.h>

#include <ddk/devices/usb.h>
#include <ddk/storage.h>

/* USB mass storage subclass codes. */
#define MSD_SUBCLASS_RBC			    0x01    // Reduced Block Command Set
#define MSD_SUBCLASS_ATAPI			    0x02
#define MSD_SUBCLASS_FLOPPY			    0x04
#define MSD_SUBCLASS_SCSI			    0x06
#define MSD_SUBCLASS_LSD_FS			    0x07	// This means we have to negotiate access before using SCSI
#define MSD_SUBCLASS_IEEE_1667		    0x08

/* USB mass storage transport protocols. */
#define MSD_PROTOCOL_CBI			    0x00	// Control/Bulk/Interrupt Transport (With Command Completion Interrupt)
#define MSD_PROTOCOL_CB		            0x01	// Control/Bulk/Interrupt Transport (With NO Command Completion Interrupt)
#define MSD_PROTOCOL_BULK_ONLY		    0x50	// Bulk Only Transfers
#define MSD_PROTOCOL_UAS			    0x62	// UAS

/* USB mass storage bRequest values. */
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

/* Command block signature. */
#define MSD_CBW_SIGNATURE		        0x43425355

/* Logical unit number in the low four bits. */
#define MSD_CBW_LUN(Lun)		        (Lun & 0xF)

/* Command length in the low five bits. */
#define MSD_CBW_LEN(Length)		        (Length & 0x1F)

/* Command data direction. */
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

/* Command status signature. */
#define MSD_CSW_OK_SIGNATURE	        0x53425355

/* Command completion status. */
#define MSD_CSW_OK				        0x0
#define MSD_CSW_FAIL			        0x1
#define MSD_CSW_PHASE_ERROR		        0x2

typedef enum MsdDeviceType {
    TypeUnknown,
    TypeFloppy,
	TypeDiskDrive,
	TypeHardDrive,
    TypeCount
} MsdDeviceType_t;

typedef enum MsdProtocolType {
    ProtocolUnknown,
    ProtocolCB,
	ProtocolCBI,
	ProtocolBulk,
    ProtocolCount
} MsdProtocolType_t;

typedef struct MSDDevice MSDDevice_t;
typedef struct MSDOperations {
    oserr_t (*Initialize)(MSDDevice_t*);
    oserr_t (*SendCommand)(MSDDevice_t*, uint8_t, uint64_t, size_t, enum USBTransferCode*);
    oserr_t (*ReadData)(MSDDevice_t*, uuid_t, size_t, size_t, enum USBTransferCode*, size_t*);
    oserr_t (*WriteData)(MSDDevice_t*, uuid_t, size_t, size_t, enum USBTransferCode*, size_t*);
    oserr_t (*GetStatus)(MSDDevice_t*, enum USBTransferCode*);
} MSDOperations_t;

typedef struct MSDDevice {
    UsbDevice_t*            Device;
    element_t               Header;
    StorageDescriptor_t     Descriptor;
    MsdDeviceType_t         Type;
    MsdProtocolType_t       Protocol;
    const MSDOperations_t*  Operations;

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
} MSDDevice_t;

/**
 * @brief Creates and initializes a USB mass storage device.
 *
 * @param usbDevice USB device with its interface and endpoint descriptors.
 * @return The initialized device, or NULL on failure.
 */
extern MSDDevice_t*
MSDDeviceCreate(
    _In_ UsbDevice_t *usbDevice);

/**
 * @brief Unregisters the storage device and frees its buffers and device data.
 *
 * @param msdDevice Device to destroy.
 * @return OS_EOK on success.
 */
extern oserr_t
MSDDeviceDestroy(
        _In_ MSDDevice_t *msdDevice);

/**
 * @brief Selects the transport operations and checks the required endpoints.
 *
 * @param device Device with its USB interface configuration loaded.
 * @return OS_EOK on success, or an error if the protocol cannot be used.
 */
extern oserr_t
MSDDeviceInitialize(
        _In_ MSDDevice_t *device);

/**
 * @brief Waits for the disk to become ready and reads its capabilities.
 *
 * @param device Device with its transport operations and command buffers ready.
 * @return OS_EOK on success, or an error from device setup.
 */
extern oserr_t
MSDDeviceStart(
        _In_ MSDDevice_t *device);

extern MSDDevice_t*
MsdDeviceGet(
        _In_ uuid_t deviceId);

#endif // !_USB_MSD_H_
