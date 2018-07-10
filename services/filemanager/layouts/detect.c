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
 * MollenOS - File Manager Service
 * - Handles all file related services and disk services
 */
#define __TRACE

#include <os/utils.h>
#include "../include/vfs.h"
#include "../include/gpt.h"
#include "../include/mbr.h"

/* DiskDetectFileSystem
 * Detectes the kind of filesystem at the given absolute sector 
 * with the given sector count. It then loads the correct driver
 * and installs it */
OsStatus_t DiskDetectFileSystem(FileSystemDisk_t *Disk,
	DmaBuffer_t *Buffer, uint64_t Sector, uint64_t SectorCount)
{
	// Variables
	MasterBootRecord_t *Mbr = NULL;
	FileSystemType_t Type = FSUnknown;

	// Trace
	TRACE("DiskDetectFileSystem(Sector %u, Count %u)",
		LODWORD(Sector), LODWORD(SectorCount));

	// Make sure the MBR is loaded
	if (StorageRead(Disk->Driver, Disk->Device, Sector, GetBufferDma(Buffer), 1) != OsSuccess) {
		return OsError;
	}

	// Instantiate the mbr-pointer
	Mbr = (MasterBootRecord_t*)GetBufferDataPointer(Buffer);

	// Ok - we do some basic signature checks 
	// MFS - "MFS1" 
	// NTFS - "NTFS" 
	// exFAT - "EXFAT" 
	// FAT - "FATXX"
	if (!strncmp((const char*)&Mbr->BootCode[3], "MFS1", 4)) {
		Type = FSMFS;
	}
	else if (!strncmp((const char*)&Mbr->BootCode[3], "NTFS", 4)) {
		Type = FSNTFS;
	}
	else if (!strncmp((const char*)&Mbr->BootCode[3], "EXFAT", 5)) {
		Type = FSEXFAT;
	}
	else if (!strncmp((const char*)&Mbr->BootCode[0x36], "FAT12", 5)
		|| !strncmp((const char*)&Mbr->BootCode[0x36], "FAT16", 5)
		|| !strncmp((const char*)&Mbr->BootCode[0x52], "FAT32", 5)) {
		Type = FSFAT;
	}
	else {
		// The following needs processing in other sectors to be determined
		//TODO
		//HPFS
		//EXT
		//HFS
	}

	// Sanitize the type
	if (Type != FSUnknown) {
		return DiskRegisterFileSystem(Disk, Sector, SectorCount, Type);
	}
	else {
		return OsError;
	}
}

/* DiskDetectLayout
 * Detects the kind of layout on the disk, be it
 * MBR or GPT layout, if there is no layout it returns
 * OsError to indicate the entire disk is a FS */
OsStatus_t DiskDetectLayout(FileSystemDisk_t *Disk)
{
	// Variables
	DmaBuffer_t *Buffer = NULL;
	GptHeader_t *Gpt = NULL;
	OsStatus_t Result;

	// Trace
	TRACE("DiskDetectLayout(SectorSize %u)",
        Disk->Descriptor.SectorSize);

	// Allocate a generic transfer buffer for disk operations
	// on the given disk, we need it to parse the disk
	Buffer = CreateBuffer(UUID_INVALID, Disk->Descriptor.SectorSize);

	// In order to detect the schema that is used
	// for the disk - we can easily just read sector LBA 1
	// and look for the GPT signature
	if (StorageRead(Disk->Driver, Disk->Device, 1, GetBufferDma(Buffer), 1) != OsSuccess) {
		DestroyBuffer(Buffer);
		return OsError;
	}

	// Initiate the gpt pointer directly from the buffer-object
	// to avoid doing double allocates when its not needed
	Gpt = (GptHeader_t*)GetBufferDataPointer(Buffer);

	// Check the GPT signature if it matches 
	// - If it doesn't match, it can only be a MBR disk
	if (!strncmp((const char*)&Gpt->Signature[0], GPT_SIGNATURE, 8)) {
		Result = GptEnumerate(Disk, Buffer);
	}
	else {
		Result = MbrEnumerate(Disk, Buffer);
	}

	// Cleanup buffer
	DestroyBuffer(Buffer);
	return Result;
}
