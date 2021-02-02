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
 * General File System (MFS) Driver
 *  - Contains the implementation of the MFS driver for mollenos
 */
//#define __TRACE

#include <ddk/utils.h>
#include <threads.h>
#include <stdlib.h>
#include <string.h>
#include "mfs.h"

OsStatus_t
FsReadFromFile(
    _In_  FileSystemDescriptor_t* FileSystem,
    _In_  MfsEntryHandle_t*       Handle,
    _In_  UUId_t                  BufferHandle,
    _In_  void*                   Buffer,
    _In_  size_t                  BufferOffset,
    _In_  size_t                  UnitCount,
    _Out_ size_t*                 UnitsRead)
{
    MfsInstance_t* Mfs             = (MfsInstance_t*)FileSystem->ExtensionData;
    MfsEntry_t*    Entry           = (MfsEntry_t*)Handle->Base.Entry;
    OsStatus_t     Result          = OsSuccess;
    uint64_t       Position        = Handle->Base.Position;
    size_t         BucketSizeBytes = Mfs->SectorsPerBucket * FileSystem->Disk.descriptor.SectorSize;
    size_t         BytesToRead     = UnitCount;

    TRACE("[mfs] [read_file] id 0x%x, position %u, length %u",
        Handle->Base.Id, LODWORD(Handle->Base.Position), LODWORD(UnitCount));

    // Zero this first to indicate no bytes read
    *UnitsRead = 0;

    // Sanitize the amount of bytes we want to read, cap it at bytes available
    if ((Position + BytesToRead) > Entry->Base.Descriptor.Size.QuadPart) {
        if (Position >= Entry->Base.Descriptor.Size.QuadPart) {
            return OsSuccess;
        }
        BytesToRead = (size_t)(Entry->Base.Descriptor.Size.QuadPart - Position);
    }

    if (Handle->DataBucketPosition == MFS_ENDOFCHAIN) {
        // No buckets allocated for this file. Guard against this as there are two cases;
        // Case 1 - Newly created file, return OsSuccess;
        if (Entry->Base.Descriptor.Size.QuadPart == 0) {
            return OsSuccess;
        }
        else {
            // Read from integrated data
            // TODO
        }
    }
    
    // Debug counter values
    TRACE(" > dma: 0x%x, fpos %u, bytes-total %u, bytes-at %u", DataPointer, 
        LODWORD(Position), BytesToRead, *BytesAt);

    // Read the current sector, update index to where data starts
    // Keep reading consecutive after that untill all bytes requested have
    // been read
    while (BytesToRead) {
        // Calculate which bucket, then the sector offset
        // Then calculate how many sectors of the bucket we need to read
        uint64_t Sector       = MFS_GETSECTOR(Mfs, Handle->DataBucketPosition);    // Start-sector of current bucket
        uint64_t SectorOffset = Position % FileSystem->Disk.descriptor.SectorSize; // Byte-offset into the current sector
        size_t   SectorIndex  = (size_t)((Position - Handle->BucketByteBoundary) / FileSystem->Disk.descriptor.SectorSize); // The sector-index into the current bucket
        size_t   SectorsLeft  = MFS_GETSECTOR(Mfs, Handle->DataBucketLength) - SectorIndex; // How many sectors are left in this bucket
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
        if (SectorOffset == 0 && BytesToRead >= FileSystem->Disk.descriptor.SectorSize) {
            SectorCount    = BytesToRead / FileSystem->Disk.descriptor.SectorSize;
            SelectedHandle = BufferHandle;
            SelectedOffset = BufferOffset;
        }
        
        // CASE 2: SINGLE READ INTO INTERMEDIATE BUFFER
        // We want this to happen when we can fit the entire read into our fs transfer buffer
        // that can act as an intermediate buffer. So calculate enough space for <BytesToRead>, with
        // room for <SectorOffset> and also the spill-over bytes thats left for the sector
        else if ((SectorOffset + BytesToRead + (FileSystem->Disk.descriptor.SectorSize -
                    ((SectorOffset + BytesToRead) % FileSystem->Disk.descriptor.SectorSize))) <=
                        Mfs->TransferBuffer.length) {
            SectorCount = DIVUP(BytesToRead, FileSystem->Disk.descriptor.SectorSize);
            if (SectorOffset != 0 && (SectorOffset + BytesToRead > FileSystem->Disk.descriptor.SectorSize)) {
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
            ByteCount = MIN(BytesToRead, (SectorCount * FileSystem->Disk.descriptor.SectorSize) - SectorOffset);
    
            // Ex pos 490 - length 50
            // SectorIndex = 0, SectorOffset = 490, SectorCount = 2 - ByteCount = 50 (Capacity 4096)
            // Ex pos 1109 - length 450
            // SectorIndex = 2, SectorOffset = 85, SectorCount = 2 - ByteCount = 450 (Capacity 4096)
            // Ex pos 490 - length 4000
            // SectorIndex = 0, SectorOffset = 490, SectorCount = 8 - ByteCount = 3606 (Capacity 4096)
            TRACE(" > sector %u (b-start %u, b-index %u), num-sectors %u, sector-byte-offset %u, bytecount %u",
                LODWORD(Sector), LODWORD(Sector) - SectorIndex, SectorIndex, SectorCount, LODWORD(SectorOffset), ByteCount);
    
            if (MfsReadSectors(FileSystem, SelectedHandle, SelectedOffset, 
                    Sector, SectorCount, &SectorsRead) != OsSuccess) {
                ERROR("Failed to read sector");
                Result = OsDeviceError;
                break;
            }
            
            // Adjust for how many sectors we actually read
            if (SectorCount != SectorsRead) {
                ByteCount = (FileSystem->Disk.descriptor.SectorSize * SectorsRead) - SectorOffset;
            }
            
            // If we used the intermediate buffer for the transfer we now have to copy
            // <ByteCount> amount of bytes from <TransferBuffer> + <SectorOffset> to <Buffer> + <BufferOffset>
            if (SelectedHandle == Mfs->TransferBuffer.handle) {
                memcpy(((uint8_t*)Buffer + BufferOffset), ((uint8_t*)Mfs->TransferBuffer.buffer + SectorOffset), ByteCount);
            }
            
            // Increament all read-state variables
            *UnitsRead   += ByteCount;
            BufferOffset += ByteCount;
            Position     += ByteCount;
            BytesToRead  -= ByteCount;            
        }

        // Do we need to switch bucket?
        // We do if the position we have read to equals end of bucket
        if (Position == (Handle->BucketByteBoundary + (Handle->DataBucketLength * BucketSizeBytes))) {
            Result = MfsSwitchToNextBucketLink(FileSystem, Handle, BucketSizeBytes);
            if (Result != OsSuccess) {
                if (Result == OsDoesNotExist) {
                    Result = OsSuccess;
                }
                break;
            }
        }
    }

    // if (update_when_accessed) @todo
    // entry->accessed = now
    // entry->action_on_close = update
    TRACE("[mfs] [read_file] bytes read %u/%u", *UnitsRead, UnitCount);
    return Result;
}

OsStatus_t
FsWriteToFile(
    _In_  FileSystemDescriptor_t* FileSystem,
    _In_  MfsEntryHandle_t*       Handle,
    _In_  UUId_t                  BufferHandle,
    _In_  void*                   Buffer,
    _In_  size_t                  BufferOffset,
    _In_  size_t                  UnitCount,
    _Out_ size_t*                 UnitsWritten)
{
    MfsInstance_t* Mfs             = (MfsInstance_t*)FileSystem->ExtensionData;
    MfsEntry_t*    Entry           = (MfsEntry_t*)Handle->Base.Entry;
    OsStatus_t     Result          = OsSuccess;
    uint64_t       Position        = Handle->Base.Position;
    size_t         BucketSizeBytes = Mfs->SectorsPerBucket * FileSystem->Disk.descriptor.SectorSize;
    size_t         BytesToWrite    = UnitCount;

    TRACE("FsWriteEntry(Id 0x%x, Position %u, Length %u)",
        Handle->Base.Id, LODWORD(Position), UnitCount);

    // Set 0 to start out with, in case of errors we want to indicate correctly.
    *UnitsWritten = 0;
    
    // We do not have the same boundary limits here as we do when reading, when we
    // write to a file we can do so untill we run out of space on the filesystem.
    Result = MfsEnsureRecordSpace(FileSystem, Entry, Position + BytesToWrite);
    if (Result != OsSuccess) {
        return Result;
    }

    // Guard against newly allocated files
    if (Handle->DataBucketPosition == MFS_ENDOFCHAIN) {
        Handle->DataBucketPosition  = Entry->StartBucket;
        Handle->DataBucketLength    = Entry->StartLength;
        Handle->BucketByteBoundary  = 0;
    }
    
    // Write in a loop to make sure we write all requested bytes
    while (BytesToWrite) {
        // Calculate which bucket, then the sector offset
        // Then calculate how many sectors of the bucket we need to read
        uint64_t Sector       = MFS_GETSECTOR(Mfs, Handle->DataBucketPosition);
        uint64_t SectorOffset = (Position - Handle->BucketByteBoundary) % FileSystem->Disk.descriptor.SectorSize;
        size_t   SectorIndex  = (size_t)((Position - Handle->BucketByteBoundary) / FileSystem->Disk.descriptor.SectorSize);
        size_t   SectorsLeft  = MFS_GETSECTOR(Mfs, Handle->DataBucketLength) - SectorIndex;
        size_t   SectorCount;
        size_t   SectorsWritten;
        size_t   ByteCount;
        
        // The buffer handle + offset that was selected for writing 
        UUId_t SelectedHandle = Mfs->TransferBuffer.handle;
        size_t SelectedOffset = 0;

        // Calculate the sector index into bucket
        Sector += SectorIndex;
        
        // CASE 1: WE CAN WRITE DIRECTLY FROM USER-BUFFER TO DISK
        // If <SectorOffset> is 0, this means we can write directly to the disk
        // from <Buffer> + <BufferOffset>. We must also be able to write an entire
        // sector to avoid writing out of bounds from the buffer
        if (SectorOffset == 0 && BytesToWrite >= FileSystem->Disk.descriptor.SectorSize) {
            SectorCount    = BytesToWrite / FileSystem->Disk.descriptor.SectorSize;
            SelectedHandle = BufferHandle;
            SelectedOffset = BufferOffset;
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
            ByteCount = MIN(BytesToWrite, (SectorCount * FileSystem->Disk.descriptor.SectorSize) - SectorOffset);
    
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
            if (SelectedHandle == Mfs->TransferBuffer.handle) {
                memset(Mfs->TransferBuffer.buffer, 0, Mfs->TransferBuffer.length);
                
                // CASE READ-WRITE: We do this to support appending to sectors and overwriting
                // bytes in a sector. This must occur when we either have a <SectorOffset> != 0
                // or when the <SectorOffset> == 0 and <ByteCount> is less than a sector. 
                if (MfsReadSectors(FileSystem, Mfs->TransferBuffer.handle, 0, 
                        Sector, SectorCount, &SectorsWritten) != OsSuccess) {
                    ERROR("Failed to read sector %u for combination step", 
                        LODWORD(Sector));
                    Result = OsDeviceError;
                    break;
                }
                
                // Now perform the user copy operation where we overwrite some of the data
                // Copy from <Buffer> + <BufferOffset> to <TransferBuffer> + <SectorOffset>
                memcpy(((uint8_t*)Mfs->TransferBuffer.buffer + SectorOffset), ((uint8_t*)Buffer + BufferOffset), ByteCount);
            }
            
            // Write either the intermediate buffer or directly from user
            if (MfsWriteSectors(FileSystem, SelectedHandle, SelectedOffset,
                    Sector, SectorCount, &SectorsWritten) != OsSuccess) {
                ERROR("Failed to write sector %u", LODWORD(Sector));
                Result = OsDeviceError;
                break;
            }
            
            // Adjust for how many sectors we actually read
            if (SectorCount != SectorsWritten) {
                ByteCount = (FileSystem->Disk.descriptor.SectorSize * SectorsWritten) - SectorOffset;
            }
            
            *UnitsWritten += ByteCount;
            BufferOffset  += ByteCount;
            Position      += ByteCount;
            BytesToWrite  -= ByteCount;            
        }

        // Do we need to switch bucket?
        // We do if the position we have read to equals end of bucket
        if (Position == (Handle->BucketByteBoundary + (Handle->DataBucketLength * BucketSizeBytes))) {
            MapRecord_t Link;

            // We have to lookup the link for current bucket
            if (MfsGetBucketLink(FileSystem, Handle->DataBucketPosition, &Link) != OsSuccess) {
                ERROR("Failed to get link for bucket %u", Handle->DataBucketPosition);
                Result = OsDeviceError;
                break;
            }

            // Check for EOL
            if (Link.Link == MFS_ENDOFCHAIN) {
                break;
            }
            Handle->DataBucketPosition = Link.Link;

            // Lookup length of link
            if (MfsGetBucketLink(FileSystem, Handle->DataBucketPosition, &Link) != OsSuccess) {
                ERROR("Failed to get length for bucket %u", Handle->DataBucketPosition);
                Result = OsDeviceError;
                break;
            }
            Handle->DataBucketLength    = Link.Length;
            Handle->BucketByteBoundary  += (Link.Length * BucketSizeBytes);
        }
    }

    // entry->modified = now
    Entry->ActionOnClose = MFS_ACTION_UPDATE;
    return Result;
}

OsStatus_t
FsSeekInFile(
    _In_ FileSystemDescriptor_t* FileSystem,
    _In_ MfsEntryHandle_t*       Handle,
    _In_ uint64_t                AbsolutePosition)
{
    MfsInstance_t* Mfs              = (MfsInstance_t*)FileSystem->ExtensionData;
    MfsEntry_t*    Entry            = (MfsEntry_t*)Handle->Base.Entry;
    size_t         InitialBucketMax = 0;
    int            ConstantLoop     = 1;

    TRACE("FsSeekInFile(Id 0x%x, Position 0x%x)", Handle->Base.Id, LODWORD(AbsolutePosition));

    // Step 1, if the new position is in
    // initial bucket, we need to do no actual seeking
    InitialBucketMax = (Entry->StartLength * (Mfs->SectorsPerBucket * FileSystem->Disk.descriptor.SectorSize));
    if (AbsolutePosition < InitialBucketMax) {
        Handle->DataBucketPosition   = Entry->StartBucket;
        Handle->DataBucketLength     = Entry->StartLength;
        Handle->BucketByteBoundary   = 0;
    }
    else {
        // Step 2. We might still get out easy
        // if we are setting a new position that's 
        // within the current bucket
        uint64_t OldBucketLow, OldBucketHigh;

        // Calculate bucket boundaries
        OldBucketLow  = Handle->BucketByteBoundary;
        OldBucketHigh = OldBucketLow + (Handle->DataBucketLength 
            * (Mfs->SectorsPerBucket * FileSystem->Disk.descriptor.SectorSize));

        // If we are seeking inside the same bucket no need
        // to do anything else
        if (AbsolutePosition >= OldBucketLow && AbsolutePosition < OldBucketHigh) {
            // Same bucket
        }
        else {
            // We need to figure out which bucket the position is in
            uint64_t    PositionBoundLow  = 0;
            uint64_t    PositionBoundHigh = InitialBucketMax;
            MapRecord_t Link;

            // Start at the file-bucket
            uint32_t BucketPtr    = Entry->StartBucket;
            uint32_t BucketLength = Entry->StartLength;
            while (ConstantLoop) {
                // Check if we reached correct bucket
                TRACE("[mfs] [seek] are we there yeti? [%llu-%llu]/%llu",
                    PositionBoundLow, PositionBoundLow + PositionBoundHigh, AbsolutePosition);
                
                // Check termination conditions
                if (AbsolutePosition >= PositionBoundLow && AbsolutePosition < (PositionBoundLow + PositionBoundHigh)) {
                    break;
                }

                // Get link
                if (MfsGetBucketLink(FileSystem, BucketPtr, &Link) != OsSuccess) {
                    ERROR("Failed to get link for bucket %u", BucketPtr);
                    return OsDeviceError;
                }

                // If we do reach end of chain, then we handle this case by setting
                // the pointer to the wished destination, and store the last indices
                if (Link.Link == MFS_ENDOFCHAIN) {
                    WARNING("[mfs] [seek] seeking beyond eof [%llu-%llu]/%llu",
                        PositionBoundLow, PositionBoundLow + PositionBoundHigh, AbsolutePosition);
                    break;
                }
                
                BucketPtr = Link.Link;

                // Get length of link
                if (MfsGetBucketLink(FileSystem, BucketPtr, &Link) != OsSuccess) {
                    ERROR("Failed to get length for bucket %u", BucketPtr);
                    return OsDeviceError;
                }
                BucketLength = Link.Length;

                // Calculate bounds for the new bucket
                PositionBoundLow += PositionBoundHigh;
                PositionBoundHigh = (BucketLength * (Mfs->SectorsPerBucket * FileSystem->Disk.descriptor.SectorSize));
            }

            // Update handle positioning
            Handle->DataBucketPosition = BucketPtr;
            Handle->DataBucketLength   = BucketLength;
            Handle->BucketByteBoundary = PositionBoundLow;
        }
    }
    
    // Update the new position since everything went ok
    Handle->Base.Position = AbsolutePosition;
    return OsSuccess;
}

OsStatus_t
FsChangeFileSize(
    _In_ FileSystemDescriptor_t* FileSystem,
    _In_ FileSystemEntry_t*      BaseEntry,
    _In_ uint64_t                Size)
{
    MfsEntry_t* Entry = (MfsEntry_t*)BaseEntry;
    OsStatus_t  Code  = OsSuccess;

    TRACE("FsChangeFileSize(Name %s, Size 0x%x)", MStringRaw(Entry->Base.Name), LODWORD(Size));

    // Handle a special case of 0
    if (Size == 0) {
        // Free all buckets allocated, if any are allocated
        if (Entry->StartBucket != MFS_ENDOFCHAIN) {
            OsStatus_t Status = MfsFreeBuckets(FileSystem, Entry->StartBucket, Entry->StartLength);
            if (Status != OsSuccess) {
                ERROR("Failed to free the buckets at start 0x%x, length 0x%x. when truncating",
                    Entry->StartBucket, Entry->StartLength);
                Code = OsDeviceError;
            }
        }

        if (Code == OsSuccess) {
            Entry->AllocatedSize = 0;
            Entry->StartBucket   = MFS_ENDOFCHAIN;
            Entry->StartLength   = 0;
        }
    }
    else {
        Code = MfsEnsureRecordSpace(FileSystem, Entry, Size);
    }

    if (Code == OsSuccess) {
        // entry->modified = now
        Entry->Base.Descriptor.Size.QuadPart    = Size;
        Entry->ActionOnClose                    = MFS_ACTION_UPDATE;
    }
    return Code;
}
