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
* MollenOS MCore - MollenOS Module Manager
*/
#ifndef _RAMDISK_H_
#define _RAMDISK_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

/* Definitions */
#define RAMDISK_MAGIC			0x3144524D

#define RAMDISK_VERSION_1		0x01

#define RAMDISK_ARCH_X86_32		0x08
#define RAMDISK_ARCH_X86_64		0x10

#define RAMDISK_FILE			0x1
#define RAMDISK_DIRECTORY		0x2
#define RAMDISK_MODULE			0x4

/* Structures */
typedef struct _MCoreRamDiskHeader
{
	/* Magic */
	uint32_t Magic;

	/* Version */
	uint32_t Version;

	/* Architecture */
	uint32_t Architecture;

	/* File Count */
	uint32_t FileCount;

} MCoreRamDiskHeader_t;

typedef struct _MCoreRamDiskFileHeader
{
	/* UTF-8 FileName
	* Fixed-length */
	uint8_t Filename[64];

	/* File Type */
	uint32_t Type;

	/* Data Pointer */
	uint32_t DataOffset;

} MCoreRamDiskFileHeader_t;

typedef struct _MCoreRamDiskModuleHeader
{
	/* Module Name
	* Also UTF-8 */
	uint8_t ModuleName[64];

	/* Device Type */
	uint32_t DeviceType;

	/* Device SubType */
	uint32_t DeviceSubType;

	/* Module Length */
	uint32_t Length;

} MCoreRamDiskModuleHeader_t;

#endif //!_RAMDISK_H_