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
 * MollenOS - ATA Command Header
 * - Contains shared definitions for ATA commands that can
 *   be used by all storage drivers that implement the ATA protocol
 */

#ifndef _ATA_H_
#define _ATA_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>

// Status Definitions
#define ATA_STS_DEV_ERROR			0x1
#define ATA_STS_DEV_DRQ				0x08
#define ATA_STS_DEV_CDERR			0x10
#define ATA_STS_DEV_FAULT			0x20
#define ATA_STS_DEV_READY			0x40
#define ATA_STS_DEV_BUSY			0x80

// Error Definitions
#define ATA_ERR_DEV_CCTO			0x1
#define ATA_ERR_DEV_MED				0x1
#define ATA_ERR_DEV_ILI				0x1
#define ATA_ERR_DEV_EOM				0x2
#define ATA_ERR_DEV_ABORT			0x4
#define ATA_ERR_DEV_IDNF			0x10
#define ATA_ERR_DEV_UNC				0x40
#define ATA_ERR_DEV_WP				0x40
#define ATA_ERR_DEV_ICRC			0x80

/* The ATA Command Set 
 * As specified by the ATA8 Specification 
 * - It's incomplete as there are a lot */
typedef enum _ATACommandType
{
	AtaCFAEraseSectors				= 0xC0,
	AtaCFARequestErrorCodeExt		= 0x03,
	AtaPIOCFATranslateSector		= 0x87,
	AtaPIOCFAWriteMultiNoErase		= 0xCD,
	AtaPIOCFAWriteNoErase			= 0x38,
	AtaCheckMediaCardType			= 0xD1,
	AtaCheckPowerMode				= 0xE5,
	AtaConfigureStream				= 0x51,

	/* Device Configuration Overlay */
	AtaDCOFreezeLock				= 0xB1, /* Feature Code 0xC1 */
	AtaPIODCOIdentify				= 0xB1, /* Feature Code 0xC2 */
	AtaDCORestoreConfig				= 0xB1, /* Feature Code 0xC0 */
	AtaPIODCOSetConfig				= 0xB1, /* Feature Code 0xC3 */

	AtaDeviceReset					= 0x08,
	AtaPIODownloadMicrocode			= 0x92, 
	AtaExecuteDiagnostics			= 0x90,
	AtaFlushCache					= 0xE7,
	AtaFlushCacheExt				= 0xEA,
	AtaPIOIdentifyDevice			= 0xEC,
	AtaPIOIdentifyPacketDevice		= 0xA1,
	AtaIdleImmediately				= 0xE1,
	AtaNOP							= 0x00,
	AtaPacket						= 0xA0,
	AtaPIOReadBuffer				= 0xE4,
	AtaPIOWriteBuffer				= 0xE8,
	
	/* Dma Commands */
	AtaDMARead						= 0xC8,
	AtaDMAReadExt					= 0x25,
	AtaDMAReadQueued				= 0xC7,
	AtaDMAReadQueuedExt				= 0x26,
	AtaDMAWrite						= 0xCA,
	AtaDMAWriteExt					= 0x35,
	AtaDMAWriteExtFUA				= 0x3D,
	AtaDMAWriteQueued				= 0xCC,
	AtaDMAWriteQueuedExt			= 0x36,
	AtaDmaWriteQueuedExtFUA			= 0x3E,

	AtaPIOReadLogExt				= 0x2F,
	AtaPIOWriteLogExt				= 0x3F,
	AtaDMAReadLogExt				= 0x47,
	AtaDMAWriteLogExt				= 0x57,

	AtaPIOReadMultiple				= 0xC4,
	AtaPIOReadMultipleExt			= 0x29,
	AtaPIOWriteMultiple				= 0xC3,
	AtaPIOWriteMultipleExt			= 0x39,
	AtaPIOWriteMultipleExtFUA		= 0xCE,

	AtaReadNativeMaxAddr			= 0xF8,
	AtaReadNativeMaxAddrExt			= 0x27,

	/* Pio Commands */
	AtaPIORead						= 0x20,
	AtaPIOReadExt					= 0x24,
	AtaPIOWrite						= 0x30,
	AtaPIOWriteExt					= 0x34,

	AtaDMAReadStreamExt				= 0x2A,
	AtaDMAWriteStreamExt			= 0x3A,
	AtaPIOReadStreamExt				= 0x2B,
	AtaPIOWriteStreamExt			= 0x3B,
	AtaReadVerifySectors			= 0x40,
	AtaReadVerifySectorsExt			= 0x42,
	AtaPIOSecurityDisablePassword	= 0xF6,
	AtaSecurityErasePrepare			= 0xF3,
	AtaPIOSecurityEraseUnit			= 0xF4,
	AtaSecurityFreezeLock			= 0xF5,
	AtaPIOSecuritySetPassword		= 0xF1,
	AtaPIOSecurityUnlock			= 0xF2,
	AtaService						= 0xA2,
	AtaSetFeatures					= 0xEF,
	AtaSleep						= 0xE6,
	AtaStandby						= 0xE2,
	AtaStandbyImmediate				= 0xE0,

} ATACommandType_t;

