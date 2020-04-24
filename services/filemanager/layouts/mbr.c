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

#include <ddk/utils.h>
#include <internal/_ipc.h>
#include "../include/vfs.h"
#include "../include/mbr.h"
#include <stdlib.h>

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
	
	ctt_storage_transfer(GetGrachtClient(), &msg, storage->Device,
			__STORAGE_OPERATION_READ, LODWORD(sector), HIDWORD(sector), 
			bufferHandle, 0, sectorCount, &status, sectorsRead);
	gracht_vali_message_finish(&msg);
	return status;
}

OsStatus_t
mbrEnumeratePartitions(
    _In_ FileSystemDisk_t* disk,
    _In_ UUId_t            bufferHandle,
    _In_ void*             buffer,
    _In_ uint64_t          sector)
{
    MasterBootRecord_t* mbr;
    int                 partitionCount = 0;
    int                 i;
    size_t              sectorsRead;
    OsStatus_t          status;

    // Trace
    TRACE("mbrEnumeratePartitions(Sector %u)", LODWORD(sector));

    // Start out by reading the mbr to detect whether
    // or not there is a partition table
    status = ReadStorage(disk, bufferHandle, 0, 1, &sectorsRead);
    if (status != OsSuccess) {
        return OsError;
    }

    // Allocate a buffer where we can store a copy of the mbr 
    // it might be overwritten by recursion here
    mbr = (MasterBootRecord_t*)malloc(sizeof(MasterBootRecord_t));
    if (!mbr) {
        return OsOutOfMemory;
    }
    memcpy(mbr, buffer, sizeof(MasterBootRecord_t));

    // Now try to see if there is any valid data
    // in any of the partitions
    for (i = 0; i < 4; i++) {
        if (mbr->Partitions[i].Status == MBR_PARTITION_ACTIVE) {
            FileSystemType_t type = FSUnknown;
            partitionCount++;

            // Check extended partitions first 
            // 0x05 = CHS
            // 0x0F = LBA
            // 0xCF = LBA
            if (mbr->Partitions[i].Type == 0x05) {
            }
            else if (mbr->Partitions[i].Type == 0x0F || mbr->Partitions[i].Type == 0xCF) {
                mbrEnumeratePartitions(disk, bufferHandle, buffer,
                    sector + mbr->Partitions[i].LbaSector);
            }

            // GPT Formatted 
            // ??? Shouldn't happen ever we reach this
            // otherwise the GPT is invalid
            else if (mbr->Partitions[i].Type == 0xEE) {
                return OsError;
            }

            // Check MFS
            else if (mbr->Partitions[i].Type == 0x61) {
                type = FSMFS;
            }

            // Check FAT 
            // - Both FAT12, 16 and 32
            else if (mbr->Partitions[i].Type == 0x1            /* Fat12 */
                    || mbr->Partitions[i].Type == 0x4        /* Fat16 */
                    || mbr->Partitions[i].Type == 0x6        /* Fat16B */
                    || mbr->Partitions[i].Type == 0xB        /* FAT32 - CHS */
                    || mbr->Partitions[i].Type == 0xC) {    /* Fat32 - LBA */
                type = FSFAT;
            }

            // Check HFS
            else if (mbr->Partitions[i].Type == 0xAF) {
                type = FSHFS;
            }

            // exFat or NTFS or HPFS
            // This requires some extra checks to determine
            // exactly which fs it is
            else if (mbr->Partitions[i].Type == 0x7) {
                if (!strncmp((const char*)&mbr->BootCode[3], "EXFAT", 5)) {
                    type = FSEXFAT;
                }
                else if (!strncmp((const char*)&mbr->BootCode[3], "NTFS", 4)) {
                    type = FSNTFS;
                }
                else {
                    type = FSHPFS;
                }
            }

            // Register the FS
            DiskRegisterFileSystem(disk, sector + mbr->Partitions[i].LbaSector,
                mbr->Partitions[i].LbaSize, type);
        }
    }
    
    free(mbr);
    return partitionCount != 0 ? OsSuccess : OsError;
}

OsStatus_t
MbrEnumerate(
    _In_ FileSystemDisk_t* disk,
    _In_ UUId_t            bufferHandle,
    _In_ void*             buffer)
{
    TRACE("MbrEnumerate()");
    
    // First of all, we want to detect whether or 
    // not there is a partition table available
    // otherwise we treat the entire disk as one partition
    if (mbrEnumeratePartitions(disk, bufferHandle, buffer, 0) != OsSuccess) {
        return DiskDetectFileSystem(disk, bufferHandle, buffer, 0, disk->Descriptor.SectorCount);
    }
    else {
        return OsSuccess;
    }
}
