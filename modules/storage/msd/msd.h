/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS USB Core MSD Driver
*/

#ifndef _USB_MSD_H_
#define _USB_MSD_H_

/* Includes */
#include <os/osdefs.h>
#include "../scsi/scsi.h"

/* Subclasses */
#define USB_MSD_SUBCLASS_RBC			0x01
#define USB_MSD_SUBCLASS_ATAPI			0x02
#define USB_MSD_SUBCLASS_FLOPPY			0x04
#define USB_MSD_SUBCLASS_SCSI			0x06
#define USB_MSD_SUBCLASS_LSD_FS			0x07	/* This means we have to negotiate access before using SCSI */
#define USB_MSD_SUBCLASS_IEEE_1667		0x08

/* Protocols */
#define USB_MSD_PROTOCOL_CBI			0x00	/* Control/Bulk/Interrupt Transport (With Command Completion Interrupt) */
#define USB_MSD_PROTOCOL_CBI_NOINT		0x01	/* Control/Bulk/Interrupt Transport (With NO Command Completion Interrupt) */
#define USB_MSD_PROTOCOL_BULK_ONLY		0x50	/* Bulk Only Transfers */
#define USB_MSD_PROTOCOL_UAS			0x62	/* UAS */

/* bRequest Types */
#define USB_MSD_REQUEST_ADSC			0x00 /* Accept Device-Specific Command */
#define USB_MSD_REQUEST_GET_REQUESTS	0xFC
#define USB_MSD_REQUEST_PUT_REQUESTS	0xFD
#define USB_MSD_REQUEST_GET_MAX_LUN		0xFE
#define USB_MSD_REQUEST_RESET			0xFF /* Bulk Only MSD */

/* Structures */
#pragma pack(push, 1)
typedef struct _MsdCommandBlockWrap
{
	/* Cbw Signature 
	 * Must Contain 0x43425355 (little-endian) */
	uint32_t Signature;

	/* Cbw Tag */
	uint32_t Tag;

	/* Data Transfer Length */
	uint32_t DataLen;

	/* Flags */
	uint8_t Flags;

	/* LUN - Bits 0:3 */
	uint8_t Lun;

	/* Length of this - Bits 0:4 */
	uint8_t Length;

	/* Data */
	uint8_t CommandBytes[16];

} MsdCommandBlockWrap_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _MsdCommandBlockWrapUFI
{
	/* Data */
	uint8_t CommandBytes[12];

} MsdCommandBlockWrapUFI_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _MsdCommandStatusWrap
{
	/* Csw Signature
	* Must Contain 0x53425355 (little-endian) */
	uint32_t Signature;

	/* Cbw Tag */
	uint32_t Tag;

	/* Data Residue 
	 * The difference in data transfered */
	uint32_t DataResidue;

	/* Status */
	uint8_t Status;

} MsdCommandStatusWrap_t;
#pragma pack(pop)

#define USB_MSD_CSW_OK				0x0
#define USB_MSD_CSW_FAIL			0x1
#define USB_MSD_CSW_PHASE_ERROR		0x2

/* Msd Type */
typedef enum _MsdDeviceType
{
	FloppyDrive,
	DiskDrive,
	HardDrive

} MsdDeviceType_t;

#pragma pack(push, 1)
typedef struct _MsdDevice
{
	/* Id's */
	int Interface;
	DevId_t DeviceId; 

	/* Msd Information */
	MsdDeviceType_t Type;
	int IsReady;
	int IsExtended;
	size_t LUNCount;
	size_t SectorSize;

	/* Disk Stats */
	size_t SectorsPerCylinder;
	int AlignedAccess;

	/* Usb Data */
	UsbHcDevice_t *UsbDevice;
	UsbHcEndpoint_t *EpIn;
	UsbHcEndpoint_t *EpOut;
	UsbHcEndpoint_t *EpInterrupt;

} MsdDevice_t;
#pragma pack(pop)

/* Structure Definitions */
#define X86_USB_MSD_CBW_SIGNATURE		0x43425355
#define X86_USB_MSD_CSW_OK_SIGNATURE	0x53425355
#define X86_USB_MSD_TAG_SIGNATURE		0xB00B1E00
#define X86_USB_MSD_CBW_LUN(Lun)		(Lun & 0x7)
#define X86_USB_MSD_CBW_LEN(Len)		(Len & 0xF)
#define X86_USB_MSD_CBW_IN				0x80
#define X86_USB_MSD_CBW_OUT				0x0

#endif // !X86_USB_MSD_H_
