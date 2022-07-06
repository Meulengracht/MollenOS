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
#include <string.h>
#include "mfs.h"

oscode_t
FsReadFromFile(
        _In_  FileSystemBase_t*      fileSystemBase,
        _In_  FileSystemEntryMFS_t*  entry,
        _In_  FileSystemHandleMFS_t* handle,
        _In_  UUId_t                 bufferHandle,
        _In_  void*                  buffer,
        _In_  size_t                 bufferOffset,
        _In_  size_t                 unitCount,
        _Out_ size_t*                unitsRead)
{
    FileSystemMFS_t* Mfs             = (FileSystemMFS_t*)fileSystemBase->ExtensionData;
    oscode_t       osStatus        = OsOK;
    uint64_t         position        = handle->Base.Position;
    size_t           bucketSizeBytes = Mfs->SectorsPerBucket * fileSystemBase->Disk.descriptor.SectorSize;
    size_t           bytesToRead     = unitCount;

    TRACE("[mfs] [read_file] id 0x%x, position %u, length %u",
          handle->Base.Id, LODWORD(handle->Base.position), LODWORD(unitCount));

    // Zero this first to indicate no bytes read
    *unitsRead = 0;

    // Sanitize the amount of bytes we want to read, cap it at bytes available
    if ((position + bytesToRead) > entry->Base.Descriptor.Size.QuadPart) {
        if (position >= entry->Base.Descriptor.Size.QuadPart) {
            return OsOK;
        }
        bytesToRead = (size_t)(entry->Base.Descriptor.Size.QuadPart - position);
    }

    if (handle->DataBucketPosition == MFS_ENDOFCHAIN) {
        // No buckets allocated for this file. Guard against this as there are two cases;
        // Case 1 - Newly created file, return OsOK;
        if (entry->Base.Descriptor.Size.QuadPart == 0) {
            return OsOK;
        }
        else {
            // Read from integrated data
            // TODO
        }
    }
    
    // Debug counter values
    TRACE(" > dma: 0x%x, fpos %u, bytes-total %u, bytes-at %u", DataPointer,
          LODWORD(position), bytesToRead, *BytesAt);

    // Read the current sector, update index to where data starts
    // Keep reading consecutive after that untill all bytes requested have
    // been read
    while (bytesToRead) {
        // Calculate which bucket, then the sector offset
        // Then calculate how many sectors of the bucket we need to read
        uint64_t Sector       = MFS_GETSECTOR(Mfs, handle->DataBucketPosition);    // Start-sector of current bucket
        uint64_t SectorOffset = position % fileSystemBase->Disk.descriptor.SectorSize; // Byte-offset into the current sector
        size_t   SectorIndex  = (size_t)((position - handle->BucketByteBoundary) / fileSystemBase->Disk.descriptor.SectorSize); // The sector-index into the current bucket
        size_t   SectorsLeft  = MFS_SECTORCOUNT(Mfs, handle->DataBucketLength) - SectorIndex; // How many sectors are left in this bucket
        size_t   SectorCount;
        size_t   SectorsRead;
        size_t   ByteCount;
        
        // The buffer handle + offset that was selected for reading 
        UUId_t SelectedHandle = Mfs->TransferBuffer.handle;
        size_t SelectedOffset = 0;
        
        // Calculate the sector index into bucket
        Sector += SectorIndex;

        // CASE 1: DIRECT READING INTO USER BUFFER
        // <Sector> now contains where we should start reading, and SectorOffset is
        // the byte offset into that first sector. This means if we request any number of bytes
        // we should also have room for that. So to read directly into user provided buffer, we MUST
        // ensure that <SectorOffset> is 0 and that we can read atleast one entire sector to avoid
        // any form for discarding of data.
        if (SectorOffset == 0 && bytesToRead >= fileSystemBase->Disk.descriptor.SectorSize) {
            SectorCount    = bytesToRead / fileSystemBase->Disk.descriptor.SectorSize;
            SelectedHandle = bufferHandle;
            SelectedOffset = bufferOffset;
        }
        
        // CASE 2: SINGLE READ INTO INTERMEDIATE BUFFER
        // We want this to happen when we can fit the entire read into our fs transfer buffer
        // that can act as an intermediate buffer. So calculate enough space for <BytesToRead>, with
        // room for <SectorOffset> and also the spill-over bytes thats left for the sector
        else if ((SectorOffset + bytesToRead + (fileSystemBase->Disk.descriptor.SectorSize -
                                                ((SectorOffset + bytesToRead) % fileSystemBase->Disk.descriptor.SectorSize))) <=
                 Mfs->TransferBuffer.length) {
            SectorCount = DIVUP(bytesToRead, fileSystemBase->Disk.descriptor.SectorSize);
            if (SectorOffset != 0 && (SectorOffset + bytesToRead > fileSystemBase->Disk.descriptor.SectorSize)) {
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
            ByteCount = MIN(bytesToRead, (SectorCount * fileSystemBase->Disk.descriptor.SectorSize) - SectorOffset);
    
            // Ex pos 490 - length 50
            // SectorIndex = 0, SectorOffset = 490, SectorCount = 2 - ByteCount = 50 (Capacity 4096)
            // Ex pos 1109 - length 450
            // SectorIndex = 2, SectorOffset = 85, SectorCount = 2 - ByteCount = 450 (Capacity 4096)
            // Ex pos 490 - length 4000
            // SectorIndex = 0, SectorOffset = 490, SectorCount = 8 - ByteCount = 3606 (Capacity 4096)
            TRACE(" > sector %u (b-start %u, b-index %u), num-sectors %u, sector-byte-offset %u, bytecount %u",
                LODWORD(Sector), LODWORD(Sector) - SectorIndex, SectorIndex, SectorCount, LODWORD(SectorOffset), ByteCount);
    
            if (MfsReadSectors(fileSystemBase, SelectedHandle, SelectedOffset,
                               Sector, SectorCount, &SectorsRead) != OsOK) {
                ERROR("Failed to read sector");
                osStatus = OsDeviceError;
                break;
            }
            
            // Adjust for how many sectors we actually read
            if (SectorCount != SectorsRead) {
                ByteCount = (fileSystemBase->Disk.descriptor.SectorSize * SectorsRead) - SectorOffset;
            }
            
            // If we used the intermediate buffer for the transfer we now have to copy
            // <ByteCount> amount of bytes from <TransferBuffer> + <SectorOffset> to <Buffer> + <BufferOffset>
            if (SelectedHandle == Mfs->TransferBuffer.handle) {
                memcpy(((uint8_t*)buffer + bufferOffset), ((uint8_t*)Mfs->TransferBuffer.buffer + SectorOffset), ByteCount);
            }
            
            // Increament all read-state variables
            *unitsRead   += ByteCount;
            bufferOffset += ByteCount;
            position     += ByteCount;
            bytesToRead  -= ByteCount;
        }

        // Do we need to switch bucket?
        // We do if the position we have read to equals end of bucket
        if (position == (handle->BucketByteBoundary + (handle->DataBucketLength * bucketSizeBytes))) {
            osStatus = MfsSwitchToNextBucketLink(fileSystemBase, handle, bucketSizeBytes);
            if (osStatus != OsOK) {
                if (osStatus == OsNotExists) {
                    osStatus = OsOK;
                }
                break;
            }
        }
    }

    // if (update_when_accessed) @todo
    // entry->accessed = now
    // entry->action_on_close = update
    TRACE("[mfs] [read_file] bytes read %u/%u", *unitsRead, UnitCount);
    return osStatus;
}

oscode_t
FsWriteToFile(
        _In_  FileSystemBase_t*      fileSystemBase,
        _In_  FileSystemEntryMFS_t*  entry,
        _In_  FileSystemHandleMFS_t* handle,
        _In_  UUId_t                 bufferHandle,
        _In_  void*                  buffer,
        _In_  size_t                 bufferOffset,
        _In_  size_t                 unitCount,
        _Out_ size_t*                unitsWritten)
{
    FileSystemMFS_t* mfs             = (FileSystemMFS_t*)fileSystemBase->ExtensionData;
    uint64_t         position        = handle->Base.Position;
    size_t           bucketSizeBytes = mfs->SectorsPerBucket * fileSystemBase->Disk.descriptor.SectorSize;
    size_t           bytesToWrite    = unitCount;
    oscode_t       osStatus;

    TRACE("FsWriteEntry(Id 0x%x, Position %u, Length %u)",
          handle->Base.Id, LODWORD(position), unitCount);

    // Set 0 to start out with, in case of errors we want to indicate correctly.
    *unitsWritten = 0;
    
    // We do not have the same boundary limits here as we do when reading, when we
    // write to a file we can do so untill we run out of space on the filesystem.
    osStatus = MfsEnsureRecordSpace(fileSystemBase, entry, position + bytesToWrite);
    if (osStatus != OsOK) {
        return osStatus;
    }

    // Guard against newly allocated files
    if (handle->DataBucketPosition == MFS_ENDOFCHAIN) {
        handle->DataBucketPosition = entry->StartBucket;
        handle->DataBucketLength   = entry->StartLength;
        handle->BucketByteBoundary = 0;
    }
    
    // Write in a loop to make sure we write all requested bytes
    while (bytesToWrite) {
        // Calculate which bucket, then the sector offset
        // Then calculate how many sectors of the bucket we need to read
        uint64_t Sector       = MFS_GETSECTOR(mfs, handle->DataBucketPosition);
        uint64_t SectorOffset = (position - handle->BucketByteBoundary) % fileSystemBase->Disk.descriptor.SectorSize;
        size_t   SectorIndex  = (size_t)((position - handle->BucketByteBoundary) / fileSystemBase->Disk.descriptor.SectorSize);
        size_t   SectorsLeft  = MFS_SECTORCOUNT(mfs, handle->DataBucketLength) - SectorIndex;
        size_t   SectorCount;
        size_t   SectorsWritten;
        size_t   ByteCount;
        
        // The buffer handle + offset that was selected for writing 
        UUId_t SelectedHandle = mfs->TransferBuffer.handle;
        size_t SelectedOffset = 0;

        // Calculate the sector index into bucket
        Sector += SectorIndex;
        
        // CASE 1: WE CAN WRITE DIRECTLY FROM USER-BUFFER TO DISK
        // If <SectorOffset> is 0, this means we can write directly to the disk
        // from <Buffer> + <BufferOffset>. We must also be able to write an entire
        // sector to avoid writing out of bounds from the buffer
        if (SectorOffset == 0 && bytesToWrite >= fileSystemBase->Disk.descriptor.SectorSize) {
            SectorCount    = bytesToWrite / fileSystemBase->Disk.descriptor.SectorSize;
            SelectedHandle = bufferHandle;
            SelectedOffset = bufferOffset;
        }
        
        // CASE 2: SECTOR-ALIGN BY READING-WRITE ONE SECTOR FIRST FOR CASE 1
        // If we are not aligned to a sector boundary in the file, we should start
        // out by writing the rest of the sector with a read-write combine. This
        // must happen by using the intermediate buffer
        // CASE 3: FINAL SECTOR WRITE BY USING INTERMEDIATE BUFFER
        else {
            SectorCount = 1;
        }

        // Adjust for bucket boundary
        SectorCount = MIN(SectorsLeft, SectorCount);
        if (SectorCount != 0) {
            ByteCount = MIN(bytesToWrite, (SectorCount * fileSystemBase->Disk.descriptor.SectorSize) - SectorOffset);
    
            // Ex pos 490 - length 50
            // SectorIndex = 0, SectorOffset = 490, SectorCount = 2 - ByteCount = 50 (Capacity 4096)
            // Ex pos 1109 - length 450
            // SectorIndex = 2, SectorOffset = 85, SectorCount = 2 - ByteCount = 450 (Capacity 4096)
            // Ex pos 490 - length 4000
            // SectorIndex = 0, SectorOffset = 490, SectorCount = 8 - ByteCount = 3606 (Capacity 4096)
            TRACE("Write metrics - Sector %u + %u, Count %u, ByteOffset %u, ByteCount %u",
                LODWORD(Sector), SectorIndex, SectorCount, LODWORD(SectorOffset), ByteCount);
    
            // First of all, calculate the bounds as we might need to read
            // in existing data - Start out by clearing our combination buffer
            if (SelectedHandle == mfs->TransferBuffer.handle) {
                memset(mfs->TransferBuffer.buffer, 0, mfs->TransferBuffer.length);
                
                // CASE READ-WRITE: We do this to support appending to sectors and overwriting
                // bytes in a sector. This must occur when we either have a <SectorOffset> != 0
                // or when the <SectorOffset> == 0 and <ByteCount> is less than a sector. 
                if (MfsReadSectors(fileSystemBase, mfs->TransferBuffer.handle, 0,
                                   Sector, SectorCount, &SectorsWritten) != OsOK) {
                    ERROR("Failed to read sector %u for combination step", 
                        LODWORD(Sector));
                    osStatus = OsDeviceError;
                    break;
                }
                
                // Now perform the user copy operation where we overwrite some of the data
                // Copy from <Buffer> + <BufferOffset> to <TransferBuffer> + <SectorOffset>
                memcpy(((uint8_t*)mfs->TransferBuffer.buffer + SectorOffset), ((uint8_t*)buffer + bufferOffset), ByteCount);
            }
            
            // Write either the intermediate buffer or directly from user
            if (MfsWriteSectors(fileSystemBase, SelectedHandle, SelectedOffset,
                                Sector, SectorCount, &SectorsWritten) != OsOK) {
                ERROR("Failed to write sector %u", LODWORD(Sector));
                osStatus = OsDeviceError;
                break;
            }
            
            // Adjust for how many sectors we actually read
            if (SectorCount != SectorsWritten) {
                ByteCount = (fileSystemBase->Disk.descriptor.SectorSize * SectorsWritten) - SectorOffset;
            }
            
            *unitsWritten += ByteCount;
            bufferOffset  += ByteCount;
            position      += ByteCount;
            bytesToWrite  -= ByteCount;
        }

        // Do we need to switch bucket?
        // We do if the position we have read to equals end of bucket
        if (position == (handle->BucketByteBoundary + (handle->DataBucketLength * bucketSizeBytes))) {
            MapRecord_t Link;

            // We have to lookup the link for current bucket
            if (MfsGetBucketLink(fileSystemBase, handle->DataBucketPosition, &Link) != OsOK) {
                ERROR("Failed to get link for bucket %u", handle->DataBucketPosition);
                osStatus = OsDeviceError;
                break;
            }

            // Check for EOL
            if (Link.Link == MFS_ENDOFCHAIN) {
                break;
            }
            handle->DataBucketPosition = Link.Link;

            // Lookup length of link
            if (MfsGetBucketLink(fileSystemBase, handle->DataBucketPosition, &Link) != OsOK) {
                ERROR("Failed to get length for bucket %u", handle->DataBucketPosition);
                osStatus = OsDeviceError;
                break;
            }
            handle->DataBucketLength = Link.Length;
            handle->BucketByteBoundary  += (Link.Length * bucketSizeBytes);
        }
    }

    // entry->modified = now
    entry->ActionOnClose = MFS_ACTION_UPDATE;
    return osStatus;
}

oscode_t
FsSeekInFile(
        _In_ FileSystemBase_t*      fileSystemBase,
        _In_ FileSystemEntryMFS_t*  entry,
        _In_ FileSystemHandleMFS_t* handle,
        _In_ uint64_t               absolutePosition)
{
    FileSystemMFS_t* mfs          = (FileSystemMFS_t*)fileSystemBase->ExtensionData;
    int              constantLoop = 1;
    size_t           initialBucketMax;

    TRACE("FsSeekInFile(Id 0x%x, Position 0x%x)", handle->Base.Id, LODWORD(absolutePosition));

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
        // if we are setting a new position that's 
        // within the current bucket
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
            uint64_t    PositionBoundLow  = 0;
            uint64_t    PositionBoundHigh = initialBucketMax;
            MapRecord_t Link;

            // Start at the file-bucket
            uint32_t BucketPtr    = entry->StartBucket;
            uint32_t BucketLength = entry->StartLength;
            while (constantLoop) {
                // Check if we reached correct bucket
                TRACE("[mfs] [seek] are we there yeti? [%llu-%llu]/%llu",
                      PositionBoundLow, PositionBoundLow + PositionBoundHigh, absolutePosition);
                
                // Check termination conditions
                if (absolutePosition >= PositionBoundLow && absolutePosition < (PositionBoundLow + PositionBoundHigh)) {
                    break;
                }

                // Get link
                if (MfsGetBucketLink(fileSystemBase, BucketPtr, &Link) != OsOK) {
                    ERROR("Failed to get link for bucket %u", BucketPtr);
                    return OsDeviceError;
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
                if (MfsGetBucketLink(fileSystemBase, BucketPtr, &Link) != OsOK) {
                    ERROR("Failed to get length for bucket %u", BucketPtr);
                    return OsDeviceError;
                }
                BucketLength = Link.Length;

                // Calculate bounds for the new bucket
                PositionBoundLow += PositionBoundHigh;
                PositionBoundHigh = (BucketLength * (mfs->SectorsPerBucket * fileSystemBase->Disk.descriptor.SectorSize));
            }

            // Update handle positioning
            handle->DataBucketPosition = BucketPtr;
            handle->DataBucketLength   = BucketLength;
            handle->BucketByteBoundary = PositionBoundLow;
        }
    }
    
    // Update the new position since everything went ok
    handle->Base.Position = absolutePosition;
    return OsOK;
}

