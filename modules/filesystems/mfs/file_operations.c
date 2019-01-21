/* MollenOS
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
 * MollenOS General File System (MFS) Driver
 *  - Contains the implementation of the MFS driver for mollenos
 */
//#define __TRACE

#include <ddk/utils.h>
#include <threads.h>
#include <stdlib.h>
#include <string.h>
#include "mfs.h"

/* FsReadFromFile 
 * Reads the requested number of units from the entry handle into the supplied buffer. This
 * can be handled differently based on the type of entry. */
FileSystemCode_t
FsReadFromFile(
    _In_  FileSystemDescriptor_t*   FileSystem,
    _In_  MfsEntryHandle_t*         Handle,
    _In_  DmaBuffer_t*              BufferObject,
    _In_  size_t                    Length,
    _Out_ size_t*                   BytesAt,
    _Out_ size_t*                   BytesRead)
{
    MfsInstance_t*      Mfs             = (MfsInstance_t*)FileSystem->ExtensionData;
    MfsEntry_t*         Entry           = (MfsEntry_t*)Handle->Base.Entry;
    FileSystemCode_t    Result          = FsOk;
    uintptr_t           DataPointer     = GetBufferDma(BufferObject);
    uint64_t            Position        = Handle->Base.Position;
    size_t              BucketSizeBytes = Mfs->SectorsPerBucket * FileSystem->Disk.Descriptor.SectorSize;
    size_t              BytesToRead     = Length;

    TRACE("FsReadFile(Id 0x%x, Position %u, Length %u)",
        Handle->Base.Id, LODWORD(Handle->Base.Position), Length);

    *BytesRead      = 0;
    *BytesAt        = Handle->Base.Position % FileSystem->Disk.Descriptor.SectorSize;

    // Sanitize the amount of bytes we want to read, cap it at bytes available
    if ((Position + BytesToRead) > Entry->Base.Descriptor.Size.QuadPart) {
        if (Position == Entry->Base.Descriptor.Size.QuadPart) {
            return FsOk;
        }
        BytesToRead = (size_t)(Entry->Base.Descriptor.Size.QuadPart - Position);
    }

    // Debug counter values
    TRACE(" > dma: 0x%x, fpos %u, bytes-total %u, bytes-at %u", DataPointer, 
        LODWORD(Position), BytesToRead, *BytesAt);

    // Read the current sector, update index to where data starts
    // Keep reading consecutive after that untill all bytes requested have
    // been read

    // Read in a loop to make sure we read all requested bytes
    while (BytesToRead) {
        // Calculate which bucket, then the sector offset
        // Then calculate how many sectors of the bucket we need to read
        uint64_t Sector         = MFS_GETSECTOR(Mfs, Handle->DataBucketPosition);        // Start-sector of current bucket
        uint64_t SectorOffset   = Position % FileSystem->Disk.Descriptor.SectorSize;        // Byte-offset into the current sector
        size_t SectorIndex      = (size_t)((Position - Handle->BucketByteBoundary) / FileSystem->Disk.Descriptor.SectorSize); // The sector-index into the current bucket
        size_t SectorsLeft      = MFS_GETSECTOR(Mfs, Handle->DataBucketLength) - SectorIndex; // How many sectors are left in this bucket
        size_t SectorCount;
        size_t SectorsFitInBuffer;
        size_t ByteCount;
        
        // Calculate the sector index into bucket
        Sector += SectorIndex;

        // Calculate how many sectors we should read in
        SectorCount         = DIVUP(BytesToRead, FileSystem->Disk.Descriptor.SectorSize);
        SectorsFitInBuffer  = (GetBufferSize(BufferObject) - *BytesRead) / FileSystem->Disk.Descriptor.SectorSize;
        if (SectorOffset != 0 && (SectorOffset + BytesToRead > FileSystem->Disk.Descriptor.SectorSize)) {
            SectorCount++; // Take into account the extra sector we have to read
        }

        // Adjust for bucket boundary, and adjust again for buffer size
        SectorCount = MIN(SectorCount, SectorsLeft);
        SectorCount = MIN(SectorCount, SectorsFitInBuffer);
        if (SectorCount == 0) {
            break;
        }

        // Adjust for number of bytes already consumed in the active sector
        ByteCount = MIN(BytesToRead, (SectorCount * FileSystem->Disk.Descriptor.SectorSize) - SectorOffset);

        // Ex pos 490 - length 50
        // SectorIndex = 0, SectorOffset = 490, SectorCount = 2 - ByteCount = 50 (Capacity 4096)
        // Ex pos 1109 - length 450
        // SectorIndex = 2, SectorOffset = 85, SectorCount = 2 - ByteCount = 450 (Capacity 4096)
        // Ex pos 490 - length 4000
        // SectorIndex = 0, SectorOffset = 490, SectorCount = 8 - ByteCount = 3606 (Capacity 4096)
        TRACE(" > sector %u (b-start %u, b-index %u), num-sectors %u, sector-byte-offset %u, bytecount %u",
            LODWORD(Sector), LODWORD(Sector) - SectorIndex, SectorIndex, SectorCount, LODWORD(SectorOffset), ByteCount);
        if ((GetBufferSize(BufferObject) - *BytesRead) < (SectorCount * FileSystem->Disk.Descriptor.SectorSize)) {
            WARNING(" > not enough room in buffer for transfer");
            break;
        }

        // Perform the read (Raw - as we need to pass the datapointer)
        if (StorageRead(FileSystem->Disk.Driver, FileSystem->Disk.Device, 
            FileSystem->SectorStart + Sector, DataPointer, SectorCount) != OsSuccess) {
            ERROR("Failed to read sector");
            Result = FsDiskError;
            break;
        }

        // Increase the pointers and decrease with bytes read
        DataPointer += FileSystem->Disk.Descriptor.SectorSize * SectorCount;
        *BytesRead  += ByteCount;
        Position    += ByteCount;
        BytesToRead -= ByteCount;

        // Do we need to switch bucket?
        // We do if the position we have read to equals end of bucket
        if (Position == (Handle->BucketByteBoundary + (Handle->DataBucketLength * BucketSizeBytes))) {
            Result = MfsSwitchToNextBucketLink(FileSystem, Handle, BucketSizeBytes);
            if (Result == FsPathNotFound || Result != FsOk) {
                if (Result == FsPathNotFound) {
                    Result = FsOk;
                }
                break;
            }
        }
    }

    // if (update_when_accessed) @todo
    // entry->accessed = now
    // entry->action_on_close = update

    TRACE(" > bytes read %u/%u", *BytesRead, Length);
    return Result;
}

