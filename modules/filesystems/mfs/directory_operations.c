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

#include <os/utils.h>
#include <threads.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include "mfs.h"

/* FsReadFromDirectory 
 * Reads the requested number of directory entries. The Length must equal to a multiple of 
 * sizeof(struct DIRENT), where each of these represents an entry into the directory. */
FileSystemCode_t
FsReadFromDirectory(
    _In_  FileSystemDescriptor_t*   FileSystem,
    _In_  MfsEntryHandle_t*         Handle,
    _In_  DmaBuffer_t*              BufferObject,
    _In_  size_t                    Length,
    _Out_ size_t*                   BytesAt,
    _Out_ size_t*                   BytesRead)
{
    MfsInstance_t*      Mfs             = (MfsInstance_t*)FileSystem->ExtensionData;
    FileSystemCode_t    Result          = FsOk;
    size_t              BytesToRead     = Length;
    struct DIRENT*      CurrentEntry    = (struct DIRENT*)GetBufferDataPointer(BufferObject);

    TRACE("FsReadFromDirectory(Id 0x%x, Position %u, Length %u)",
        Handle->Base.Id, LODWORD(Handle->Base.Position), Length);

    *BytesRead      = 0;
    *BytesAt        = 0;

    if ((Length % sizeof(struct DIRENT)) != 0) {
        return FsInvalidParameters;
    }
    
    TRACE(" > dma: 0x%x, fpos %u, bytes-total %u, bytes-at %u", 
        DataPointer, LODWORD(Position), BytesToRead, *BytesAt);

    while (BytesToRead) {
        uint64_t    Sector      = MFS_GETSECTOR(Mfs, Handle->DataBucketPosition);
        size_t      Count       = MFS_GETSECTOR(Mfs, Handle->DataBucketLength);
        size_t      Offset      = Handle->Base.Position - Handle->BucketByteBoundary;
        uint8_t*    Data        = (uint8_t*)GetBufferDataPointer(Mfs->TransferBuffer);
        size_t      BucketSize  = Count * FileSystem->Disk.Descriptor.SectorSize;
        
        if (BucketSize > Offset) {
            // The code here is simple because we assume we can fit entire bucket at any time
            if (MfsReadSectors(FileSystem, Mfs->TransferBuffer, Sector, Count) != OsSuccess) {
                ERROR("Failed to read sector");
                Result = FsDiskError;
                break;
            }

            // Which position are we in?
            for (FileRecord_t* RecordPtr = (FileRecord_t*)(Data + Offset); (Offset < BucketSize) && BytesToRead; 
                RecordPtr++, CurrentEntry++, Offset += sizeof(FileRecord_t), 
                Handle->Base.Position += sizeof(FileRecord_t), BytesToRead -= sizeof(struct DIRENT)) {
                // Convert file record
                MfsFileRecordFlagsToVfsFlags(RecordPtr, &CurrentEntry->d_options, &CurrentEntry->d_perms);
                memcpy(&CurrentEntry->d_name[0], &RecordPtr->Name[0], 
                    MIN(sizeof(CurrentEntry->d_name), sizeof(RecordPtr->Name)));
            }
        }
        
        // Do we need to switch bucket?
        // We do if the position we have read to equals end of bucket
        if (Handle->Base.Position == (Handle->BucketByteBoundary + BucketSize)) {
            Result = MfsSwitchToNextBucketLink(FileSystem, Handle, BucketSize);
            if (Result == FsPathNotFound || Result != FsOk) {
                if (Result == FsPathNotFound) {
                    Result = FsOk;
                }
                break;
            }
        }
    }
    return Result;
}

/* FsSeekInDirectory 
 * Seeks to the directory entry number given. Seeks in directories are not governed by bytes, but rather
 * directory entries. So position = 1 is actually 1 * sizeof(entry) in the stored position */
FileSystemCode_t
FsSeekInDirectory(
    _In_ FileSystemDescriptor_t*    FileSystem,
    _In_ MfsEntryHandle_t*          Handle,
    _In_ uint64_t                   AbsolutePosition)
{
    MfsInstance_t*      Mfs             = (MfsInstance_t*)FileSystem->ExtensionData;
    MfsEntry_t*         Entry           = (MfsEntry_t*)Handle->Base.Entry;
    size_t              InitialBucketMax = 0;
    int                 ConstantLoop    = 1;
    uint64_t            ActualPosition  = AbsolutePosition * sizeof(struct DIRENT);

    // Trace
    TRACE("FsSeekInDirectory(Id 0x%x, Position 0x%x)", Handle->Id, LODWORD(AbsolutePosition));

    // Sanitize seeking bounds
    if (Entry->Base.Descriptor.Size.QuadPart == 0) {
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
        // if we are setting a new position that's within the current bucket
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
                BucketLength        = Link.Length;
                PositionBoundLow    += PositionBoundHigh;
                PositionBoundHigh   = (BucketLength * 
                    (Mfs->SectorsPerBucket * FileSystem->Disk.Descriptor.SectorSize));
            }

            // Update bucket pointer
            if (BucketPtr != MFS_ENDOFCHAIN) {
                Handle->DataBucketPosition = BucketPtr;
            }
        }
    }
    
    // Update the new position since everything went ok
    Handle->Base.Position = ActualPosition;
    return FsOk;
}
