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

#include <ddk/utils.h>
#include <fs/common.h>
#include <os/types/file.h>
#include "mfs.h"
#include <stdlib.h>
#include <string.h>

static oserr_t
__UpdateMasterRecords(
        _In_ FileSystemMFS_t* mfs)
{
    size_t  sectorsTransferred;
    oserr_t oserr;

    TRACE("__UpdateMasterRecords()");

    memset(mfs->TransferBuffer.buffer, 0, mfs->SectorSize);
    memcpy(mfs->TransferBuffer.buffer, &mfs->MasterRecord, sizeof(MasterRecord_t));

    // Flush the secondary copy first, so we can detect failures
    oserr = FSStorageWrite(
            &mfs->Storage,
            mfs->TransferBuffer.handle,
            0,
            &(UInteger64_t) { .QuadPart = mfs->MasterRecordMirrorSector},
            1,
            &sectorsTransferred
    );
    if (oserr != OsOK) {
        ERROR("Failed to write (secondary) master-record to disk");
        return OsError;
    }

    // Flush the primary copy
    oserr = FSStorageWrite(
            &mfs->Storage,
            mfs->TransferBuffer.handle,
            0,
            &(UInteger64_t) { .QuadPart = mfs->MasterRecordSector},
            1,
            &sectorsTransferred
    );
    if (oserr != OsOK) {
        ERROR("Failed to write (primary) master-record to disk");
    }
    return oserr;
}

oserr_t
MfsZeroBucket(
        _In_ FileSystemMFS_t* mfs,
        _In_ uint32_t         bucket,
        _In_ size_t           count)
{
    size_t i;
    size_t sectorsTransferred;

    TRACE("MfsZeroBucket(Bucket %u, Count %u)", bucket, count);

    memset(mfs->TransferBuffer.buffer, 0, mfs->TransferBuffer.length);
    for (i = 0; i < count; i++) {
        uint64_t sector = MFS_GETSECTOR(mfs, bucket + i);
        oserr_t  oserr;

        oserr = FSStorageWrite(
                &mfs->Storage,
                mfs->TransferBuffer.handle,
                0,
                &(UInteger64_t) { .QuadPart = sector },
                mfs->SectorsPerBucket,
                &sectorsTransferred
        );
        if (oserr != OsOK) {
            ERROR("MfsZeroBucket failed to write bucket to disk");
            return oserr;
        }
    }
    return OsOK;
}

oserr_t
MfsGetBucketLink(
        _In_  FileSystemMFS_t* mfs,
        _In_  uint32_t         bucket,
        _Out_ MapRecord_t*     link)
{
    TRACE("MfsGetBucketLink(Bucket %u)", bucket);
    if (bucket >= mfs->BucketsInMap) {
        return OsInvalidParameters;
    }

    link->Link   = mfs->BucketMap[(bucket * 2)];
    link->Length = mfs->BucketMap[(bucket * 2) + 1];
    TRACE("... link %u, length %u", link->Link, link->Length);
    return OsOK;
}

oserr_t
MfsSetBucketLink(
        _In_ FileSystemMFS_t* mfs,
        _In_ uint32_t         bucket,
        _In_ MapRecord_t*     link,
        _In_ int              updateLength)
{
    uint8_t* bufferOffset;
    size_t   sectorOffset;
    size_t   sectorsTransferred;
    oserr_t  oserr;

    TRACE("MfsSetBucketLink(Bucket %u, Link %u)", bucket, link->Link);

    // Update in-memory map first
    mfs->BucketMap[(bucket * 2)] = link->Link;
    if (updateLength) {
        mfs->BucketMap[(bucket * 2) + 1] = link->Length;
    }

    // Calculate which sector that is dirty now
    sectorOffset = bucket / mfs->BucketsPerSectorInMap;

    // Calculate offset into buffer
    bufferOffset = (uint8_t*)mfs->BucketMap;
    bufferOffset += (sectorOffset * mfs->SectorSize);

    // Copy a sector's worth of data into the buffer
    memcpy(mfs->TransferBuffer.buffer, bufferOffset, mfs->SectorSize);

    // Flush buffer to disk
    oserr = FSStorageWrite(
            &mfs->Storage,
            mfs->TransferBuffer.handle,
            0,
            &(UInteger64_t) { .QuadPart = mfs->MasterRecord.MapSector + sectorOffset },
            1,
            &sectorsTransferred
    );
    if (oserr != OsOK) {
        ERROR("Failed to update the given map-sector %u on disk",
              LODWORD(mfs->MasterRecord.MapSector + sectorOffset));
        return OsError;
    }
    return OsOK;
}

