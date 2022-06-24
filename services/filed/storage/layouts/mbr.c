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
 * MollenOS - File Manager Service
 * - Handles all file related services and disk services
 */
#define __TRACE

#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>
#include <vfs/mbr.h>
#include <vfs/storage.h>

#define MBR_PARTITION_COUNT 4
#define IS_PARTITION_PRESENT(part) ((part)->Status == 0x80 || ((part)->Status == 0 && (part)->Type != 0))

static guid_t g_emptyGuid = GUID_EMPTY;

OsStatus_t MbrEnumeratePartitions(
        _In_ FileSystemStorage_t* storage,
        _In_ UUId_t               bufferHandle,
        _In_ void*                buffer,
        _In_ uint64_t             sector);

static OsStatus_t ParsePartitionEntry(
        _In_ FileSystemStorage_t*       storage,
        _In_ UUId_t                     bufferHandle,
        _In_ void*                      buffer,
        _In_ uint64_t                   currentSector,
        _In_ const MasterBootRecord_t*  mbr,
        _In_ const MbrPartitionEntry_t* entry)
{
    enum FileSystemType type = FileSystemType_UNKNOWN;

    if (!IS_PARTITION_PRESENT(entry)) {
        return OsNotExists;
    }

    // Check extended partitions first
    // 0x05 = CHS
    // 0x0F = LBA
    // 0xCF = LBA
    if (entry->Type == 0x05) {
    }
    else if (entry->Type == 0x0F || entry->Type == 0xCF) {
        MbrEnumeratePartitions(storage, bufferHandle, buffer,
                               currentSector + entry->LbaSector);
    }
    // GPT Formatted
    // ??? Shouldn't happen ever we reach this
    // otherwise the GPT is invalid
    else if (entry->Type == 0xEE) {
        return OsInvalidParameters;
    }

    // Check MFS
    else if (entry->Type == 0x61) {
        type = FileSystemType_MFS;
    }

    // Check FAT
    // - Both FAT12, 16 and 32
    else if (entry->Type == 0x1          /* Fat12 */
             || entry->Type == 0x4       /* Fat16 */
             || entry->Type == 0x6       /* Fat16B */
             || entry->Type == 0xB       /* FAT32 - CHS */
             || entry->Type == 0xC) {    /* Fat32 - LBA */
        type = FileSystemType_FAT;
    }

    // Check HFS
    else if (entry->Type == 0xAF) {
        type = FileSystemType_HFS;
    }

    // exFat or NTFS or HPFS
    // This requires some extra checks to determine
    // exactly which fs it is
    else if (entry->Type == 0x7) {
        const char* signature = (const char*)&mbr->BootCode[3];
        if (!strncmp(signature, "EXFAT", 5)) {
            type = FileSystemType_EXFAT;
        }
        else if (!strncmp(signature, "NTFS", 4)) {
            type = FileSystemType_NTFS;
        }
        else {
            type = FileSystemType_HPFS;
        }
    }

    return VfsStorageRegisterFileSystem(storage,
                                 currentSector + entry->LbaSector,
                                 entry->LbaSize,
                                 type,
                                 &g_emptyGuid,
                                 &g_emptyGuid
    );
}

OsStatus_t
MbrEnumeratePartitions(
        _In_ FileSystemStorage_t* storage,
        _In_ UUId_t               bufferHandle,
        _In_ void*                buffer,
        _In_ uint64_t             sector)
{
    MasterBootRecord_t* mbr;
    int                 partitionCount = 0;
    int                 i;
    size_t              sectorsRead;
    OsStatus_t          status;

    TRACE("MbrEnumeratePartitions(Sector %u)", LODWORD(sector));

    // Start out by reading the mbr to detect whether there is a partition table
    status = VfsStorageReadHelper(storage, bufferHandle, sector, 1, &sectorsRead);
    if (status != OsOK) {
        return OsError;
    }

    // Allocate a buffer where we can store a copy of the mbr 
    // it might be overwritten by recursion here
    mbr = (MasterBootRecord_t*)malloc(sizeof(MasterBootRecord_t));
    if (!mbr) {
        return OsOutOfMemory;
    }
    memcpy(mbr, buffer, sizeof(MasterBootRecord_t));

    for (i = 0; i < MBR_PARTITION_COUNT; i++) {
        if (ParsePartitionEntry(storage, bufferHandle, buffer, sector, mbr, &mbr->Partitions[i]) == OsOK) {
            partitionCount++;
        }
    }
    
    free(mbr);
    return partitionCount != 0 ? OsOK : OsError;
}

OsStatus_t
MbrEnumerate(
        _In_ FileSystemStorage_t* storage,
        _In_ UUId_t               bufferHandle,
        _In_ void*                buffer)
{
    OsStatus_t osStatus;
    TRACE("MbrEnumerate()");
    
    // First, we want to detect whether there is a partition table available
    // otherwise we treat the entire disk as one partition
    osStatus = MbrEnumeratePartitions(storage, bufferHandle, buffer, 0);
    if (osStatus != OsOK) {
        return VfsStorageDetectFileSystem(storage, bufferHandle, buffer, 0, storage->storage.descriptor.SectorCount);
    }
    return osStatus;
}
