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
 * File Manager Service
 * - Handles all file related services and disk services
 */
#define __TRACE

#include <ddk/utils.h>
#include "../include/vfs.h"
#include "../include/gpt.h"
#include "../include/mbr.h"

OsStatus_t
DiskDetectFileSystem(FileSystemDisk_t *Disk,
	UUId_t BufferHandle, void* Buffer, 
	uint64_t Sector, uint64_t SectorCount)
{
	MasterBootRecord_t* Mbr;
	FileSystemType_t    Type = FSUnknown;
	size_t              SectorsRead;

	// Trace
	TRACE("DiskDetectFileSystem(Sector %u, Count %u)",
		LODWORD(Sector), LODWORD(SectorCount));

	// Make sure the MBR is loaded
	if (StorageRead(Disk->Driver, Disk->Device, Sector, 
			BufferHandle, 1, &SectorsRead) != OsSuccess) {
		return OsError;
	}

	// Ok - we do some basic signature checks 
	// MFS - "MFS1" 
	// NTFS - "NTFS" 
	// exFAT - "EXFAT" 
	// FAT - "FATXX"
	Mbr = (MasterBootRecord_t*)Buffer;
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
        WARNING("Unknown filesystem detected");
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

OsStatus_t
DiskDetectLayout(
	_In_ FileSystemDisk_t* Disk)
{
	UUId_t       BufferHandle;
	void*        Buffer;
	GptHeader_t* Gpt;
	OsStatus_t   Result;
	size_t       SectorsRead;
	OsStatus_t   Status;

	TRACE("DiskDetectLayout(SectorSize %u)", Disk->Descriptor.SectorSize);

	// Allocate a generic transfer buffer for disk operations
	// on the given disk, we need it to parse the disk
	Status = MemoryShare(Disk->Descriptor.SectorSize, Disk->Descriptor.SectorSize,
		&Buffer, &BufferHandle);
	if (Status != OsSuccess) {
		return Status;
	}
	
	// In order to detect the schema that is used
	// for the disk - we can easily just read sector LBA 1
	// and look for the GPT signature
	if (StorageRead(Disk->Driver, Disk->Device, 1, 
			BufferHandle, 1, &SectorsRead) != OsSuccess) {
		MemoryFree(Buffer, Disk->Descriptor.SectorSize);
		MemoryUnshare(BufferHandle);
		return OsError;
	}
	
	// Check the GPT signature if it matches 
	// - If it doesn't match, it can only be a MBR disk
	Gpt = (GptHeader_t*)Buffer;
	if (!strncmp((const char*)&Gpt->Signature[0], GPT_SIGNATURE, 8)) {
		Result = GptEnumerate(Disk, BufferHandle, Buffer);
	}
	else {
		Result = MbrEnumerate(Disk, BufferHandle, Buffer);
	}

	MemoryFree(Buffer, Disk->Descriptor.SectorSize);
	MemoryUnshare(BufferHandle);
	return Result;
}
