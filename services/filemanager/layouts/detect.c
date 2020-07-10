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
#include <internal/_ipc.h>
#include <os/dmabuf.h>
#include "../include/vfs.h"
#include "../include/gpt.h"
#include "../include/mbr.h"
#include <string.h>

static OsStatus_t
ReadStorage(
	_In_  FileSystemDisk_t* storage,
	_In_  UUId_t            bufferHandle,
	_In_  uint64_t          sector,
	_In_  size_t            sectorCount,
	_Out_ size_t*           sectorsRead)
{
	struct vali_link_message msg  = VALI_MSG_INIT_HANDLE(storage->Driver);
	OsStatus_t               status;
	
	ctt_storage_transfer(GetGrachtClient(), &msg.base, storage->Device,
			__STORAGE_OPERATION_READ, LODWORD(sector), HIDWORD(sector), 
			bufferHandle, 0, sectorCount);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GetGrachtBuffer(), GRACHT_WAIT_BLOCK);
	ctt_storage_transfer_result(GetGrachtClient(), &msg.base, &status, sectorsRead);
	return status;
}

OsStatus_t
DiskDetectFileSystem(
	_In_ FileSystemDisk_t* disk,
	_In_ UUId_t            bufferHandle,
	_In_ void*             buffer, 
	_In_ uint64_t          sector,
	_In_ uint64_t          sectorCount)
{
	FileSystemType_t         type = FSUnknown;
	MasterBootRecord_t*      mbr;
	size_t                   sectorsRead;
	OsStatus_t               status;

	// Trace
	TRACE("DiskDetectFileSystem(Sector %u, Count %u)",
		LODWORD(sector), LODWORD(sectorCount));

	// Make sure the MBR is loaded
	status = ReadStorage(disk, bufferHandle, sector, 1, &sectorsRead);
	if (status != OsSuccess) {
		return status;
	}

	// Ok - we do some basic signature checks 
	// MFS - "MFS1" 
	// NTFS - "NTFS" 
	// exFAT - "EXFAT" 
	// FAT - "FATXX"
	mbr = (MasterBootRecord_t*)buffer;
	if (!strncmp((const char*)&mbr->BootCode[3], "MFS1", 4)) {
		type = FSMFS;
	}
	else if (!strncmp((const char*)&mbr->BootCode[3], "NTFS", 4)) {
		type = FSNTFS;
	}
	else if (!strncmp((const char*)&mbr->BootCode[3], "EXFAT", 5)) {
		type = FSEXFAT;
	}
	else if (!strncmp((const char*)&mbr->BootCode[0x36], "FAT12", 5)
		|| !strncmp((const char*)&mbr->BootCode[0x36], "FAT16", 5)
		|| !strncmp((const char*)&mbr->BootCode[0x52], "FAT32", 5)) {
		type = FSFAT;
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
	if (type != FSUnknown) {
		return DiskRegisterFileSystem(disk, sector, sectorCount, type);
	}
	else {
		return OsError;
	}
}

OsStatus_t
DiskDetectLayout(
	_In_ FileSystemDisk_t* disk)
{
	GptHeader_t* gpt;
	size_t       sectorsRead;
	OsStatus_t   status;
	
	struct dma_buffer_info dmaInfo;
	struct dma_attachment  dmaAttachment;

	TRACE("DiskDetectLayout(SectorSize %u)", disk->Descriptor.SectorSize);

	// Allocate a generic transfer buffer for disk operations
	// on the given disk, we need it to parse the disk
	dmaInfo.name     = "disk_temp_buffer";
	dmaInfo.capacity = disk->Descriptor.SectorSize;
	dmaInfo.length   = disk->Descriptor.SectorSize;
	dmaInfo.flags    = 0;
	
	status = dma_create(&dmaInfo, &dmaAttachment);
	if (status != OsSuccess) {
		return status;
	}
	
	// In order to detect the schema that is used
	// for the disk - we can easily just read sector LBA 1
	// and look for the GPT signature
	status = ReadStorage(disk, dmaAttachment.handle, 1, 1, &sectorsRead);
	if (status != OsSuccess) {
		dma_attachment_unmap(&dmaAttachment);
		dma_detach(&dmaAttachment);
		return status;
	}
	
	// Check the GPT signature if it matches 
	// - If it doesn't match, it can only be a MBR disk
	gpt = (GptHeader_t*)dmaAttachment.buffer;
	if (!strncmp((const char*)&gpt->Signature[0], GPT_SIGNATURE, 8)) {
		status = GptEnumerate(disk, dmaAttachment.handle, dmaAttachment.buffer);
	}
	else {
		status = MbrEnumerate(disk, dmaAttachment.handle, dmaAttachment.buffer);
	}

	dma_attachment_unmap(&dmaAttachment);
	dma_detach(&dmaAttachment);
	return status;
}
