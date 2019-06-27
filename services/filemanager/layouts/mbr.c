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
#include "../include/vfs.h"
#include "../include/mbr.h"
#include <stdlib.h>

OsStatus_t
MbrEnumeratePartitions(
    _In_ FileSystemDisk_t* Disk,
    _In_ UUId_t            BufferHandle,
    _In_ void*             Buffer,
    _In_ uint64_t          Sector)
{
    MasterBootRecord_t* Mbr;
    int                 PartitionCount = 0;
    int                 i;
    size_t              SectorsRead;

    // Trace
    TRACE("MbrEnumeratePartitions(Sector %u)", LODWORD(Sector));

    // Start out by reading the mbr to detect whether
    // or not there is a partition table
    if (StorageTransfer(Disk->Device, Disk->Driver, __STORAGE_OPERATION_READ, Sector, 
            BufferHandle, 0, 1, &SectorsRead) != OsSuccess) {
        return OsError;
    }

    // Allocate a buffer where we can store a copy of the mbr 
    // it might be overwritten by recursion here
    Mbr = (MasterBootRecord_t*)malloc(sizeof(MasterBootRecord_t));
    if (!Mbr) {
        return OsOutOfMemory;
    }
    memcpy(Mbr, Buffer, sizeof(MasterBootRecord_t));

    // Now try to see if there is any valid data
    // in any of the partitions
    for (i = 0; i < 4; i++) {
        if (Mbr->Partitions[i].Status == MBR_PARTITION_ACTIVE) {
            FileSystemType_t Type = FSUnknown;
            PartitionCount++;

            // Check extended partitions first 
            // 0x05 = CHS
            // 0x0F = LBA
            // 0xCF = LBA
            if (Mbr->Partitions[i].Type == 0x05) {
            }
            else if (Mbr->Partitions[i].Type == 0x0F || Mbr->Partitions[i].Type == 0xCF) {
                MbrEnumeratePartitions(Disk, BufferHandle, Buffer, Sector + Mbr->Partitions[i].LbaSector);
            }

            // GPT Formatted 
            // ??? Shouldn't happen ever we reach this
            // otherwise the GPT is invalid
            else if (Mbr->Partitions[i].Type == 0xEE) {
                return OsError;
            }

            // Check MFS
            else if (Mbr->Partitions[i].Type == 0x61) {
                Type = FSMFS;
            }

            // Check FAT 
            // - Both FAT12, 16 and 32
            else if (Mbr->Partitions[i].Type == 0x1            /* Fat12 */
                    || Mbr->Partitions[i].Type == 0x4        /* Fat16 */
                    || Mbr->Partitions[i].Type == 0x6        /* Fat16B */
                    || Mbr->Partitions[i].Type == 0xB        /* FAT32 - CHS */
                    || Mbr->Partitions[i].Type == 0xC) {    /* Fat32 - LBA */
                Type = FSFAT;
            }

            // Check HFS
            else if (Mbr->Partitions[i].Type == 0xAF) {
                Type = FSHFS;
            }

            // exFat or NTFS or HPFS
            // This requires some extra checks to determine
            // exactly which fs it is
            else if (Mbr->Partitions[i].Type == 0x7) {
                if (!strncmp((const char*)&Mbr->BootCode[3], "EXFAT", 5)) {
                    Type = FSEXFAT;
                }
                else if (!strncmp((const char*)&Mbr->BootCode[3], "NTFS", 4)) {
                    Type = FSNTFS;
                }
                else {
                    Type = FSHPFS;
                }
            }

            // Register the FS
            DiskRegisterFileSystem(Disk, Sector + Mbr->Partitions[i].LbaSector,
                Mbr->Partitions[i].LbaSize, Type);
        }
    }
    free(Mbr);
    return PartitionCount != 0 ? OsSuccess : OsError;
}

OsStatus_t
MbrEnumerate(
    _In_ FileSystemDisk_t* Disk,
    _In_ UUId_t            BufferHandle,
    _In_ void*             Buffer)
{
    TRACE("MbrEnumerate()");

    // First of all, we want to detect whether or 
    // not there is a partition table available
    // otherwise we treat the entire disk as one partition
    if (MbrEnumeratePartitions(Disk, BufferHandle, Buffer, 0) != OsSuccess) {
        return DiskDetectFileSystem(Disk, BufferHandle, Buffer, 0, Disk->Descriptor.SectorCount);
    }
    else {
        return OsSuccess;
    }
}
