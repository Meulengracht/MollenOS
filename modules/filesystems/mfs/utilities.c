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
 * General File System (MFS) Driver
 *  - Contains the implementation of the MFS driver for mollenos
 */

//#define __TRACE

#include <ddk/utils.h>
#include <fs/common.h>
#include <os/types/file.h>
#include "mfs.h"
#include <stdlib.h>
#include <string.h>

oserr_t
MfsUpdateMasterRecord(
        _In_ FileSystemMFS_t* mfs)
{
    size_t  sectorsTransferred;
    oserr_t oserr;

    TRACE("MfsUpdateMasterRecord()");

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
MfsSwitchToNextBucketLink(
        _In_ FileSystemMFS_t* mfs,
        _In_ MFSEntry_t*      entry,
        _In_ size_t           bucketSizeBytes)
{
    MapRecord_t link;
    uint32_t    nextDataBucketPosition;

    // We have to lookup the link for current bucket
    if (MfsGetBucketLink(mfs, entry->DataBucketPosition, &link) != OsOK) {
        ERROR("Failed to get link for bucket %u", entry->DataBucketPosition);
        return OsDeviceError;
    }

    // Check for EOL
    if (link.Link == MFS_ENDOFCHAIN) {
        return OsNotExists;
    }
    nextDataBucketPosition = link.Link;

    // Lookup length of link
    if (MfsGetBucketLink(mfs, entry->DataBucketPosition, &link) != OsOK) {
        ERROR("Failed to get length for bucket %u", entry->DataBucketPosition);
        return OsDeviceError;
    }

    // Store length & Update bucket boundary
    entry->DataBucketPosition = nextDataBucketPosition;
    entry->DataBucketLength   = link.Length;
    entry->BucketByteBoundary += (link.Length * bucketSizeBytes);
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
    // are variable and thus we might need multiple
    // allocations to satisfy the demand
    while (counter > 0) {
        // Get next free bucket
        if (MfsGetBucketLink(mfs, bucket, &record) != OsOK) {
            ERROR("Failed to retrieve link for bucket %u", bucket);
            return OsError;
        }

        // Bucket points to the free bucket index
        // Record.Link holds the link of <Bucket>
        // Record.Length holds the length of <Bucket>

        // We now have two cases, either the next block is
        // larger than the number of buckets we are asking for
        // or it's smaller
        if (record.Length > counter) {
            // Ok, this block is larger than what we need
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
            return MfsUpdateMasterRecord(mfs);
        }
        else {
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
    return MfsUpdateMasterRecord(mfs);
}

/* MfsFreeBuckets
 * Frees an entire chain of buckets that has been allocated for a file-record */
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
        return MfsUpdateMasterRecord(mfs);
    }
    return OsOK;
}

/* MfsZeroBucket
 * Wipes the given bucket and count with zero values useful for clearing clusters of sectors */
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
        // Calculate the sector
        uint64_t AbsoluteSector = MFS_GETSECTOR(mfs, bucket + i);
        oserr_t  oserr;

        oserr = FSStorageWrite(
                &mfs->Storage,
                mfs->TransferBuffer.handle,
                0,
                &(UInteger64_t) { .QuadPart = AbsoluteSector },
                mfs->SectorsPerBucket,
                &sectorsTransferred
        );
        if (oserr != OsOK) {
            ERROR("Failed to write bucket to disk");
            return oserr;
        }
    }
    return OsOK;
}

unsigned int
MFSToNativeFlags(
    _In_ unsigned int flags)
{
    unsigned int nativeFlags = 0;
    TRACE("MFSToNativeFlags(flags=0x%x)", flags);

    if (flags & FILE_FLAG_DIRECTORY) {
        nativeFlags |= MFS_FILERECORD_DIRECTORY;
    } else if (flags & FILE_FLAG_LINK) {
        nativeFlags |= MFS_FILERECORD_LINK;
    }
    TRACE("MFSToNativeFlags returns 0x%x", nativeFlags)
    return nativeFlags;
}

void
MFSFromNativeFlags(
    _In_  FileRecord_t* fileRecord,
    _Out_ unsigned int* flags,
    _Out_ unsigned int* permissions)
{
    // Permissions are not really implemented
    *permissions = (FILE_PERMISSION_READ | FILE_PERMISSION_WRITE | FILE_PERMISSION_EXECUTE);
    *flags       = 0;

    if (fileRecord->Flags & MFS_FILERECORD_DIRECTORY) {
        *flags |= FILE_FLAG_DIRECTORY;
    } else if (fileRecord->Flags & MFS_FILERECORD_LINK) {
        *flags |= FILE_FLAG_LINK;
    }
}

void
MfsFileRecordToVfsFile(
        _In_ FileSystemMFS_t* mfs,
        _In_ FileRecord_t*    nativeEntry,
        _In_ MFSEntry_t*      mfsEntry)
{
    TRACE("MfsFileRecordToVfsFile()");

    // VfsEntry->Base.Descriptor.Id = ??
    mfsEntry->Name          = mstr_new_u8((const char*)&nativeEntry->Name[0]);
    mfsEntry->NativeFlags   = nativeEntry->Flags;
    mfsEntry->ActualSize    = nativeEntry->Size;
    mfsEntry->AllocatedSize = nativeEntry->AllocatedSize;
    mfsEntry->StartBucket   = nativeEntry->StartBucket;
    mfsEntry->StartLength   = nativeEntry->StartLength;

    // Convert flags to generic vfs flags and permissions
    MFSFromNativeFlags(nativeEntry,
                       &mfsEntry->Flags,
                       &mfsEntry->Permissions);

    // TODO Convert dates
    // VfsEntry->Base.DescriptorCreatedAt;
    // VfsEntry->Base.DescriptorModifiedAt;
    // VfsEntry->Base.DescriptorAccessedAt;
}