/* This is the ATA Identify Command 
 * Structure data, as described in the
 * ATA specs. */
PACKED_TYPESTRUCT(ATAIdentify, {
	/* Flags 
	 * Nothing is ok excpet:
	 * Bit 15 -> (0) ATA Device, (1) Who knows */
	uint16_t Flags;

	/* Reserved or obsolete 
	 * Words 1 -> 9 */
	uint16_t Obsolete0[9];

	/* Serial Number (20 ASCII Chars)
	 * Length: 10 words, 20 bytes */
	uint8_t SerialNo[20];

	/* Retired or obsolete 
	 * Words 20 -> 22 */
	uint16_t Obsolete1[3];

	/* Firmware Revision (8 ASCII Chars)
	 * Length: 4 words, 8 bytes */
	uint8_t FWRevision[8];

	/* Model Number (40 ASCII Chars)
	 * Length: 20 words, 40 bytes */
	uint8_t ModelNo[40];

	/* RW Multiple information 
	 * Max number of logical sectors per DRQ */
	uint8_t RWMultiple;

	/* This is fixed value at 0x80 */
	uint8_t FixedValue;

	/* Trusted Computing Features */
	uint16_t TCFeatures;

	/* Retired part of Capabilities 1 */
	uint8_t Obsolete2;

	/* Capabilities 0
	 * Bit 0: DMA Supported
	 * Bit 1: LBA Supported 
	 * Bit 2: Don't use IORDY 
	 * Bit 3: (1) IORDY Supported, (0) Maybe Supported
	 * Bit 4: Reserved
	 * Bit 5: Standby Timer 
	 * Bit 6-7: Reserved */
	uint8_t Capabilities0;

	/* Capabilities 1 
	 * Contains totally not fun settings */
	uint16_t Capabilities1;

	/* Words 51-52: Obsolete */
	uint32_t Obsolete3;

	/* Describes which fields are valid data 
	 * in the Identify Structure */ 
	uint16_t ValidExtData;

	/* Obsolete AND i don't care
	 * Words 54 -> 58 */
	uint16_t Obsolete4[5];

	/* Current setting for number of logical sectors */
	uint16_t RWMultipleCurrent;

	/* Number of LBA28 sectors */
	uint32_t SectorCountLBA28;

	/* Obsolete AND i don't care 
	 * Words 62-79 */
	uint16_t Obsolete5[18];

	/* 80: Drive Revision 
	 * - Major */
	uint16_t MajorRevision;

	/* 81: Drive Revision
	 * - Minor */
	uint16_t MinorRevision;

	/* 82: Command Set supported 0 
	 * Bit 0: SMART Supported
	 * Bit 1: Security Mode Supported
	 * Bit 2: Obsolete
	 * Bit 3: Power Management Supported
	 * Bit 4: PACKET Supported
	 * Bit 5: Write Cache Supported
	 * Bit 6: Look-Ahead Supported
	 * Bit 7: Release Interrupt Supported
	 * Bit 8: SERVICE Interrupt Supported
	 * Bit 9: DEVICE-RESET Supported
	 * Bit 10: Host Protected Area Supported
	 * Bit 11: Obsolete
	 * Bit 12: WRITE BUFFER Supported
	 * Bit 13: READ BUFFER Supported
	 * Bit 14: NOP Supported
	 * Bit 15: Obsolete */
	uint16_t CommandSetSupport0;

	/* 83: Command Set supported 1 
	 * Bit 0: DOWNLOAD MICROCODE Supported
	 * Bit 1: READ/WRITE DMA QUEUED Supported
	 * Bit 2: CFA Supported
	 * Bit 3: Advanced Power Management Supported
	 * Bit 4: Obsolete
	 * Bit 5: Power-Up In Standby Supported
	 * Bit 6: SET FEATURES subcommand required to spin-up after power-up
	 * Bit 7: Ignore....
	 * Bit 8: SET MAX security extension Supported
	 * Bit 9: Automatic Acoustic Management Supported
	 * Bit 10: 48-bit Address Support
	 * Bit 11: Device Configuration Overlay Support
	 * Bit 12: FLUSH CACHE Supported
	 * Bit 13: FLUSH CACHE EXT Supported
	 * Bit 14: Shall be set to one
	 * Bit 15: Shall be set to zero */
	uint16_t CommandSetSupport1;

	/* 84: Command Set supported 2
	 * Bit 0: SMART error logging Supported
	 * Bit 1: SMART self-test Supported
	 * Bit 2: Media serial number Supported
	 * Bit 3: Media Card Pass Through Command Supported
	 * Bit 4: Streaming feature Supported
	 * Bit 5: General Purpose Logging Supported
	 * Bit 6: WRITE DMA FUA EXT and WRITE MULTIPLE FUA EXT Supported
	 * Bit 7: WRITE DMA QUEUED FUA EXT Supported
	 * Bit 8: 64-bit World wide name Supported
	 * Bit 9: Obsolete
	 * Bit 10: Obsolete
	 * Bit 11: Reserved
	 * Bit 12: Reserved
	 * Bit 13: IDLE IMMEDIATE with UNLOAD FEATURE Supported
	 * Bit 14: Shall be set to one
	 * Bit 15: Shall be set to zero */
	uint16_t CommandSetSupport2;

	/* 85: CommandSetEnabled 0, 1, 2 
	 * They have exact same structure as 
	 * the commandsetsupport above */
	uint16_t CommandSetEnabled0;
	uint16_t CommandSetEnabled1;
	uint16_t CommandSetEnabled2;

	/* 88: Ultra DMA Mode 
	 * Support / Enabled Status */
	uint8_t UltraDMASupport;
	uint8_t UltraDMAStatus;

	/* 89: Timings, master password, 
	 * hardware reset results 
	 * streaming information 
	 * Words 89 -> 99 */
	uint16_t Obsolete6[11];

	/* 100: Number of LBA48 sectors 
	 * This value is 0, use LBA28 */
	uint64_t SectorCountLBA48;

	/* 104: PIO Streaming Transfer Time */
	uint16_t StreamingTransferTimePIO;

	/* 105: Reserved */
	uint16_t Obsolete7;

	/* 106: Sector Size 
	 * Bits 0-3: logical sectors per physical sector 
	 * Bit   12: (1) Logical Sector is larger than 512 bytes (256 words)
	 * Bit   13: (1) There is multiple logical sectors per physical 
	 * Bit   14: Shall be set to one
	 * Bit   15: Cleared to zero */
	uint16_t SectorSize;

	/* 107: Obsolete AND i don't care 
	 * Words 107-116 */
	uint16_t Obsolete8[10];

	/* 117: Words per logical sector 
	 * This * 2 = bytes per sector */
	uint32_t WordsPerLogicalSector;

	/* 119: Obsolete AND i don't care 
	 * Words 119-255 */
	uint16_t Obsolete9[255 - 119];

	/* Signature + Checksum */
	uint8_t Signature;
	uint8_t Checksum;

	/* 209 -> Alignment of logical blocks within a larger physical block */

});

#endif //!_ATA_H_
