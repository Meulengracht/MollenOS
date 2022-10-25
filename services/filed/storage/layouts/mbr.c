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

struct __EnumerateContext {
    int PartitionIndex;
};

static oserr_t __EnumeratePartitions(
        struct VFSStorage* storage, struct __EnumerateContext* context,
        uuid_t bufferHandle, void* buffer, uint64_t sector);

static oserr_t __ParseEntry(
        _In_ struct VFSStorage*         storage,
        _In_ struct __EnumerateContext* context,
        _In_ uuid_t                     bufferHandle,
        _In_ void*                      buffer,
        _In_ uint64_t                   currentSector,
        _In_ const MasterBootRecord_t*  mbr,
        _In_ const MbrPartitionEntry_t* entry)
{
    char* fsHint = NULL;

    if (!IS_PARTITION_PRESENT(entry)) {
        return OS_ENOENT;
    }

    // Check extended partitions first
    // 0x05 = CHS
    // 0x0F = LBA
    // 0xCF = LBA
    if (entry->Type == 0x05) {

    } else if (entry->Type == 0x0F || entry->Type == 0xCF) {
        return __EnumeratePartitions(
                storage,
                context,
                bufferHandle,
                buffer,
                currentSector + entry->LbaSector
        );
    }
    // GPT Formatted
    // ??? Shouldn't happen ever we reach this
    // otherwise the GPT is invalid
    else if (entry->Type == 0xEE) {
        return OS_EINVALPARAMS;
    }

    // Check MFS
    else if (entry->Type == 0x61) {
        fsHint = "mfs";
    }

    // Check FAT
    // - Both FAT12, 16 and 32
    else if (entry->Type == 0x1          /* Fat12 */
             || entry->Type == 0x4       /* Fat16 */
             || entry->Type == 0x6       /* Fat16B */
             || entry->Type == 0xB       /* FAT32 - CHS */
             || entry->Type == 0xC) {    /* Fat32 - LBA */
        fsHint = "fat";
    }

    // Check HFS
    else if (entry->Type == 0xAF) {
        fsHint = "hfs";
    }

    // exFat or NTFS or HPFS
    // This requires some extra checks to determine
    // exactly which fs it is
    else if (entry->Type == 0x7) {
        const char* signature = (const char*)&mbr->BootCode[3];
        if (!strncmp(signature, "EXFAT", 5)) {
            fsHint = "exfat";
        }
        else if (!strncmp(signature, "NTFS", 4)) {
            fsHint = "ntfs";
        }
        else {
            fsHint = "hpfs";
        }
    }

    UInteger64_t startSector = {
            .QuadPart = currentSector + entry->LbaSector
    };
    return VFSStorageRegisterAndSetupPartition(
            storage,
            context->PartitionIndex++,
            &startSector,
            &g_emptyGuid,
            fsHint,
            &g_emptyGuid,
            UUID_INVALID,
            NULL
    );
}

static oserr_t __EnumeratePartitions(
        _In_ struct VFSStorage*         storage,
        _In_ struct __EnumerateContext* context,
        _In_ uuid_t                     bufferHandle,
        _In_ void*                      buffer,
        _In_ uint64_t                   sector)
{
    MasterBootRecord_t* mbr;
    int                 partitionCount = 0;
    int                 i;
    size_t              sectorsRead;
    oserr_t             oserr;

    TRACE("__EnumeratePartitions(Sector %u)", LODWORD(sector));

    // Start out by reading the mbr to detect whether there is a partition table

    oserr = storage->Operations.Read(
            storage, bufferHandle, 0,
            &(UInteger64_t) { .QuadPart = sector },
            1, &sectorsRead
    );
    if (oserr != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Allocate a buffer where we can store a copy of the mbr 
    // it might be overwritten by recursion here
    mbr = (MasterBootRecord_t*)malloc(sizeof(MasterBootRecord_t));
    if (!mbr) {
        return OS_EOOM;
    }
    memcpy(mbr, buffer, sizeof(MasterBootRecord_t));

    for (i = 0; i < MBR_PARTITION_COUNT; i++) {
        if (__ParseEntry(storage, context, bufferHandle, buffer, sector, mbr, &mbr->Partitions[i]) == OS_EOK) {
            partitionCount++;
        }
    }
    
    free(mbr);
    return partitionCount != 0 ? OS_EOK : OS_EUNKNOWN;
}

oserr_t
MbrEnumerate(
        _In_ struct VFSStorage* storage,
        _In_ uuid_t             bufferHandle,
        _In_ void*              buffer)
{
    struct __EnumerateContext context;
    oserr_t                   oserr;
    TRACE("MbrEnumerate()");

    context.PartitionIndex = 0;
    
    // First, we want to detect whether there is a partition table available
    // otherwise we treat the entire disk as one partition
    oserr = __EnumeratePartitions(storage, &context, bufferHandle, buffer, 0);
    if (oserr != OS_EOK) {
        return VFSStorageDetectFileSystem(storage, bufferHandle, buffer, 0);
    }
    return oserr;
}
