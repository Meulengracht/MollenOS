/**
 * Copyright 2023, Philip Meulengracht
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

#define __TRACE
#define __need_minmax
#include <ddk/utils.h>
#include <fs/common.h>
#include <os/shm.h>
#include <string.h>
#include "mfs.h"

oserr_t
FsReadFromFile(
        _In_  FileSystemMFS_t* mfs,
        _In_  MFSEntry_t*      entry,
        _In_  uuid_t           bufferHandle,
        _In_  void*            buffer,
        _In_  size_t           bufferOffset,
        _In_  size_t           unitCount,
        _Out_ size_t*          unitsRead)
{
    oserr_t  oserr    = OS_EOK;
    uint64_t position = entry->Position;
    size_t   bucketSizeBytes = mfs->SectorsPerBucket * mfs->SectorSize;
    size_t   bytesToRead     = unitCount;

    TRACE("FsReadFromFile(name=%ms, position=%u, length=%u)",
          entry->Name, LODWORD(entry->Position), LODWORD(unitCount));

    // Zero this first to indicate no bytes read
    *unitsRead = 0;

    // Sanitize the amount of bytes we want to read, cap it at bytes available
    if ((position + bytesToRead) > entry->ActualSize) {
        if (position >= entry->ActualSize) {
            return OS_EOK;
        }
        bytesToRead = (size_t)(entry->ActualSize - position);
    }

    if (entry->DataBucketPosition == MFS_ENDOFCHAIN) {
        // No buckets allocated for this file. Guard against this as there are two cases;
        // Case 1 - Newly created file, return OS_EOK;
        if (entry->ActualSize == 0) {
            return OS_EOK;
        } else {
            // Read from integrated data
            // TODO
        }
    }

    // Read the current sector, update index to where data starts
    // Keep reading consecutive after that untill all bytes requested have
    // been read
    while (bytesToRead) {
        // Calculate which bucket, then the sector offset
        // Then calculate how many sectors of the bucket we need to read
        uint64_t Sector       = MFS_GETSECTOR(mfs, entry->DataBucketPosition);    // Start-sector of current bucket
        uint64_t SectorOffset = position % mfs->SectorSize; // Byte-offset into the current sector
        size_t   SectorIndex  = (size_t)((position - entry->BucketByteBoundary) / mfs->SectorSize); // The sector-index into the current bucket
        size_t   SectorsLeft  = MFS_SECTORCOUNT(mfs, entry->DataBucketLength) - SectorIndex; // How many sectors are left in this bucket
        size_t   SectorCount;
        size_t   SectorsRead;
        size_t   ByteCount;
        
        // The buffer handle + offset that was selected for reading 
        uuid_t SelectedHandle = mfs->TransferBuffer.ID;
        size_t SelectedOffset = 0;
        
        // Calculate the sector index into bucket
        Sector += SectorIndex;

        // CASE 1: DIRECT READING INTO USER BUFFER
        // <Sector> now contains where we should start reading, and SectorOffset is
        // the byte offset into that first sector. This means if we request any number of bytes
        // we should also have room for that. So to read directly into user provided buffer, we MUST
        // ensure that <SectorOffset> is 0 and that we can read atleast one entire sector to avoid
        // any form for discarding of data.
        if (SectorOffset == 0 && bytesToRead >= mfs->SectorSize) {
            SectorCount    = bytesToRead / mfs->SectorSize;
            SelectedHandle = bufferHandle;
            SelectedOffset = bufferOffset;
        }
        
        // CASE 2: SINGLE READ INTO INTERMEDIATE BUFFER
        // We want this to happen when we can fit the entire read into our fs transfer buffer
        // that can act as an intermediate buffer. So calculate enough space for <BytesToRead>, with
        // room for <SectorOffset> and also the spill-over bytes thats left for the sector
        else if ((SectorOffset + bytesToRead + (mfs->SectorSize -
                                                ((SectorOffset + bytesToRead) % mfs->SectorSize))) <=
                SHMBufferLength(&mfs->TransferBuffer)) {
            SectorCount = DIVUP(bytesToRead, mfs->SectorSize);
            if (SectorOffset != 0 && (SectorOffset + bytesToRead > mfs->SectorSize)) {
                SectorCount++; // Take into account the extra sector we have to read
            }
        }

        // CASE 3: SINGLE READ INTO INTERMEDIATE BUFFER TO CORRECTLY ALIGN FOR CASE 1
        // Make sure we make a single read to align the file position to a sector offset
        // so we can directly read into the buffer
        // CASE 4: SINGLE READ INTO INTERMEDIATE BUFFER TO FINISH TRANSFER AFTER CASE 1
        // Read a single sector into the buffer to copy the remaining bytes
        else {
            // Just read a single sector, the ByteCount adjustor will automatically
            // adjust the number of bytes to read in this iteration
            SectorCount = 1;
        }

        // Adjust for bucket boundary
        SectorCount = MIN(SectorCount, SectorsLeft);
        if (SectorCount != 0) {
            // Adjust for number of bytes already consumed in the active sector
            ByteCount = MIN(bytesToRead, (SectorCount * mfs->SectorSize) - SectorOffset);
    
            // Ex pos 490 - length 50
            // SectorIndex = 0, SectorOffset = 490, SectorCount = 2 - ByteCount = 50 (Capacity 4096)
            // Ex pos 1109 - length 450
            // SectorIndex = 2, SectorOffset = 85, SectorCount = 2 - ByteCount = 450 (Capacity 4096)
            // Ex pos 490 - length 4000
            // SectorIndex = 0, SectorOffset = 490, SectorCount = 8 - ByteCount = 3606 (Capacity 4096)
            TRACE(" > sector %u (b-start %u, b-index %u), num-sectors %u, sector-byte-offset %u, bytecount %u",
                LODWORD(Sector), LODWORD(Sector) - SectorIndex, SectorIndex, SectorCount, LODWORD(SectorOffset), ByteCount);

            oserr = FSStorageRead(
                    &mfs->Storage,
                    SelectedHandle,
                    SelectedOffset,
                    &(UInteger64_t) { .QuadPart = Sector },
                    SectorCount,
                    &SectorsRead
            );
            if (oserr != OS_EOK) {
                ERROR("Failed to read sector");
                break;
            }
            
            // Adjust for how many sectors we actually read
            if (SectorCount != SectorsRead) {
                ByteCount = (mfs->SectorSize * SectorsRead) - SectorOffset;
            }
            
            // If we used the intermediate buffer for the transfer we now have to copy
            // <ByteCount> amount of bytes from <TransferBuffer> + <SectorOffset> to <Buffer> + <BufferOffset>
            if (SelectedHandle == mfs->TransferBuffer.ID) {
                memcpy(((uint8_t*)buffer + bufferOffset), ((uint8_t*)SHMBuffer(&mfs->TransferBuffer) + SectorOffset), ByteCount);
            }
            
            // Increament all read-state variables
            *unitsRead   += ByteCount;
            bufferOffset += ByteCount;
            position     += ByteCount;
            bytesToRead  -= ByteCount;
        }

        // Do we need to switch bucket?
        // We do if the position we have read to equals end of bucket
        if (position == (entry->BucketByteBoundary + (entry->DataBucketLength * bucketSizeBytes))) {
            oserr = MFSAdvanceToNextBucket(mfs, entry);
            if (oserr != OS_EOK) {
                if (oserr == OS_ENOENT) {
                    oserr = OS_EOK;
                }
                break;
            }
        }
    }

    // Store the new position we've calculated during the read operation.
    entry->Position = position;

    TRACE("[mfs] [read_file] bytes read %u/%u", *unitsRead, unitCount);
    return oserr;
}

oserr_t
FsWriteToFile(
        _In_  FileSystemMFS_t* mfs,
        _In_  MFSEntry_t*      entry,
        _In_  uuid_t           bufferHandle,
        _In_  void*            buffer,
        _In_  size_t           bufferOffset,
        _In_  size_t           unitCount,
        _Out_ size_t*          unitsWritten)
{
    uint64_t position        = entry->Position;
    size_t   bucketSizeBytes = mfs->SectorsPerBucket * mfs->SectorSize;
    size_t   bytesToWrite    = unitCount;
    size_t   accumOffset     = bufferOffset;
    oserr_t  oserr;

    TRACE("FsWriteToFile(name=%ms, position=%u, length=%u)",
          entry->Name, LODWORD(entry->Position), LODWORD(unitCount));

    // Set 0 to start out with, in case of errors we want to indicate correctly.
    *unitsWritten = 0;
    
    // We do not have the same boundary limits here as we do when reading, when we
    // write to a file we can do so untill we run out of space on the filesystem.
    oserr = MfsEnsureRecordSpace(mfs, entry, position + bytesToWrite);
    if (oserr != OS_EOK) {
        ERROR("FsWriteToFile: failed to ensure record space for write: %u", oserr);
        return oserr;
    }

    // Guard against newly allocated files
    if (entry->DataBucketPosition == MFS_ENDOFCHAIN) {
        TRACE("FsWriteToFile: initializing new record");
        entry->DataBucketPosition = entry->StartBucket;
        entry->DataBucketLength   = entry->StartLength;
        entry->BucketByteBoundary = 0;
    }
    
    // Write in a loop to make sure we write all requested bytes
    while (bytesToWrite) {
        // Determine whether we need to switch bucket as the first step of writing. Do
        // this to avoid switching buckets when writing the last byte of a file. There will
        // always be enough space to write because of Ensure.
        TRACE("FsWriteToFile: position=0x%llx, boundary at 0x%llx",
              position, (entry->BucketByteBoundary + (entry->DataBucketLength * bucketSizeBytes)));
        if (position == (entry->BucketByteBoundary + (entry->DataBucketLength * bucketSizeBytes))) {
            oserr = MFSAdvanceToNextBucket(mfs, entry);
            if (oserr != OS_EOK) {
                ERROR("FsWriteToFile: failed to get next data bucket: %u", oserr);
                break;
            }
        }

        // Calculate which bucket, then the sector offset
        // Then calculate how many sectors of the bucket we need to read
        uint64_t bucketSector       = MFS_GETSECTOR(mfs, entry->DataBucketPosition);
        uint64_t bucketSectorOffset = (position - entry->BucketByteBoundary) % mfs->SectorSize;
        size_t   sectorIndex        = (size_t)((position - entry->BucketByteBoundary) / mfs->SectorSize);
        size_t   sectorsLeft        = MFS_SECTORCOUNT(mfs, entry->DataBucketLength) - sectorIndex;
        size_t   sectorCount;
        size_t   sectorsWritten;
        size_t   byteCount;

        TRACE("FsWriteToFile: position=0x%llx, entry->BucketByteBoundary=0x%llx",
              position, entry->BucketByteBoundary);
        TRACE("FsWriteToFile: bucketSector=0x%llx, sectorIndex=0x%" PRIxIN ", sectorOffset=0x%llx",
              bucketSector, bucketSectorOffset, sectorIndex);

        // The buffer handle + offset that was selected for writing 
        uuid_t SelectedHandle = mfs->TransferBuffer.ID;
        size_t SelectedOffset = 0;

        // Calculate the sector index into bucket
        bucketSector += sectorIndex;
        
        // CASE 1: WE CAN WRITE DIRECTLY FROM USER-BUFFER TO DISK
        // If <SectorOffset> is 0, this means we can write directly to the disk
        // from <Buffer> + <accumOffset>. We must also be able to write an entire
        // sector to avoid writing out of bounds from the buffer
        if (bucketSectorOffset == 0 && bytesToWrite >= mfs->SectorSize) {
            sectorCount    = bytesToWrite / mfs->SectorSize;
            SelectedHandle = bufferHandle;
            SelectedOffset = accumOffset;
            TRACE("FsWriteToFile: [case 1] direct transfer %" PRIuIN " bytes from user-buffer",
                  sectorCount * mfs->SectorSize);
        }
        
        // CASE 2: SECTOR-ALIGN BY READING-WRITE ONE SECTOR FIRST FOR CASE 1
        // If we are not aligned to a sector boundary in the file, we should start
        // out by writing the rest of the sector with a read-write combine. This
        // must happen by using the intermediate buffer
        // CASE 3: FINAL SECTOR WRITE BY USING INTERMEDIATE BUFFER
        else {
            TRACE("FsWriteToFile: [case 2]");
            sectorCount = 1;
        }

        // Adjust for bucket boundary
        sectorCount = MIN(sectorsLeft, sectorCount);
        TRACE("FsWriteToFile: sectors (adjusted)=0x%" PRIuIN, sectorCount);
        if (sectorCount != 0) {
            byteCount = MIN(bytesToWrite, (sectorCount * mfs->SectorSize) - bucketSectorOffset);
    
            // Ex pos 490 - length 50
            // SectorIndex = 0, SectorOffset = 490, SectorCount = 2 - ByteCount = 50 (Capacity 4096)
            // Ex pos 1109 - length 450
            // SectorIndex = 2, SectorOffset = 85, SectorCount = 2 - ByteCount = 450 (Capacity 4096)
            // Ex pos 490 - length 4000
            // SectorIndex = 0, SectorOffset = 490, SectorCount = 8 - ByteCount = 3606 (Capacity 4096)
            TRACE("FsWriteToFile: bytes to write (adjusted)=%" PRIuIN, byteCount);
    
            // First of all, calculate the bounds as we might need to read
            // in existing data - Start out by clearing our combination buffer
            if (SelectedHandle == mfs->TransferBuffer.ID) {
                TRACE("FsWriteToFile: write-combine");
                memset(
                        SHMBuffer(&mfs->TransferBuffer),
                        0,
                        SHMBufferLength(&mfs->TransferBuffer)
                );
                
                // CASE READ-WRITE: We do this to support appending to sectors and overwriting
                // bytes in a sector. This must occur when we either have a <SectorOffset> != 0
                // or when the <SectorOffset> == 0 and <ByteCount> is less than a sector.
                TRACE("FsWriteToFile: read sector=0x%llx, count=0x%" PRIuIN, bucketSector, sectorCount);
                oserr = FSStorageRead(
                        &mfs->Storage,
                        mfs->TransferBuffer.ID,
                        0,
                        &(UInteger64_t) { .QuadPart = bucketSector },
                        sectorCount,
                        &sectorsWritten
                );
                if (oserr != OS_EOK) {
                    ERROR("Failed to read sector %u for combination step", 
                        LODWORD(bucketSector));
                    break;
                }
                
                // Now perform the user copy operation where we overwrite some of the data
                // Copy from <Buffer> + <accumOffset> to <TransferBuffer> + <SectorOffset>
                TRACE("FsWriteToFile: copying 0x%" PRIuIN " bytes at offset=0x%" PRIuIN " into read buffer",
                      byteCount, bucketSectorOffset);
                memcpy(
                        ((uint8_t*)SHMBuffer(&mfs->TransferBuffer) + bucketSectorOffset),
                        ((uint8_t*)buffer + accumOffset),
                        byteCount
                );
            }
            
            // Write either the intermediate buffer or directly from user
            TRACE("FsWriteToFile: write sector=0x%llx, count=0x%" PRIuIN, bucketSector, sectorCount);
            oserr = FSStorageWrite(
                    &mfs->Storage,
                    SelectedHandle,
                    SelectedOffset,
                    &(UInteger64_t) { .QuadPart = bucketSector },
                    sectorCount,
                    &sectorsWritten
            );
            if (oserr != OS_EOK) {
                ERROR("Failed to write sector %u", LODWORD(bucketSector));
                break;
            }
            
            // Adjust for how many sectors we actually read
            if (sectorCount != sectorsWritten) {
                byteCount = (mfs->SectorSize * sectorsWritten) - bucketSectorOffset;
            }

            TRACE("FsWriteToFile: written %" PRIuIN " bytes", byteCount);
            *unitsWritten += byteCount;
            accumOffset   += byteCount;
            position      += byteCount;
            bytesToWrite  -= byteCount;
        }
    }

    // Store the new position we've calculated during the read operation.
    entry->Position = position;
    if (entry->Position > entry->ActualSize) {
        entry->ActualSize = entry->Position;
        entry->ActionOnClose = MFS_ACTION_UPDATE;
    }
    return oserr;
}

oserr_t
FsSeekInFile(
        _In_ FileSystemMFS_t* mfs,
        _In_ MFSEntry_t*      entry,
        _In_ uint64_t         absolutePosition)
{
    size_t initialBucketMax;

    TRACE("FsSeekInFile(name=%ms, position=0x%x)", entry->Name, LODWORD(absolutePosition));

    // Step 1, if the new position is in
    // initial bucket, we need to do no actual seeking
    initialBucketMax = (entry->StartLength * (mfs->SectorsPerBucket * mfs->SectorSize));
    if (absolutePosition < initialBucketMax) {
        entry->DataBucketPosition = entry->StartBucket;
        entry->DataBucketLength   = entry->StartLength;
        entry->BucketByteBoundary = 0;
    } else {
        // Step 2. We might still get out easy
        // if we are setting a new position that's 
        // within the current bucket
        uint64_t OldBucketLow, OldBucketHigh;

        // Calculate bucket boundaries
        OldBucketLow  = entry->BucketByteBoundary;
        OldBucketHigh = OldBucketLow + (entry->DataBucketLength
                                        * (mfs->SectorsPerBucket * mfs->SectorSize));

        // If we are seeking inside the same bucket no need
        // to do anything else
        if (absolutePosition >= OldBucketLow && absolutePosition < OldBucketHigh) {
            // Same bucket
        } else {
            // We need to figure out which bucket the position is in
            uint64_t    PositionBoundLow  = 0;
            uint64_t    PositionBoundHigh = initialBucketMax;
            MapRecord_t Link;

            // Start at the file-bucket
            uint32_t BucketPtr    = entry->StartBucket;
            uint32_t BucketLength = entry->StartLength;
            while (1) {
                // Check if we reached correct bucket
                TRACE("[mfs] [seek] are we there yeti? [%llu-%llu]/%llu",
                      PositionBoundLow, PositionBoundLow + PositionBoundHigh, absolutePosition);
                
                // Check termination conditions
                if (absolutePosition >= PositionBoundLow && absolutePosition < (PositionBoundLow + PositionBoundHigh)) {
                    break;
                }

                // Get link
                if (MFSBucketMapGetLengthAndLink(mfs, BucketPtr, &Link) != OS_EOK) {
                    ERROR("FsSeekInFile failed to get link for bucket %u", BucketPtr);
                    return OS_EDEVFAULT;
                }

                // If we do reach end of chain, then we handle this case by setting
                // the pointer to the wished destination, and store the last indices
                if (Link.Link == MFS_ENDOFCHAIN) {
                    WARNING("[mfs] [seek] seeking beyond eof [%llu-%llu]/%llu",
                            PositionBoundLow, PositionBoundLow + PositionBoundHigh, absolutePosition);
                    break;
                }
                
                BucketPtr = Link.Link;

                // Get length of link
                if (MFSBucketMapGetLengthAndLink(mfs, BucketPtr, &Link) != OS_EOK) {
                    ERROR("Failed to get length for bucket %u", BucketPtr);
                    return OS_EDEVFAULT;
                }
                BucketLength = Link.Length;

                // Calculate bounds for the new bucket
                PositionBoundLow += PositionBoundHigh;
                PositionBoundHigh = (BucketLength * (mfs->SectorsPerBucket * mfs->SectorSize));
            }

            // Update handle positioning
            entry->DataBucketPosition = BucketPtr;
            entry->DataBucketLength   = BucketLength;
            entry->BucketByteBoundary = PositionBoundLow;
        }
    }
    
    // Update the new position since everything went ok
    entry->Position = absolutePosition;
    return OS_EOK;
}

oserr_t
FsTruncate(
        _In_ void*    instanceData,
        _In_ void*    data,
        _In_ uint64_t size)
{
    FileSystemMFS_t* mfs = instanceData;
    MFSEntry_t*      entry    = (MFSEntry_t*)data;
    oserr_t          osStatus = OS_EOK;
    TRACE("FsChangeFileSize(Name %ms, Size 0x%x)", entry->Name, LODWORD(size));

    // Handle a special case of 0
    if (size == 0) {
        // Free all buckets allocated, if any are allocated
        if (entry->StartBucket != MFS_ENDOFCHAIN) {
            oserr_t Status = MfsFreeBuckets(mfs, entry->StartBucket, entry->StartLength);
            if (Status != OS_EOK) {
                ERROR("Failed to free the buckets at start 0x%x, length 0x%x. when truncating",
                      entry->StartBucket, entry->StartLength);
                osStatus = OS_EDEVFAULT;
            }
        }

        if (osStatus == OS_EOK) {
            entry->AllocatedSize = 0;
            entry->StartBucket   = MFS_ENDOFCHAIN;
            entry->StartLength   = 0;
        }
    } else {
        osStatus = MfsEnsureRecordSpace(mfs, entry, size);
    }

    if (osStatus == OS_EOK) {
        // entry->modified = now
        entry->ActualSize    = size;
        entry->ActionOnClose = MFS_ACTION_UPDATE;
    }
    return osStatus;
}