oserr_t
MfsAllocateBuckets(
        _In_ FileSystemMFS_t* mfs,
        _In_ size_t           bucketCount,
        _In_ MapRecord_t*     mapRecord)
{
    uint32_t    previousBucket = 0;
    uint32_t    bucket  = mfs->MasterRecord.FreeBucket;
    size_t      counter = bucketCount;
    MapRecord_t record;

    TRACE("MfsAllocateBuckets(FreeAt %u, Count %u)", bucket, bucketCount);

    mapRecord->Link   = mfs->MasterRecord.FreeBucket;
    mapRecord->Length = 0;

    // Do allocation in a for-loop as bucket-sizes
    // are variable, and thus we might need multiple
    // allocations to satisfy the demand
    while (counter > 0) {
        // Get next free bucket
        if (MfsGetBucketLink(mfs, bucket, &record) != OsOK) {
            ERROR("Failed to retrieve link for bucket %u", bucket);
            return OsError;
        }

        // Bucket points to the free bucket index
        // Record.Link holds the link of <bucket>
        // Record.Length holds the length of <bucket>

        // We now have two cases, either the next block is
        // larger than the number of buckets we are asking for,
        // or it's smaller
        if (record.Length > counter) {
            // Ok, this block is larger than what we need.
            // We now need to first, update the free index to these values
            // Map[Bucket] = (Counter) | (MFS_ENDOFCHAIN)
            // Map[Bucket + Counter] = (Length - Counter) | Link
            MapRecord_t Update, Next;

            // Set update
            Update.Link     = MFS_ENDOFCHAIN;
            Update.Length   = counter;

            // Set next
            Next.Link       = record.Link;
            Next.Length     = record.Length - counter;

            // Make sure only to update out once, we just need
            // the initial size, not for each new allocation
            if (mapRecord->Length == 0) {
                mapRecord->Length = Update.Length;
            }

            // We have to adjust now, since we are taking
            // only a chunk of the available length
            // Map[Bucket] = (Counter) | (MFS_ENDOFCHAIN)
            // Map[Bucket + Counter] = (Length - Counter) | PreviousLink
            if (MfsSetBucketLink(mfs, bucket, &Update, 1) != OsOK &&
                MfsSetBucketLink(mfs, bucket + counter, &Next, 1) != OsOK) {
                ERROR("Failed to update link for bucket %u and %u",
                      bucket, bucket + counter);
                return OsError;
            }
            mfs->MasterRecord.FreeBucket = bucket + counter;
            return __UpdateMasterRecords(mfs);
        } else {
            // Ok, block is either exactly the size we need or less
            // than what we need

            // Make sure only to update out once, we just need
            // the initial size, not for each new allocation
            if (mapRecord->Length == 0) {
                mapRecord->Length = record.Length;
            }
            counter         -= record.Length;
            previousBucket = bucket;
            bucket         = record.Link;
        }
    }

    // If we reach here it was because we encountered a block
    // that was exactly the fit we needed. So we set FreeIndex to Bucket
    // We set Record.Link to ENDOFCHAIN. We leave size unchanged

    // We want to update the last bucket of the chain but not update the length
    record.Link = MFS_ENDOFCHAIN;

    // Update the previous bucket to MFS_ENDOFCHAIN
    if (MfsSetBucketLink(mfs, previousBucket, &record, 0) != OsOK) {
        ERROR("Failed to update link for bucket %u", previousBucket);
        return OsError;
    }

    // Update the master-record and we are done
    mfs->MasterRecord.FreeBucket = bucket;
    return __UpdateMasterRecords(mfs);
}

oserr_t
MfsFreeBuckets(
        _In_ FileSystemMFS_t* mfs,
        _In_ uint32_t         startBucket,
        _In_ uint32_t         startLength)
{
    uint32_t    previousBucket;
    MapRecord_t mapRecord;

    TRACE("MfsFreeBuckets(Bucket %u, Length %u)", startBucket, startLength);

    if (startLength == 0) {
        return OsError;
    }

    // Essentially there is two algorithms we can deploy here
    // The quick one - Which is just to add the allocated bucket list
    // to the free and set the last allocated to point to the first free
    // OR there is the slow one that makes sure that buckets are <in order> as
    // they get freed, and gets inserted or extended correctly. This will reduce
    // fragmentation by A LOT
    mapRecord.Link = startBucket;

    // Start by iterating to the last bucket
    previousBucket = MFS_ENDOFCHAIN;
    while (mapRecord.Link != MFS_ENDOFCHAIN) {
        previousBucket = mapRecord.Link;
        if (MfsGetBucketLink(mfs, mapRecord.Link, &mapRecord) != OsOK) {
            ERROR("Failed to retrieve the next bucket-link");
            return OsError;
        }
    }

    // If there was no allocated buckets to start with then do nothing
    if (previousBucket != MFS_ENDOFCHAIN) {
        mapRecord.Link = mfs->MasterRecord.FreeBucket;

        // Ok, so now update the pointer to free list
        if (MfsSetBucketLink(mfs, previousBucket, &mapRecord, 0)) {
            ERROR("Failed to update the next bucket-link");
            return OsError;
        }
        mfs->MasterRecord.FreeBucket = startBucket;
        return __UpdateMasterRecords(mfs);
    }
    return OsOK;
}
