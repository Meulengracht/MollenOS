/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * File Manager Service
 * - Handles all file related services and disk services
 */
#define __TRACE

#include <ddk/utils.h>
#include <os/dmabuf.h>
#include <string.h>
#include <vfs/storage.h>
#include <vfs/gpt.h>
#include <vfs/mbr.h>

static guid_t g_emptyGuid = GUID_EMPTY;

oserr_t
VfsStorageDetectFileSystem(
        _In_ FileSystemStorage_t* storage,
        _In_ uuid_t               bufferHandle,
        _In_ void*                buffer,
        _In_ uint64_t             sector,
        _In_ uint64_t             sectorCount)
{
    enum FileSystemType type = FileSystemType_UNKNOWN;
	MasterBootRecord_t* mbr;
	size_t              sectorsRead;
	oserr_t          status;

	TRACE("VfsStorageDetectFileSystem(Sector %u, Count %u)",
		LODWORD(sector), LODWORD(sectorCount));

	// Make sure the MBR is loaded
	status = VfsStorageReadHelper(storage, bufferHandle, sector, 1, &sectorsRead);
	if (status != OsOK) {
		return status;
	}

	// Ok - we do some basic signature checks 
	// MFS - "MFS1" 
	// NTFS - "NTFS" 
	// exFAT - "EXFAT" 
	// FAT - "FATXX"
	mbr = (MasterBootRecord_t*)buffer;
	if (!strncmp((const char*)&mbr->BootCode[3], "MFS1", 4)) {
		type = FileSystemType_MFS;
	}
	else if (!strncmp((const char*)&mbr->BootCode[3], "NTFS", 4)) {
		type = FileSystemType_NTFS;
	}
	else if (!strncmp((const char*)&mbr->BootCode[3], "EXFAT", 5)) {
		type = FileSystemType_EXFAT;
	}
	else if (!strncmp((const char*)&mbr->BootCode[0x36], "FAT12", 5)
		|| !strncmp((const char*)&mbr->BootCode[0x36], "FAT16", 5)
		|| !strncmp((const char*)&mbr->BootCode[0x52], "FAT32", 5)) {
		type = FileSystemType_FAT;
	}
	else {
        WARNING("Unknown filesystem detected");
		// The following needs processing in other sectors to be determined
		//TODO
		//HPFS
		//EXT
		//HFS
	}

    if (type == FileSystemType_UNKNOWN) {
        return OsError;
    }

    return VfsStorageRegisterFileSystem(
            storage, sector,
            sectorCount, type,
            &g_emptyGuid,
            &g_emptyGuid
    );
}

oserr_t
VfsStorageParse(
	_In_ FileSystemStorage_t* fsStorage)
{
	struct dma_buffer_info dmaInfo;
	struct dma_attachment  dmaAttachment;
    oserr_t               osStatus;

	TRACE("VfsStorageParse(SectorSize %u)", fsStorage->Storage.SectorSize);

	// Allocate a generic transfer buffer for disk operations
	// on the given disk, we need it to parse the disk
	dmaInfo.name     = "disk_temp_buffer";
	dmaInfo.capacity = fsStorage->Storage.SectorSize;
	dmaInfo.length   = fsStorage->Storage.SectorSize;
	dmaInfo.flags    = 0;
    dmaInfo.type     = DMA_TYPE_DRIVER_32LOW;

    osStatus = dma_create(&dmaInfo, &dmaAttachment);
	if (osStatus != OsOK) {
		return osStatus;
	}

    // Always check for GPT table first
    osStatus = GptEnumerate(fsStorage, dmaAttachment.handle, dmaAttachment.buffer);
    if (osStatus == OsNotExists) {
        osStatus = MbrEnumerate(fsStorage, dmaAttachment.handle, dmaAttachment.buffer);
    }

	dma_attachment_unmap(&dmaAttachment);
	dma_detach(&dmaAttachment);
	return osStatus;
}