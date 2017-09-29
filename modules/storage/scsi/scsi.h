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
 * MollenOS - SCSI Header
 * - Contains shared definitions for SCSI that can
 *   be used by all storage drivers that implement the SCSI protocol
 */

#ifndef _SCSI_H_
#define _SCSI_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>

/* SCSI Command Definitions 
 * Contains generic magic constants and definitions */
#define SCSI_TEST_UNIT_READY            0x00
#define SCSI_REQUEST_SENSE              0x03
#define SCSI_FORMAT_UNIT                0x04
#define SCSI_READ_6                     0x08
#define SCSI_WRITE_6                    0x0A
#define SCSI_INQUIRY                    0x12
#define SCSI_START_STOP_UNIT            0x1B
#define SCSI_SET_MEDIUM_REMOVAL         0x1E
#define SCSI_GET_FORMAT_CAPS            0x23
#define SCSI_READ_CAPACITY              0x25
#define SCSI_READ                       0x28
#define SCSI_WRITE                      0x2A
#define SCSI_SEEK                       0x2B
#define SCSI_WRITE_AND_VERIFY           0x2E
#define SCSI_VERIFY                     0x2F
#define SCSI_SYNC_CACHE                 0x35
#define SCSI_WRITE_BUFFER               0x3B
#define SCSI_READ_BUFFER                0x3C
#define SCSI_READ_TOC                   0x43
#define SCSI_GET_CONFIGURATION          0x46
#define SCSI_EVENT_STATUS               0x4A
#define SCSI_READ_DISC_INFO             0x51
#define SCSI_READ_TRACK_INFO            0x52
#define SCSI_RESERVE_TRACK              0x53
#define SCSI_SEND_OPC_INFO              0x54
#define SCSI_MODE_SELECT                0x55
#define SCSI_REPAIR_TRACK               0x58
#define SCSI_MODE_SENSE                 0x5A
#define SCSI_CLOSE_TRACK_SESSION        0x5B
#define SCSI_READ_BUFFER_CAPS           0x5C
#define SCSI_SEND_CUE_SHEET             0x5D
#define SCSI_READ_16                    0x88
#define SCSI_WRITE_16                   0x8A
#define SCSI_READ_CAPACITY_16           0x9E
#define SCSI_REPORT_LUNS                0xA0
#define SCSI_BLANK                      0xA1
#define SCSI_SECURITY_PROTOCOL_IN       0xA2
#define SCSI_SEND_KEY                   0xA3
#define SCSI_REPORT_KEY                 0xA4
#define SCSI_LOAD_UNLOAD_MEDIUM         0xA6
#define SCSI_SET_READAHEAD              0xA7
#define SCSI_READ_12                    0xA8
#define SCSI_WRITE_12                   0xAA
#define SCSI_READ_SERIAL                0xAB
#define SCSI_GET_PERFORMANCE            0xAC
#define SCSI_READ_DISC_STRUCT           0xAD
#define SCSI_SECURITY_PROTOCOL_OUT      0xB5
#define SCSI_SET_STREAMING              0xB6
#define SCSI_READ_CD_MSF                0xB9
#define SCSI_SET_CD_SPEED               0xBB
#define SCSI_MECHANISM_STATUS           0xBD
#define SCSI_READ_CD                    0xBE
#define SCSI_SEND_DISC_STRUCT           0xBF

#pragma pack(push, 1)
typedef struct _ScsiInquiry
{
    /* Bits 0:4 - Peripheral Device Type 
     * Bits 5:7 - Peripheral Qualifier */
    uint8_t PeripheralInfo;

    /* Bit 7 - Removable Media */
    uint8_t Rmb;

    /* Version */
    uint8_t Version;

    /* Bits 0:3 - Response Data Format 
     * Bits 4 - Hearical Support 
     * Bits 5 - Normal ACA support */
    uint8_t RespDataFormat;

    /* Additional Length N-4 */
    uint8_t AdditionalLength;

    /* Bit 0 - Protection Support
     * Bit 3 - Third Party Support (Third Party Commands) 
     * Bit 4:5 - Target Port Group Support 
     * Bit 6 - Access Controls Coordinator 
     * Bit 7 - SCC Support
     * Bit 8 - Addr16
     * Bit 11 - Medium Changer 
     * Bit 12 - Multi Port Support
     * Bit 13 - VS
     * Bit 14 - Enclosure Services
     * Bit 15 - Basic Queue Support */
    uint16_t Flags;

    /* Vendor Identification
     * Data is Left Aligned */
    uint8_t VendorIdent[8];

    /* Product Identification 
    * Data is Left Aligned (Reversed) */
    uint8_t ProductIdent[16];

    /* Product Revision Level
     * Data is Left Aligned */
    uint8_t ProductRevLvl[4];

} ScsiInquiry_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _ScsiSense
{
    /* Response Code & Validity */
    uint8_t ResponseStatus;

    /* Not Used */
    uint8_t Obsolete;

    /* SenseFlags 
     * 0:3 - SenseKey */
    uint8_t Flags;

    /* Information */
    uint8_t Information[4];

    /* Additional Length n - 7 */
    uint8_t AdditionalLength;

    /* Command Specific Information */
    uint8_t CmdInformation[4];

    /* Additional Sense Code */
    uint8_t AdditionalSenseCode;

    /* Additional Sense Qualifier */
    uint8_t AdditionalSenseQualifier;

    /* Field Replacable Unit Code */
    uint8_t FieldUnitCode;

    /* Extra Data */
    uint8_t ExtraData[3];

} ScsiSense_t;
#pragma pack(pop)

#define SCSI_SENSE_VALID            0x80

/* ScsiExtendedCaps
 * Extended capabilites structure, only used in case the device supports larger
 * spaces than 4gb storage. */
PACKED_TYPESTRUCT(ScsiExtendedCaps, {
    uint64_t SectorCount;
    uint32_t SectorSize;
    uint32_t Reserved[5];
});

#endif //!_SCSI_H_
