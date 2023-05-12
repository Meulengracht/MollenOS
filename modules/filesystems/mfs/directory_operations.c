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
#include <os/shm.h>
#include <string.h>
#include "mfs.h"

static size_t
__WriteDirectoryEntry(
        _In_ FileRecord_t* record,
        _In_ uint8_t*      buffer,
        _In_ size_t        maxBytesToWrite)
{
    struct VFSDirectoryEntry* entry = (struct VFSDirectoryEntry*)buffer;
    char*                     stringData = (char*)buffer + sizeof(struct VFSDirectoryEntry);
    size_t                    bytesWritten = sizeof(struct VFSDirectoryEntry);

    // fill out the entry itself, then write string data, string data is always
    // zero-terminated
    entry->NameLength = strlen((const char*)&record->Name[0]) + 1;
    entry->LinkLength = 0; // TODO: add this

    // If we can't safely write all data, then abort
    if ((bytesWritten + entry->NameLength + entry->LinkLength) > maxBytesToWrite) {
        TRACE("skipping entry %s, not enough buffer room", &record->Name[0]);
        return 0;
    }

    entry->UserID = 0; // TODO: add this
    entry->GroupID = 0; // TODO: add this
    entry->Size = record->Size;
    entry->SizeOnDisk = record->AllocatedSize;
    MFSFromNativeFlags(
            record,
            &entry->Flags,
            &entry->Permissions
    );

    // TODO write timestamps

    // write out the string data
    if (entry->NameLength) {
        memcpy(stringData, (const void*)&record->Name[0], entry->NameLength);
        stringData += entry->NameLength;
    }
    if (entry->LinkLength) {
        memcpy(stringData, (const void*)&record->Integrated[0], entry->LinkLength);
    }
    return bytesWritten + entry->NameLength + entry->LinkLength;
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
    oserr_t  oserr       = OS_EOK;
    size_t   bytesToRead = unitCount;
    uint64_t position    = entry->Position * sizeof(FileRecord_t);
    uint8_t* bufferPointer = ((uint8_t*)buffer + bufferOffset);

    TRACE("FsReadFromDirectory(entry=%ms, position=%u, count=%u)",
          entry->Name, LODWORD(position), LODWORD(unitCount));

    // Indicate zero bytes read to start with
    *unitsRead = 0;

    // Guard against empty directories, we just return OS_EOK and
    // unitsRead=0
    if (entry->StartBucket == MFS_ENDOFCHAIN) {
        return OS_EOK;
    }

    TRACE(" > dma: fpos %u, bytes-total %u, offset %u", LODWORD(position), bytesToRead, bufferOffset);
    TRACE(" > dma: databucket-pos %u, databucket-len %u, databucket-bound %u",
          LODWORD(entry->DataBucketPosition), LODWORD(entry->DataBucketLength),
          LODWORD(entry->BucketByteBoundary));
    TRACE(" > sec %u, count %u, offset %u", LODWORD(MFS_GETSECTOR(mfs, entry->DataBucketPosition)),
          LODWORD(MFS_SECTORCOUNT(mfs, entry->DataBucketLength)), LODWORD(position - entry->BucketByteBoundary));

    while (bytesToRead > sizeof(struct VFSDirectoryEntry)) {
        uint64_t sector       = MFS_GETSECTOR(mfs, entry->DataBucketPosition);
        size_t   sectorCount  = MFS_SECTORCOUNT(mfs, entry->DataBucketLength);
        size_t   bucketOffset = position - entry->BucketByteBoundary;
        uint8_t* transferBuffer = (uint8_t*)SHMBuffer(&mfs->TransferBuffer);
        size_t   bucketSize = sectorCount * mfs->SectorSize;
        size_t   sectorsRead;
        TRACE("read_metrics:: sector=%u, sectorCount=%u, bucketOffset=%u, bucketSize=%u",
              LODWORD(sector), sectorCount, bucketOffset, bucketSize);

        if (bucketSize > bucketOffset) {
            // The code here is simple because we assume we can fit entire bucket at any time
            oserr = FSStorageRead(
                    &mfs->Storage,
                    mfs->TransferBuffer.ID,
                    0,
                    &(UInteger64_t) { .QuadPart = sector },
                    sectorCount,
                    &sectorsRead
            );
            if (oserr != OS_EOK) {
                ERROR("Failed to read sector");
                break;
            }

            // Which position are we in?
            for (FileRecord_t* fileRecord = (FileRecord_t*)(transferBuffer + bucketOffset);
                    (bucketSize > bucketOffset) && bytesToRead;
                    fileRecord++, bucketOffset += sizeof(FileRecord_t), position += sizeof(FileRecord_t)) {
                if (fileRecord->Flags & MFS_FILERECORD_INUSE) {
                    TRACE("Gathering entry %s", &fileRecord->Name[0]);
                    size_t written = __WriteDirectoryEntry(
                            fileRecord,
                            bufferPointer,
                            bytesToRead
                    );
                    if (!written) {
                        goto exit;
                    }
                    bytesToRead -= written;
                    bufferPointer += written;
                }
            }
        }
        
        // Do we need to switch bucket?
        // We do if the position we have read to equals end of bucket
        if (position == (entry->BucketByteBoundary + bucketSize)) {
            TRACE("read_metrics::position %u, limit %u", LODWORD(position),
                LODWORD(entry->BucketByteBoundary + bucketSize));
            oserr = MFSAdvanceToNextBucket(mfs, entry);
            if (oserr != OS_EOK) {
                if (oserr == OS_ENOENT) {
                    oserr = OS_EOK;
                }
                break;
            }
        }
    }

exit:
    entry->Position = position / sizeof(FileRecord_t);
    *unitsRead = (unitCount - bytesToRead);
    return oserr;
}

// TODO this is wrong
oserr_t
FsSeekInDirectory(
        _In_ FileSystemMFS_t* mfs,
        _In_ MFSEntry_t*      entry,
        _In_ uint64_t         absolutePosition)
{
    uint64_t actualPosition = absolutePosition * sizeof(struct VFSDirectoryEntry);
    size_t   initialBucketMax;

    // Trace
    TRACE("FsSeekInDirectory(entry=%ms, position=%u)",
          entry->Name, LODWORD(absolutePosition));

    // Sanitize seeking bounds
    if (entry->ActualSize == 0) {
        return OS_EINVALPARAMS;
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
                if (MFSBucketMapGetLengthAndLink(mfs, BucketPtr, &Link) != OS_EOK) {
                    ERROR("FsSeekInDirectory failed to get link for bucket %u", BucketPtr);
                    return OS_EDEVFAULT;
                }

                // If we do reach end of chain, something went terribly wrong
                if (Link.Link == MFS_ENDOFCHAIN) {
                    ERROR("Reached end of chain during seek");
                    return OS_EINVALPARAMS;
                }
                BucketPtr = Link.Link;

                // Get length of link
                if (MFSBucketMapGetLengthAndLink(mfs, BucketPtr, &Link) != OS_EOK) {
                    ERROR("Failed to get length for bucket %u", BucketPtr);
                    return OS_EDEVFAULT;
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
    return OS_EOK;
}
