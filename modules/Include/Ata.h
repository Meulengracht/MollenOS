/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS MCore - Serial ATA Header
*/

#ifndef _ATA_H_
#define _ATA_H_

/* Includes */
#include <os/osdefs.h>

/* ATA Status and Error 
 * Bit Definitions */

/* ATA Status bits */
#define ATA_STS_DEV_ERROR			0x1
#define ATA_STS_DEV_DRQ				0x10
#define ATA_STS_DEV_DWE				0x10
#define ATA_STS_DEV_SERV			0x10
#define ATA_STS_DEV_STREAMERR		0x20
#define ATA_STS_DEV_FAULT			0x20
#define ATA_STS_DEV_READY			0x40
#define ATA_STS_DEV_BUSY			0x80

/* ATA Error bits */
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
	AtaPIOReadSectors				= 0x20,
	AtaPIOWriteSectors				= 0x30,
	AtaPIOWriteSectorsExt			= 0x34,
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

	/* Pio Reads */
	AtaPIORead						= 0x20,
	AtaPIOReadExt					= 0x24,

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

#endif //!_ATA_H_
