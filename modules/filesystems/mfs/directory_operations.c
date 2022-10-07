/**
 * Copyright 2022, Philip Meulengracht
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
 */

//#define __TRACE

#include <ddk/utils.h>
#include <fs/common.h>
#include <string.h>
#include "mfs.h"

static void __ConvertEntry(FileRecord_t* record, struct VFSStat* stat)
{
    stat->Name  = mstr_new_u8((const char*)&record->Name[0]);
    stat->Owner = 0; // TODO not supported by MFS
    stat->Size  = record->Size;
    MfsFileRecordFlagsToVfsFlags(record, &stat->Flags, &stat->Permissions);
}

oserr_t
FsReadFromDirectory(
        _In_  FileSystemMFS_t* mfs,
        _In_  MFSEntry_t*      entry,
        _In_  void*            buffer,
        _In_  size_t           bufferOffset,
        _In_  size_t           unitCount,
        _Out_ size_t*          unitsRead)
{
    oserr_t          osStatus    = OsOK;
    size_t           bytesToRead = unitCount;
    uint64_t         position    = entry->Position;
    struct VFSStat*  currentEntry = (struct VFSStat*)((uint8_t*)buffer + bufferOffset);

    TRACE("FsReadFromDirectory(entry=%ms, position=%u, count=%u)",
          entry->Base.Name, LODWORD(position), LODWORD(unitCount));

    // Indicate zero bytes read to start with
    *unitsRead = 0;

    // Readjust the stored position since it's stored in units of DIRENT, however we
    // iterate in units of MfsRecords
    position /= sizeof(struct VFSStat);
    position *= sizeof(FileRecord_t);

    TRACE(" > dma: fpos %u, bytes-total %u, offset %u", LODWORD(position), bytesToRead, bufferOffset);
    TRACE(" > dma: databucket-pos %u, databucket-len %u, databucket-bound %u",
          LODWORD(handle->DataBucketPosition), LODWORD(handle->DataBucketLength),
          LODWORD(handle->BucketByteBoundary));
    TRACE(" > sec %u, count %u, offset %u", LODWORD(MFS_GETSECTOR(mfs, handle->DataBucketPosition)),
          LODWORD(MFS_SECTORCOUNT(mfs, handle->DataBucketLength)), LODWORD(position - handle->BucketByteBoundary));

    while (bytesToRead > sizeof(struct VFSStat)) {
        uint64_t sector       = MFS_GETSECTOR(mfs, entry->DataBucketPosition);
        size_t   sectorCount  = MFS_SECTORCOUNT(mfs, entry->DataBucketLength);
        size_t   bucketOffset = position - entry->BucketByteBoundary;
        uint8_t* transferBuffer = (uint8_t*)mfs->TransferBuffer.buffer;
        size_t   bucketSize = sectorCount * mfs->SectorSize;
        size_t   sectorsRead;
        TRACE("read_metrics:: sector=%u, sectorCount=%u, bucketOffset=%u, bucketSize=%u",
              LODWORD(sector), sectorCount, bucketOffset, bucketSize);

        if (bucketSize > bucketOffset) {
            // The code here is simple because we assume we can fit entire bucket at any time
            osStatus = FSStorageRead(
                    &mfs->Storage,
                    mfs->TransferBuffer.handle,
                    0,
                    &(UInteger64_t) { .QuadPart = sector },
                    sectorCount,
                    &sectorsRead
            );
            if (osStatus != OsOK) {
                ERROR("Failed to read sector");
                break;
            }

            // Which position are we in?
            for (FileRecord_t* fileRecord = (FileRecord_t*)(transferBuffer + bucketOffset);
                    (bucketSize > bucketOffset) && bytesToRead;
                    fileRecord++, bucketOffset += sizeof(FileRecord_t), position += sizeof(FileRecord_t)) {
                if (fileRecord->Flags & MFS_FILERECORD_INUSE) {
                    TRACE("Gathering entry %s", &fileRecord->Name[0]);
                    __ConvertEntry(fileRecord, currentEntry);
                    bytesToRead -= sizeof(struct VFSStat);
                    currentEntry++;
                }
            }
        }
        
        // Do we need to switch bucket?
        // We do if the position we have read to equals end of bucket
        if (position == (entry->BucketByteBoundary + bucketSize)) {
            TRACE("read_metrics::position %u, limit %u", LODWORD(position),
                LODWORD(handle->BucketByteBoundary + bucketSize));
            osStatus = MfsSwitchToNextBucketLink(mfs, entry,
                                                 mfs->SectorsPerBucket * mfs->SectorSize);
            if (osStatus != OsOK) {
                if (osStatus == OsNotExists) {
                    osStatus = OsOK;
                }
                break;
            }
        }
    }
    
    // Readjust the position to the current position, but it has to be in units
    // of DIRENT instead of MfsRecords, and then readjust again for the number of
    // bytes read, since they are added to position in the vfs layer
    position        /= sizeof(FileRecord_t);
    position        *= sizeof(struct VFSStat);
    entry->Position = position - (unitCount - bytesToRead);
    
    *unitsRead = (unitCount - bytesToRead);
    return osStatus;
}

