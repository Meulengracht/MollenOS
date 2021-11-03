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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
#include <threads.h>
#include <stdlib.h>
 * General File System (MFS) Driver
 *  - Contains the implementation of the MFS driver for mollenos
 */
//#define __TRACE

#include <ddk/utils.h>
#include <string.h>
#include <io.h>
#include "mfs.h"

OsStatus_t
FsReadFromDirectory(
        _In_  FileSystemBase_t*      fileSystemBase,
        _In_  FileSystemEntryMFS_t*  entry,
        _In_  FileSystemHandleMFS_t* handle,
        _In_  UUId_t                 bufferHandle,
        _In_  void*                  buffer,
        _In_  size_t                 bufferOffset,
        _In_  size_t                 unitCount,
        _Out_ size_t*                unitsRead)
{
    FileSystemMFS_t* mfs = (FileSystemMFS_t*)fileSystemBase->ExtensionData;
    OsStatus_t     osStatus    = OsSuccess;
    size_t         bytesToRead = unitCount;
    uint64_t       position    = handle->Base.Position;
    struct DIRENT* currentEntry = (struct DIRENT*)((uint8_t*)buffer + bufferOffset);

    TRACE("FsReadFromDirectory(Id 0x%x, Position %u, Length %u)",
          handle->Base.Id, LODWORD(position), unitCount);

    // Indicate zero bytes read to start with
    *unitsRead = 0;

    // Readjust the stored position since its stored in units of DIRENT, however we
    // iterate in units of MfsRecords
    position /= sizeof(struct DIRENT);
    position *= sizeof(FileRecord_t);

    if ((unitCount % sizeof(struct DIRENT)) != 0) {
        return OsInvalidParameters;
    }
    
    TRACE(" > dma: fpos %u, bytes-total %u, bytes-at %u", LODWORD(position), bytesToRead, *BytesAt);
    TRACE(" > dma: databucket-pos %u, databucket-len %u, databucket-bound %u",
          LODWORD(handle->DataBucketPosition), LODWORD(handle->DataBucketLength),
          LODWORD(handle->BucketByteBoundary));
    TRACE(" > sec %u, count %u, offset %u", LODWORD(MFS_GETSECTOR(mfs, handle->DataBucketPosition)),
          LODWORD(MFS_GETSECTOR(mfs, handle->DataBucketLength)), LODWORD(position - handle->BucketByteBoundary));

    while (bytesToRead) {
        uint64_t Sector     = MFS_GETSECTOR(mfs, handle->DataBucketPosition);
        size_t   Count      = MFS_GETSECTOR(mfs, handle->DataBucketLength);
        size_t   Offset     = position - handle->BucketByteBoundary;
        uint8_t* Data       = (uint8_t*)mfs->TransferBuffer.buffer;
        size_t   BucketSize = Count * fileSystemBase->Disk.descriptor.SectorSize;
        size_t   SectorsRead;
        TRACE("read_metrics:: sector %u, count %u, offset %u, bucket-size %u",
            LODWORD(Sector), Count, Offset, BucketSize);

        if (BucketSize > Offset) {
            // The code here is simple because we assume we can fit entire bucket at any time
            if (MfsReadSectors(fileSystemBase, mfs->TransferBuffer.handle,
                               0, Sector, Count, &SectorsRead) != OsSuccess) {
                ERROR("Failed to read sector");
                osStatus = OsDeviceError;
                break;
            }

            // Which position are we in?
            for (FileRecord_t* RecordPtr = (FileRecord_t*)(Data + Offset); (Offset < BucketSize) && bytesToRead;
                RecordPtr++, Offset += sizeof(FileRecord_t), position += sizeof(FileRecord_t)) {
                if (RecordPtr->Flags & MFS_FILERECORD_INUSE) {
                    TRACE("Gathering entry %s", &RecordPtr->Name[0]);
                    MfsFileRecordFlagsToVfsFlags(RecordPtr, &currentEntry->d_options, &currentEntry->d_perms);
                    memcpy(&currentEntry->d_name[0], &RecordPtr->Name[0],
                           MIN(sizeof(currentEntry->d_name), sizeof(RecordPtr->Name)));
                    bytesToRead -= sizeof(struct DIRENT);
                    currentEntry++;
                }
            }
        }
        
        // Do we need to switch bucket?
        // We do if the position we have read to equals end of bucket
        if (position == (handle->BucketByteBoundary + BucketSize)) {
            TRACE("read_metrics::position %u, limit %u", LODWORD(position),
                LODWORD(handle->BucketByteBoundary + BucketSize));
            osStatus = MfsSwitchToNextBucketLink(fileSystemBase, handle, mfs->SectorsPerBucket * fileSystemBase->Disk.descriptor.SectorSize);
            if (osStatus != OsSuccess) {
                if (osStatus == OsDoesNotExist) {
                    osStatus = OsSuccess;
                }
                break;
            }
        }
    }
    
    // Readjust the position to the current position, but it has to be in units
    // of DIRENT instead of MfsRecords, and then readjust again for the number of
    // bytes read, since they are added to position in the vfs layer
    position              /= sizeof(FileRecord_t);
    position              *= sizeof(struct DIRENT);
    handle->Base.Position = position - (unitCount - bytesToRead);
    
    *unitsRead = (unitCount - bytesToRead);
    return osStatus;
}

