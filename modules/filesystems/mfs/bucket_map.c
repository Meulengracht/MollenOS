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
#define __need_minmax
#include <ddk/utils.h>
#include <fs/common.h>
#include <os/types/file.h>
#include <os/shm.h>
#include "mfs.h"
#include <stdlib.h>
#include <string.h>

// TODO:
//  - Implement bucket map transactions
//     * BucketMapStartTransaction
//     * BucketMapCommitTransaction
//     * BucketMapAbortTransaction
//  - Abstract the storage medium, multiple mediums
//  - Expand buckets s to 64 bit.

static oserr_t
__UpdateMasterRecords(
        _In_ FileSystemMFS_t* mfs)
{
    size_t  sectorsTransferred;
    oserr_t oserr;
    void*   buffer = SHMBuffer(&mfs->TransferBuffer);
    TRACE("__UpdateMasterRecords()");

    memset(buffer, 0, mfs->SectorSize);
    memcpy(buffer, &mfs->MasterRecord, sizeof(MasterRecord_t));

    // Flush the secondary copy first, so we can detect failures
    oserr = FSStorageWrite(
            &mfs->Storage,
            mfs->TransferBuffer.ID,
            0,
            &(UInteger64_t) { .QuadPart = mfs->MasterRecordMirrorSector},
            1,
            &sectorsTransferred
    );
    if (oserr != OS_EOK) {
        ERROR("Failed to write (secondary) master-record to disk");
        return OS_EUNKNOWN;
    }

    // Flush the primary copy
    oserr = FSStorageWrite(
            &mfs->Storage,
            mfs->TransferBuffer.ID,
            0,
            &(UInteger64_t) { .QuadPart = mfs->MasterRecordSector},
            1,
            &sectorsTransferred
    );
    if (oserr != OS_EOK) {
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
    TRACE("MfsZeroBucket(bucket=%u, count=%u)", bucket, count);

    memset(SHMBuffer(&mfs->TransferBuffer), 0, SHMBufferLength(&mfs->TransferBuffer));
    for (i = 0; i < count; i++) {
        uint64_t sector = MFS_GETSECTOR(mfs, bucket + i);
        oserr_t  oserr;

        oserr = FSStorageWrite(
                &mfs->Storage,
                mfs->TransferBuffer.ID,
                0,
                &(UInteger64_t) { .QuadPart = sector },
                mfs->SectorsPerBucket,
                &sectorsTransferred
        );
        if (oserr != OS_EOK) {
            ERROR("MfsZeroBucket failed to write bucket to disk");
            return oserr;
        }
    }
    return OS_EOK;
}

oserr_t
MFSBucketMapGetLengthAndLink(
        _In_  FileSystemMFS_t* mfs,
        _In_  bucket_t         bucket,
        _Out_ MapRecord_t*     link)
{
    TRACE("MFSBucketMapGetLengthAndLink(bucket=%u)", bucket);
    if (bucket >= mfs->BucketsInMap) {
        return OS_EINVALPARAMS;
    }

    link->Link   = mfs->BucketMap[(bucket * 2)];
    link->Length = mfs->BucketMap[(bucket * 2) + 1];
    TRACE("... link %u, length %u", link->Link, link->Length);
    return OS_EOK;
}

oserr_t
MFSBucketMapSetLinkAndLength(
        _In_ FileSystemMFS_t* mfs,
        _In_ bucket_t         bucket,
        _In_ bucket_t         link,
        _In_ uint32_t         length,
        _In_ bool             updateLength)
{
    uint8_t* bufferOffset;
    size_t   sectorOffset;
    size_t   sectorsTransferred;
    oserr_t  oserr;
    TRACE("MFSBucketMapSetLinkAndLength(bucket=%u, link=%u, length=%u)",
          bucket, link, length);

    // Update in-memory map first
    mfs->BucketMap[(bucket * 2)] = link;
    if (updateLength) {
        mfs->BucketMap[(bucket * 2) + 1] = length;
    }

    // Calculate which sector that is dirty now
    sectorOffset = bucket / mfs->BucketsPerSectorInMap;

    // Calculate offset into buffer
    bufferOffset = (uint8_t*)mfs->BucketMap;
    bufferOffset += (sectorOffset * mfs->SectorSize);

    // Copy a sector's worth of data into the buffer
    memcpy(SHMBuffer(&mfs->TransferBuffer), bufferOffset, mfs->SectorSize);

    // Flush buffer to disk
    oserr = FSStorageWrite(
            &mfs->Storage,
            mfs->TransferBuffer.ID,
            0,
            &(UInteger64_t) { .QuadPart = mfs->MasterRecord.MapSector + sectorOffset },
            1,
            &sectorsTransferred
    );
    if (oserr != OS_EOK) {
        ERROR("Failed to update the given map-sector %u on disk",
              LODWORD(mfs->MasterRecord.MapSector + sectorOffset));
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

oserr_t
MFSBucketMapAllocate(
        _In_ FileSystemMFS_t* mfs,
        _In_ size_t           bucketCount,
        _In_ MapRecord_t*     allocatedRecord)
{
    bucket_t i;
    bucket_t previous;
    uint32_t bucketsLeft = bucketCount;

    // Initiate the allocated record
    allocatedRecord->Link = mfs->MasterRecord.FreeBucket;
    allocatedRecord->Length = 0;

    // Start allocating from the free list. If we are lucky then the length
    // of the first free bucket-chain will be long enough for our allocation.
    i = mfs->MasterRecord.FreeBucket;
    previous = MFS_ENDOFCHAIN;
    while (bucketsLeft) {
        MapRecord_t currentRecord;
        oserr_t     oserr;

        // Get length of i.
        oserr = MFSBucketMapGetLengthAndLink(mfs, i, &currentRecord);
        if (oserr != OS_EOK) {
            return oserr;
        }

        // Update the length of the allocated record
        if (!allocatedRecord->Length) {
            allocatedRecord->Length = MIN(bucketsLeft, currentRecord.Length);
        }

        // If 'i' is longer than the number of buckets we need to allocate, then we must
        // split up i.
        // i0 with the length of the number of buckets to allocate
        // i1 who starts from i0+number of buckets to allocate, with a length adjusted
        if (currentRecord.Length > bucketsLeft) {
            // Update the current record to reflect its new length and link
            oserr = MFSBucketMapSetLinkAndLength(
                    mfs,
                    i,
                    MFS_ENDOFCHAIN,
                    bucketsLeft,
                    true
            );
            if (oserr != OS_EOK) {
                // Ehh that's not good, in theory we should cancel the transaction
                return OS_EINCOMPLETE;
            }

            // Update the next record which will be our new free record
            oserr = MFSBucketMapSetLinkAndLength(
                    mfs,
                    i + bucketsLeft,
                    currentRecord.Link,
                    currentRecord.Length - bucketsLeft,
                    true
            );
            if (oserr != OS_EOK) {
                // Ehh that's not good, in theory we should cancel the transaction
                return OS_EINCOMPLETE;
            }

            // Manipulate with the current record so the code flow can continue
            currentRecord.Length = bucketsLeft;
            currentRecord.Link = i + bucketsLeft;
            bucketsLeft = 0;
        } else if (currentRecord.Length == bucketsLeft) {
            bucketsLeft = 0;

            // just relink the bucket
            oserr = MFSBucketMapSetLinkAndLength(mfs, i, MFS_ENDOFCHAIN, 0, false);
            if (oserr != OS_EOK) {
                return oserr;
            }
        } else {
            // The length is less, so we need to allocate this segment and move on
            // to the link
            if (currentRecord.Link == MFS_ENDOFCHAIN) {
                // Uh oh, out of disk space
                return OS_EINCOMPLETE;
            }
            bucketsLeft -= currentRecord.Length;
        }

        // If we are doing multiple allocations, then we have to make sure the chain
        // is correct by chaining them.
        if (previous != MFS_ENDOFCHAIN) {
            oserr = MFSBucketMapSetLinkAndLength(mfs, previous, i, 0, false);
            if (oserr != OS_EOK) {
                // Ehh that's not good, in theory we should cancel the transaction
                return OS_EINCOMPLETE;
            }
        }
        previous = i;
        i = currentRecord.Link;
    }

    // At this point we have done the allocation and 'i' will point
    // to the next free record
    mfs->MasterRecord.FreeBucket = i;
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
        return OS_EUNKNOWN;
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
        if (MFSBucketMapGetLengthAndLink(mfs, mapRecord.Link, &mapRecord) != OS_EOK) {
            ERROR("Failed to retrieve the next bucket-link");
            return OS_EUNKNOWN;
        }
    }

    // If there was no allocated buckets to start with then do nothing
    if (previousBucket != MFS_ENDOFCHAIN) {
        // Ok, so now update the pointer to free list
        oserr_t oserr = MFSBucketMapSetLinkAndLength(
                mfs,
                previousBucket,
                mfs->MasterRecord.FreeBucket,
                0,
                false
        );
        if (oserr != OS_EOK) {
            ERROR("Failed to update the next bucket-link");
            return oserr;
        }
        mfs->MasterRecord.FreeBucket = startBucket;
        return __UpdateMasterRecords(mfs);
    }
    return OS_EOK;
}
