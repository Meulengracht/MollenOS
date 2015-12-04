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
* MollenOS MCore - Disk Device Descriptor
*/
#ifndef _MCORE_DEVICE_DISK_H_
#define _MCORE_DEVICE_DISK_H_

/* Includes */
#include <stdint.h>

/* Storage Device */
#pragma pack(push, 1)
typedef struct _MCoreStorageDevice
{
	/* Disk Stats */
	uint64_t SectorCount;
	uint32_t SectorsPerCylinder;
	uint32_t AlignedAccess;
	uint32_t SectorSize;

	/* Disk Data */
	void *DiskData;

	/* Functions */
	int(*Read)(void *Data, uint64_t LBA, void *Buffer, uint32_t BufferLength);
	int(*Write)(void *Data, uint64_t LBA, void *Buffer, uint32_t BufferLength);

} MCoreStorageDevice_t;
#pragma pack(pop)

#endif //!_MCORE_DEVICE_DISK_H_