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
VFSStorageDeriveFileSystemType(
        _In_  struct VFSStorage* storage,
        _In_  uuid_t             bufferHandle,
        _In_  void*              buffer,
        _In_  UInteger64_t*      sector,
        _Out_ const char**       fsHintOut)
{
    char*               fsHint = NULL;
    MasterBootRecord_t* mbr;
    size_t              sectorsRead;
    oserr_t             status;

    TRACE("VFSStorageDeriveFileSystemType(sector=%u)", sector->u.LowPart);

    // Make sure the MBR is loaded
    status = storage->Operations.Read(storage, bufferHandle, 0, sector, 1, &sectorsRead);
    if (status != OsOK) {
        return status;
    }

    // Ok - we do some basic signature checks
    // MFS - "MFS1"
    // NTFS - "NTFS"
    // exFAT - "EXFAT"
    // FAT - "FATXX"
    mbr = buffer;
    if (!strncmp((const char*)&mbr->BootCode[3], "MFS1", 4)) {
        fsHint = "mfs";
    }
    else if (!strncmp((const char*)&mbr->BootCode[3], "NTFS", 4)) {
        fsHint = "ntfs";
    }
    else if (!strncmp((const char*)&mbr->BootCode[3], "EXFAT", 5)) {
        fsHint = "exfat";
    }
    else if (!strncmp((const char*)&mbr->BootCode[0x36], "FAT12", 5)
             || !strncmp((const char*)&mbr->BootCode[0x36], "FAT16", 5)
             || !strncmp((const char*)&mbr->BootCode[0x52], "FAT32", 5)) {
        fsHint = "fat";
    }
    else {
        WARNING("Unknown filesystem detected");
        // The following needs processing in other sectors to be determined
        //TODO
        //HPFS
        //EXT
        //HFS
    }

    if (fsHint == NULL) {
        return OsError;
    }
    *fsHintOut = fsHint;
    return OsOK;
}

oserr_t
VFSStorageDetectFileSystem(
        _In_ struct VFSStorage* storage,
        _In_ uuid_t             bufferHandle,
        _In_ void*              buffer,
        _In_ UInteger64_t*      sector)
{
    const char* fsHint;
	oserr_t     oserr;

	TRACE("VFSStorageDetectFileSystem(sector=%u)", sector->u.LowPart);

    oserr = VFSStorageDeriveFileSystemType(storage, bufferHandle, buffer, sector, &fsHint);
    if (oserr != OsOK) {
        return oserr;
    }

    return VFSStorageRegisterAndSetupPartition(
            storage, 0, sector,
            &g_emptyGuid,
            fsHint,
            &g_emptyGuid,
            UUID_INVALID,
            NULL
    );
}

oserr_t
VFSStorageParse(
        _In_ struct VFSStorage* storage)
{
	struct dma_buffer_info dmaInfo;
	struct dma_attachment  dmaAttachment;
    oserr_t                oserr;

	TRACE("VFSStorageParse(SectorSize %u)", storage->Stats.SectorSize);

	// Allocate a generic transfer buffer for disk operations
	// on the given disk, we need it to parse the disk
	dmaInfo.name     = "disk_temp_buffer";
	dmaInfo.capacity = storage->Stats.SectorSize;
	dmaInfo.length   = storage->Stats.SectorSize;
	dmaInfo.flags    = 0;
    dmaInfo.type     = DMA_TYPE_DRIVER_32LOW;

    oserr = dma_create(&dmaInfo, &dmaAttachment);
	if (oserr != OsOK) {
		return oserr;
	}

    // Always check for GPT table first
    oserr = GptEnumerate(storage, dmaAttachment.handle, dmaAttachment.buffer);
    if (oserr == OsNotExists) {
        oserr = MbrEnumerate(storage, dmaAttachment.handle, dmaAttachment.buffer);
    }

	dma_attachment_unmap(&dmaAttachment);
	dma_detach(&dmaAttachment);
	return oserr;
}