oserr_t
MfsUpdateRecord(
        _In_ FileSystemMFS_t* mfs,
        _In_ MFSEntry_t*      entry,
        _In_ int              action)
{
    oserr_t       oserr;
    FileRecord_t* record;
    size_t        sectorsTransferred;

    TRACE("MfsUpdateEntry(File %ms)", entry->Name);

    // Read the stored data bucket where the record is
    oserr = FSStorageRead(
            &mfs->Storage,
            mfs->TransferBuffer.handle,
            0,
            &(UInteger64_t) { .QuadPart = MFS_GETSECTOR(mfs, entry->DirectoryBucket) },
            MFS_SECTORCOUNT(mfs, entry->DirectoryLength),
            &sectorsTransferred
    );
    if (oserr != OsOK) {
        ERROR("MfsUpdateEntry Failed to read bucket %u", entry->DirectoryBucket);
        goto Cleanup;
    }

    record = (FileRecord_t*)((uint8_t*)mfs->TransferBuffer.buffer + (sizeof(FileRecord_t) * entry->DirectoryIndex));

    // We have two over-all cases here, as create/modify share
    // some code, and that is delete as the second. If we delete
    // we zero out the entry and set the status to deleted
    if (action == MFS_ACTION_DELETE) {
        memset((void*)record, 0, sizeof(FileRecord_t));
    } else {
        // Now we have two sub cases, but create just needs some
        // extra updates otherwise they share
        if (action == MFS_ACTION_CREATE) {
            char* entryName = mstr_u8(entry->Name);
            if (entryName == NULL) {
                return OsOutOfMemory;
            }

            memset(&record->Integrated[0], 0, 512);
            memset(&record->Name[0], 0, 300);
            memcpy(&record->Name[0], entryName, strlen(entryName));
            free(entryName);
        }

        // Update stats that are modifiable
        record->Flags       = entry->NativeFlags | MFS_FILERECORD_INUSE;
        record->StartBucket = entry->StartBucket;
        record->StartLength = entry->StartLength;

        // Update modified / accessed dates

        // Update sizes
        record->Size          = entry->ActualSize;
        record->AllocatedSize = entry->AllocatedSize;
    }
    
    // Write the bucket back to the disk
    oserr = FSStorageWrite(
            &mfs->Storage,
            mfs->TransferBuffer.handle,
            0,
            &(UInteger64_t) { .QuadPart = MFS_GETSECTOR(mfs, entry->DirectoryBucket) },
            MFS_SECTORCOUNT(mfs, entry->DirectoryLength),
            &sectorsTransferred
    );
    if (oserr != OsOK) {
        ERROR("MfsUpdateEntry Failed to update bucket %u", entry->DirectoryBucket);
    }

    // Cleanup and exit
Cleanup:
    return oserr;
}

/* MfsEnsureRecordSpace
 * Ensures that the given record has the space neccessary for the required data. */
oserr_t
MfsEnsureRecordSpace(
        _In_ FileSystemMFS_t* mfs,
        _In_ MFSEntry_t*      entry,
        _In_ uint64_t         spaceRequired)
{
    size_t bucketSizeBytes = mfs->SectorsPerBucket * mfs->SectorSize;
    TRACE("MfsEnsureRecordSpace(%u)", LODWORD(spaceRequired));

    if (spaceRequired > entry->AllocatedSize) {
        // Calculate the number of sectors, then number of buckets
        size_t      sectorCount = (size_t)(DIVUP((spaceRequired - entry->AllocatedSize), mfs->SectorSize));
        size_t      bucketCount = DIVUP(sectorCount, mfs->SectorsPerBucket);
        uint32_t    bucketPointer, previousBucketPointer;
        MapRecord_t iterator, link;

        // Perform the allocation of buckets
        if (MfsAllocateBuckets(mfs, bucketCount, &link) != OsOK) {
            ERROR("Failed to allocate %u buckets for file", bucketCount);
            return OsDeviceError;
        }

        // Now iterate to end
        bucketPointer         = entry->StartBucket;
        previousBucketPointer = MFS_ENDOFCHAIN;
        while (bucketPointer != MFS_ENDOFCHAIN) {
            previousBucketPointer = bucketPointer;
            if (MfsGetBucketLink(mfs, bucketPointer, &iterator) != OsOK) {
                ERROR("Failed to get link for bucket %u", bucketPointer);
                return OsDeviceError;
            }
            bucketPointer = iterator.Link;
        }

        // We have a special case if previous == MFS_ENDOFCHAIN
        if (previousBucketPointer == MFS_ENDOFCHAIN) {
            // This means file had nothing allocated
            entry->StartBucket = link.Link;
            entry->StartLength = link.Length;
        } else {
            if (MfsSetBucketLink(mfs, previousBucketPointer, &link, 1) != OsOK) {
                ERROR("Failed to set link for bucket %u", previousBucketPointer);
                return OsDeviceError;
            }
        }

        // Adjust the allocated-size of record
        entry->AllocatedSize += (bucketCount * bucketSizeBytes);
        entry->ActionOnClose  = MFS_ACTION_UPDATE;
    }
    return OsOK;
}