// TODO this is wrong
oserr_t
FsSeekInDirectory(
        _In_ FileSystemMFS_t* mfs,
        _In_ MFSEntry_t*      entry,
        _In_ uint64_t         absolutePosition)
{
    uint64_t actualPosition = absolutePosition * sizeof(struct VFSStat);
    size_t   initialBucketMax;

    // Trace
    TRACE("FsSeekInDirectory(entry=%ms, position=%u)",
          entry->Base.Name, LODWORD(absolutePosition));

    // Sanitize seeking bounds
    if (entry->ActualSize == 0) {
        return OsInvalidParameters;
    }

    // Step 1, if the new position is in
    // initial bucket, we need to do no actual seeking
    initialBucketMax = (entry->StartLength * (mfs->SectorsPerBucket * mfs->SectorSize));
    if (absolutePosition < initialBucketMax) {
        entry->DataBucketPosition = entry->StartBucket;
        entry->DataBucketLength   = entry->StartLength;
        entry->BucketByteBoundary = 0;
    }
    else {
        // Step 2. We might still get out easy
        // if we are setting a new position that's within the current bucket
        uint64_t OldBucketLow, OldBucketHigh;

        // Calculate bucket boundaries
        OldBucketLow  = entry->BucketByteBoundary;
        OldBucketHigh = OldBucketLow + (entry->DataBucketLength
                                        * (mfs->SectorsPerBucket * mfs->SectorSize));

        // If we are seeking inside the same bucket no need
        // to do anything else
        if (absolutePosition >= OldBucketLow && absolutePosition < OldBucketHigh) {
            // Same bucket
        }
        else {
            // We need to figure out which bucket the position is in
            uint64_t    PositionBoundLow   = 0;
            uint64_t    PositionBoundHigh  = initialBucketMax;
            MapRecord_t Link;

            // Start at the file-bucket
            uint32_t BucketPtr = entry->StartBucket;
            while (1) {
                // Check if we reached correct bucket
                if (absolutePosition >= PositionBoundLow
                    && absolutePosition < (PositionBoundLow + PositionBoundHigh)) {
                    entry->BucketByteBoundary = PositionBoundLow;
                    break;
                }

                // Get link
                if (MfsGetBucketLink(mfs, BucketPtr, &Link) != OsOK) {
                    ERROR("Failed to get link for bucket %u", BucketPtr);
                    return OsDeviceError;
                }

                // If we do reach end of chain, something went terribly wrong
                if (Link.Link == MFS_ENDOFCHAIN) {
                    ERROR("Reached end of chain during seek");
                    return OsInvalidParameters;
                }
                BucketPtr = Link.Link;

                // Get length of link
                if (MfsGetBucketLink(mfs, BucketPtr, &Link) != OsOK) {
                    ERROR("Failed to get length for bucket %u", BucketPtr);
                    return OsDeviceError;
                }
                PositionBoundLow    += PositionBoundHigh;
                PositionBoundHigh   = (Link.Length *
                    (mfs->SectorsPerBucket * mfs->SectorSize));
            }

            // Update bucket pointer
            if (BucketPtr != MFS_ENDOFCHAIN) {
                entry->DataBucketPosition = BucketPtr;
            }
        }
    }
    
    // Update the new position since everything went ok
    entry->Position = actualPosition;
    return OsOK;
}