/* FsWriteToFile 
 * Writes the requested number of bytes to the given
 * file handle and outputs the number of bytes actually written */
FileSystemCode_t
FsWriteToFile(
    _In_  FileSystemDescriptor_t*   FileSystem,
    _In_  MfsEntryHandle_t*         Handle,
    _In_  DmaBuffer_t*              BufferObject,
    _In_  size_t                    Length,
    _Out_ size_t*                   BytesWritten)
{
    MfsInstance_t*      Mfs             = (MfsInstance_t*)FileSystem->ExtensionData;
    MfsEntry_t*         Entry           = (MfsEntry_t*)Handle->Base.Entry;
    FileSystemCode_t    Result          = FsOk;
    uint64_t            Position        = Handle->Base.Position;
    size_t              BucketSizeBytes = Mfs->SectorsPerBucket * FileSystem->Disk.Descriptor.SectorSize;
    size_t              BytesToWrite    = Length;

    TRACE("FsWriteEntry(Id 0x%x, Position %u, Length %u)",
        Handle->Base.Id, LODWORD(Position), Length);

    *BytesWritten   = 0;
    Result          = MfsEnsureRecordSpace(FileSystem, Entry, Position + BytesToWrite);
    if (Result != FsOk) {
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
        uint64_t Sector         = MFS_GETSECTOR(Mfs, Handle->DataBucketPosition);
        uint64_t SectorOffset   = (Position - Handle->BucketByteBoundary) % FileSystem->Disk.Descriptor.SectorSize;
        size_t SectorIndex      = (size_t)((Position - Handle->BucketByteBoundary) / FileSystem->Disk.Descriptor.SectorSize);
        size_t SectorsLeft      = MFS_GETSECTOR(Mfs, Handle->DataBucketLength) - SectorIndex;
        size_t SectorCount      = 0, ByteCount = 0;

        // Ok - so sectorindex contains the index in the bucket
        // and sector offset contains the byte-offset in that sector

        // Calculate the sector index into bucket
        Sector += SectorIndex;

        // Calculate how many sectors we should read in
        SectorCount = DIVUP(BytesToWrite, FileSystem->Disk.Descriptor.SectorSize);

        // Do we cross a boundary?
        if (SectorOffset + BytesToWrite > FileSystem->Disk.Descriptor.SectorSize) {
            SectorCount++;
        }

        // Adjust for bucket boundary
        SectorCount = MIN(SectorsLeft, SectorCount);

        // Adjust for number of bytes read
        ByteCount = (size_t)MIN(BytesToWrite, (SectorCount * FileSystem->Disk.Descriptor.SectorSize) - SectorOffset);

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
        ZeroBuffer(Mfs->TransferBuffer);

        // Case 1 - Handle padding
        if (SectorOffset != 0 || ByteCount != FileSystem->Disk.Descriptor.SectorSize) {
            // Start building the sector
            if (MfsReadSectors(FileSystem, Mfs->TransferBuffer, Sector, SectorCount) != OsSuccess) {
                ERROR("Failed to read sector %u for combination step", 
                    LODWORD(Sector));
                Result = FsDiskError;
                break;
            }
        }

        // Now write the data to the sector
        SeekBuffer(Mfs->TransferBuffer, (size_t)SectorOffset);
        CombineBuffer(Mfs->TransferBuffer, BufferObject, ByteCount, NULL);

        // Perform the write (Raw - as we need to pass the datapointer)
        if (MfsWriteSectors(FileSystem, Mfs->TransferBuffer, Sector, SectorCount) != OsSuccess) {
            ERROR("Failed to write sector %u", LODWORD(Sector));
            Result = FsDiskError;
            break;
        }

        // Increase the pointers and decrease with bytes read
        Position        += ByteCount;
        *BytesWritten   += ByteCount;
        BytesToWrite    -= ByteCount;

        // Do we need to switch bucket?
        // We do if the position we have read to equals end of bucket
        if (Position == (Handle->BucketByteBoundary + (Handle->DataBucketLength * BucketSizeBytes))) {
            MapRecord_t Link;

            // We have to lookup the link for current bucket
            if (MfsGetBucketLink(FileSystem, Handle->DataBucketPosition, &Link) != OsSuccess) {
                ERROR("Failed to get link for bucket %u", Handle->DataBucketPosition);
                Result = FsDiskError;
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
                Result = FsDiskError;
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

/* FsSeekInFile 
 * Seeks in the given entry-handle to the absolute position
 * given, must be within boundaries otherwise a seek won't take a place */
FileSystemCode_t
FsSeekInFile(
    _In_ FileSystemDescriptor_t*    FileSystem,
    _In_ MfsEntryHandle_t*          Handle,
    _In_ uint64_t                   AbsolutePosition)
{
    MfsInstance_t*  Mfs                 = (MfsInstance_t*)FileSystem->ExtensionData;
    MfsEntry_t*     Entry               = (MfsEntry_t*)Handle->Base.Entry;
    size_t          InitialBucketMax    = 0;
    int             ConstantLoop        = 1;

    TRACE("FsSeekInFile(Id 0x%x, Position 0x%x)", Handle->Base.Id, LODWORD(AbsolutePosition));

    // Sanitize seeking bounds
    if ((AbsolutePosition > Entry->Base.Descriptor.Size.QuadPart)) {
        return FsInvalidParameters;
    }

    // Step 1, if the new position is in
    // initial bucket, we need to do no actual seeking
    InitialBucketMax = (Entry->StartLength * (Mfs->SectorsPerBucket * FileSystem->Disk.Descriptor.SectorSize));
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
        OldBucketLow    = Handle->BucketByteBoundary;
        OldBucketHigh   = OldBucketLow + (Handle->DataBucketLength 
            * (Mfs->SectorsPerBucket * FileSystem->Disk.Descriptor.SectorSize));

        // If we are seeking inside the same bucket no need
        // to do anything else
        if (AbsolutePosition >= OldBucketLow && AbsolutePosition < OldBucketHigh) {
            // Same bucket
        }
        else {
            // We need to figure out which bucket the position is in
            uint64_t PositionBoundLow   = 0;
            uint64_t PositionBoundHigh  = InitialBucketMax;
            MapRecord_t Link;

            // Start at the file-bucket
            uint32_t BucketPtr      = Entry->StartBucket;
            uint32_t BucketLength   = Entry->StartLength;
            while (ConstantLoop) {
                // Check if we reached correct bucket
                if (AbsolutePosition >= PositionBoundLow
                    && AbsolutePosition < (PositionBoundLow + PositionBoundHigh)) {
                    Handle->BucketByteBoundary = PositionBoundLow;
                    break;
                }

                // Get link
                if (MfsGetBucketLink(FileSystem, BucketPtr, &Link) != OsSuccess) {
                    ERROR("Failed to get link for bucket %u", BucketPtr);
                    return FsDiskError;
                }

                // If we do reach end of chain, something went terribly wrong
                if (Link.Link == MFS_ENDOFCHAIN) {
                    ERROR("Reached end of chain during seek");
                    return FsInvalidParameters;
                }
                BucketPtr = Link.Link;

                // Get length of link
                if (MfsGetBucketLink(FileSystem, BucketPtr, &Link) != OsSuccess) {
                    ERROR("Failed to get length for bucket %u", BucketPtr);
                    return FsDiskError;
                }
                BucketLength = Link.Length;

                // Calculate bounds for the new bucket
                PositionBoundLow += PositionBoundHigh;
                PositionBoundHigh = (BucketLength * 
                    (Mfs->SectorsPerBucket * FileSystem->Disk.Descriptor.SectorSize));
            }

            // Update bucket pointer
            if (BucketPtr != MFS_ENDOFCHAIN) {
                Handle->DataBucketPosition = BucketPtr;
            }
        }
    }
    
    // Update the new position since everything went ok
    Handle->Base.Position = AbsolutePosition;
    return FsOk;
}

/* FsChangeFileSize 
 * Either expands or shrinks the allocated space for the given
 * file-handle to the requested size. */
FileSystemCode_t
FsChangeFileSize(
    _In_ FileSystemDescriptor_t*    FileSystem,
    _In_ FileSystemEntry_t*         BaseEntry,
    _In_ uint64_t                   Size)
{
    MfsEntry_t*         Entry   = (MfsEntry_t*)BaseEntry;
    FileSystemCode_t    Code    = FsOk;

    TRACE("FsChangeFileSize(Name %s, Size 0x%x)", MStringRaw(Entry->Base.Name), LODWORD(Size));

    // Handle a special case of 0
    if (Size == 0) {
        // Free all buckets allocated, if any are allocated
        if (Entry->StartBucket != MFS_ENDOFCHAIN) {
            OsStatus_t Status = MfsFreeBuckets(FileSystem, Entry->StartBucket, Entry->StartLength);
            if (Status != OsSuccess) {
                ERROR("Failed to free the buckets at start 0x%x, length 0x%x. when truncating",
                    Entry->StartBucket, Entry->StartLength);
                Code = FsDiskError;
            }
        }

        if (Code == FsOk) {
            Entry->AllocatedSize = 0;
            Entry->StartBucket   = MFS_ENDOFCHAIN;
            Entry->StartLength   = 0;
        }
    }
    else {
        Code = MfsEnsureRecordSpace(FileSystem, Entry, Size);
    }

    if (Code == FsOk) {
        // entry->modified = now
        Entry->Base.Descriptor.Size.QuadPart    = Size;
        Entry->ActionOnClose                    = MFS_ACTION_UPDATE;
    }
    return Code;
}
