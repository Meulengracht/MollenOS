/**
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * File Manager Service
 * - Handles all file related services and disk services
 */

#define __TRACE

#include <ddk/utils.h>
#include <ds/guid.h>
#include <vfs/gpt.h>
#include <vfs/storage.h>
#include <stdlib.h>
#include <string.h>

static guid_t g_emptyGuid = GUID_EMPTY;

OsStatus_t
GptEnumeratePartitionTable(
        _In_ FileSystemStorage_t* storage,
        _In_ GptHeader_t*         gptHeader,
        _In_ UUId_t               bufferHandle,
        _In_ void*                buffer)
{
    size_t     partitionTableSectorCount;
    size_t     sectorsRead;
    OsStatus_t osStatus;
    TRACE("GptEnumeratePartitionTable()");

    // No partitions on this disk, skip parse!
    if (!gptHeader->PartitionCount) {
        TRACE("GptEnumeratePartitionTable no partitions present on disk");
        return OsSuccess;
    }

    // Calculate the number of sectors we need to parse
    partitionTableSectorCount = (gptHeader->PartitionCount * gptHeader->PartitionEntrySize);
    partitionTableSectorCount /= storage->storage.descriptor.SectorSize;
    partitionTableSectorCount++;
    TRACE("GptEnumeratePartitionTable table sector size=%u", partitionTableSectorCount);

    // Start out by reading the gpt-header to detect whether there is a valid GPT table
    while (partitionTableSectorCount) {
        GptPartitionEntry_t* entry;

        osStatus = VfsStorageReadHelper(storage, bufferHandle,
                                        gptHeader->PartitionTableLBA,
                                        1, &sectorsRead);
        if (osStatus != OsSuccess) {
            return OsError;
        }

        entry = (GptPartitionEntry_t*)buffer;
        for (size_t i = 0; i < storage->storage.descriptor.SectorSize; i += gptHeader->PartitionEntrySize, entry++) {
            guid_t   typeGuid;
            guid_t   uniqueId;
            uint64_t sectorCount;

            // detect end of table, empty type guid
            guid_parse_raw(&typeGuid, &entry->PartitionTypeGUID[0]);
            if (!guid_cmp(&typeGuid, &g_emptyGuid)) {
                goto parse_done;
            }

            guid_parse_raw(&uniqueId, &entry->PartitionGUID[0]);
            sectorCount = (entry->EndLBA - entry->StartLBA) + 1;
            VfsStorageRegisterFileSystem(
                    storage, entry->StartLBA,
                    sectorCount, 0,
                    &typeGuid,
                    &uniqueId
            );
        }

        partitionTableSectorCount--;
    }

parse_done:
    return OsSuccess;
}

OsStatus_t
GptValidateHeader(
        _In_ GptHeader_t* gptHeader)
{
    TRACE("GptValidateHeader()");

    // Check for matching signature, probably the most important for this to determine
    // whether the GPT is present
    if (memcmp(&gptHeader->Signature[0], GPT_SIGNATURE, 8) != 0) {
        return OsDoesNotExist;
    }

    if (gptHeader->Revision != GPT_REVISION || gptHeader->HeaderSize < 92 ||
        gptHeader->Reserved != 0) {
        WARNING("GptValidateHeader header data was corrupt");
        WARNING("GptValidateHeader revision=0x%x, headerSize=%u, reserved=%u",
                gptHeader->Revision, gptHeader->HeaderSize, gptHeader->Reserved);
        return OsError;
    }

    // Perform CRC check of header
    // @todo

    return OsSuccess;
}

OsStatus_t
GptEnumerate(
        _In_ FileSystemStorage_t* storage,
        _In_ UUId_t               bufferHandle,
        _In_ void*                buffer)
{
    GptHeader_t* gpt;
    size_t       sectorsRead;
    OsStatus_t   osStatus;

    TRACE("GptEnumerate()");

    // Start out by reading the gpt-header to detect whether there is a valid GPT table
    osStatus = VfsStorageReadHelper(storage, bufferHandle, 1, 1, &sectorsRead);
    if (osStatus != OsSuccess) {
        return OsError;
    }

    // Allocate a buffer where we can store a copy of the mbr
    // it might be overwritten by recursion here
    gpt = (GptHeader_t *)malloc(sizeof(GptHeader_t));
    if (!gpt) {
        return OsOutOfMemory;
    }
    memcpy(gpt, buffer, sizeof(GptHeader_t));

    osStatus = GptValidateHeader(gpt);
    if (osStatus == OsSuccess) {
        osStatus = GptEnumeratePartitionTable(storage, gpt, bufferHandle, buffer);
    }
    free(gpt);
	return osStatus;
}