oscode_t
FsChangeFileSize(
        _In_ FileSystemBase_t*      fileSystemBase,
        _In_ FileSystemEntryBase_t* entryBase,
        _In_ uint64_t               size)
{
    FileSystemEntryMFS_t* entry    = (FileSystemEntryMFS_t*)entryBase;
    oscode_t            osStatus = OsOK;

    TRACE("FsChangeFileSize(Name %s, Size 0x%x)", MStringRaw(entry->Base.Name), LODWORD(size));

    // Handle a special case of 0
    if (size == 0) {
        // Free all buckets allocated, if any are allocated
        if (entry->StartBucket != MFS_ENDOFCHAIN) {
            oscode_t Status = MfsFreeBuckets(fileSystemBase, entry->StartBucket, entry->StartLength);
            if (Status != OsOK) {
                ERROR("Failed to free the buckets at start 0x%x, length 0x%x. when truncating",
                      entry->StartBucket, entry->StartLength);
                osStatus = OsDeviceError;
            }
        }

        if (osStatus == OsOK) {
            entry->AllocatedSize = 0;
            entry->StartBucket   = MFS_ENDOFCHAIN;
            entry->StartLength   = 0;
        }
    }
    else {
        osStatus = MfsEnsureRecordSpace(fileSystemBase, entry, size);
    }

    if (osStatus == OsOK) {
        // entry->modified = now
        entry->Base.Descriptor.Size.QuadPart = size;
        entry->ActionOnClose                 = MFS_ACTION_UPDATE;
    }
    return osStatus;
}