OsStatus_t
FsSeekInDirectory(
        _In_ FileSystemBase_t*      fileSystemBase,
        _In_ FileSystemEntryMFS_t*  entry,
        _In_ FileSystemHandleMFS_t* handle,
        _In_ uint64_t               absolutePosition)
{
    FileSystemMFS_t* mfs            = (FileSystemMFS_t*)fileSystemBase->ExtensionData;
    uint64_t         actualPosition = absolutePosition * sizeof(struct DIRENT);
    size_t           initialBucketMax;

    // Trace
    TRACE("FsSeekInDirectory(Id 0x%x, Position 0x%x)", handle->Base.Id, LODWORD(absolutePosition));

    // Sanitize seeking bounds
    if (entry->Base.Descriptor.Size.QuadPart == 0) {
        return OsInvalidParameters;
    }

    // Step 1, if the new position is in
    // initial bucket, we need to do no actual seeking
    initialBucketMax = (entry->StartLength * (mfs->SectorsPerBucket * fileSystemBase->Disk.descriptor.SectorSize));
    if (absolutePosition < initialBucketMax) {
        handle->DataBucketPosition = entry->StartBucket;
        handle->DataBucketLength   = entry->StartLength;
        handle->BucketByteBoundary = 0;
    }
    else {
        // Step 2. We might still get out easy
        // if we are setting a new position that's within the current bucket
        uint64_t OldBucketLow, OldBucketHigh;

        // Calculate bucket boundaries
        OldBucketLow  = handle->BucketByteBoundary;
        OldBucketHigh = OldBucketLow + (handle->DataBucketLength
                                        * (mfs->SectorsPerBucket * fileSystemBase->Disk.descriptor.SectorSize));

        // If we are seeking inside the same bucket no need
        // to do anything else
        if (absolutePosition >= OldBucketLow && absolutePosition < OldBucketHigh) {
            // Same bucket
        }
        else {
            // We need to figure out which bucket the position is in
            uint64_t PositionBoundLow   = 0;
            uint64_t PositionBoundHigh  = initialBucketMax;
            MapRecord_t Link;

            // Start at the file-bucket
            uint32_t BucketPtr      = entry->StartBucket;
            uint32_t BucketLength   = entry->StartLength;
            while (1) {
                // Check if we reached correct bucket
                if (absolutePosition >= PositionBoundLow
                    && absolutePosition < (PositionBoundLow + PositionBoundHigh)) {
                    handle->BucketByteBoundary = PositionBoundLow;
                    break;
                }

                // Get link
                if (MfsGetBucketLink(fileSystemBase, BucketPtr, &Link) != OsSuccess) {
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
                if (MfsGetBucketLink(fileSystemBase, BucketPtr, &Link) != OsSuccess) {
                    ERROR("Failed to get length for bucket %u", BucketPtr);
                    return OsDeviceError;
                }
                BucketLength        = Link.Length;
                PositionBoundLow    += PositionBoundHigh;
                PositionBoundHigh   = (BucketLength * 
                    (mfs->SectorsPerBucket * fileSystemBase->Disk.descriptor.SectorSize));
            }

            // Update bucket pointer
            if (BucketPtr != MFS_ENDOFCHAIN) {
                handle->DataBucketPosition = BucketPtr;
            }
        }
    }
    
    // Update the new position since everything went ok
    handle->Base.Position = actualPosition;
    return OsSuccess;
}
