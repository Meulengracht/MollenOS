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
#include <internal/_ipc.h>
#include "mfs.h"
#include <stdlib.h>
#include <string.h>

OsStatus_t
MfsReadSectors(
        _In_ FileSystemBase_t* fileSystemBase,
        _In_ UUId_t                  BufferHandle,
        _In_ size_t                  BufferOffset,
        _In_ uint64_t                Sector,
        _In_ size_t                  Count,
        _In_ size_t*                 SectorsRead)
{
	struct vali_link_message msg = VALI_MSG_INIT_HANDLE(fileSystemBase->Disk.driver_id);
	OsStatus_t               status;
    LargeUInteger_t          absoluteSector;

    absoluteSector.QuadPart = fileSystemBase->SectorStart + Sector;
	
	ctt_storage_transfer(GetGrachtClient(), &msg.base, fileSystemBase->Disk.device_id,
                         __STORAGE_OPERATION_READ, absoluteSector.u.LowPart, absoluteSector.u.HighPart,
                         BufferHandle, BufferOffset, Count);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
	ctt_storage_transfer_result(GetGrachtClient(), &msg.base, &status, SectorsRead);
	return status;
}

OsStatus_t
MfsWriteSectors(
        _In_ FileSystemBase_t* fileSystemBase,
        _In_ UUId_t                  BufferHandle,
        _In_ size_t                  BufferOffset,
        _In_ uint64_t                Sector,
        _In_ size_t                  Count,
        _In_ size_t*                 SectorsWritten)
{
	struct vali_link_message msg            = VALI_MSG_INIT_HANDLE(fileSystemBase->Disk.driver_id);
    uint64_t                 absoluteSector = fileSystemBase->SectorStart + Sector;
	OsStatus_t               status;
	
	ctt_storage_transfer(GetGrachtClient(), &msg.base, fileSystemBase->Disk.device_id,
                         __STORAGE_OPERATION_WRITE, LODWORD(absoluteSector), HIDWORD(absoluteSector),
                         BufferHandle, BufferOffset, Count);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
	ctt_storage_transfer_result(GetGrachtClient(), &msg.base, &status, SectorsWritten);
	return status;
}

OsStatus_t
MfsUpdateMasterRecord(
        _In_ FileSystemBase_t* FileSystem)
{
    FileSystemMFS_t* Mfs = (FileSystemMFS_t*)FileSystem->ExtensionData;
    size_t         SectorsTransferred;

    TRACE("MfsUpdateMasterRecord()");

    memset(Mfs->TransferBuffer.buffer, 0, FileSystem->Disk.descriptor.SectorSize);
    memcpy(Mfs->TransferBuffer.buffer, &Mfs->MasterRecord, sizeof(MasterRecord_t));

    if (MfsWriteSectors(FileSystem, Mfs->TransferBuffer.handle, 0, Mfs->MasterRecordSector, 1, &SectorsTransferred)       != OsSuccess || 
        MfsWriteSectors(FileSystem, Mfs->TransferBuffer.handle, 0, Mfs->MasterRecordMirrorSector, 1, &SectorsTransferred) != OsSuccess) {
        ERROR("Failed to write master-record to disk");
        return OsError;
    }
    return OsSuccess;
}

OsStatus_t
MfsGetBucketLink(
        _In_  FileSystemBase_t* fileSystemBase,
        _In_  uint32_t                bucket,
        _Out_ MapRecord_t*            link)
{
    FileSystemMFS_t* Mfs = (FileSystemMFS_t*)fileSystemBase->ExtensionData;

    TRACE("MfsGetBucketLink(Bucket %u)", bucket);
    if (bucket < Mfs->BucketCount) {
        link->Link   = Mfs->BucketMap[(bucket * 2)];
        link->Length = Mfs->BucketMap[(bucket * 2) + 1];
        TRACE("... link %u, length %u", link->Link, link->Length);
        return OsSuccess;
    }
    return OsInvalidParameters;
}

OsStatus_t 
MfsSetBucketLink(
        _In_ FileSystemBase_t*    fileSystemBase,
        _In_ uint32_t                   bucket,
        _In_ MapRecord_t*               link,
        _In_ int                        UpdateLength)
{
    FileSystemMFS_t*  Mfs                = (FileSystemMFS_t*)fileSystemBase->ExtensionData;
    uint8_t        *        BufferOffset = NULL;
    size_t          SectorOffset    = 0;
    size_t          SectorsTransferred;

    TRACE("MfsSetBucketLink(Bucket %u, Link %u)", bucket, link->Link);

    // Update in-memory map first
    Mfs->BucketMap[(bucket * 2)] = link->Link;
    if (UpdateLength) {
        Mfs->BucketMap[(bucket * 2) + 1] = link->Length;
    }

    // Calculate which sector that is dirty now
    SectorOffset = bucket / Mfs->BucketsPerSectorInMap;

    // Calculate offset into buffer
    BufferOffset = (uint8_t*)Mfs->BucketMap;
    BufferOffset += (SectorOffset * fileSystemBase->Disk.descriptor.SectorSize);

    // Copy a sector's worth of data into the buffer
    memcpy(Mfs->TransferBuffer.buffer, BufferOffset, fileSystemBase->Disk.descriptor.SectorSize);

    // Flush buffer to disk
    if (MfsWriteSectors(fileSystemBase, Mfs->TransferBuffer.handle, 0,
        Mfs->MasterRecord.MapSector + SectorOffset, 1, &SectorsTransferred) != OsSuccess) {
        ERROR("Failed to update the given map-sector %u on disk",
            LODWORD(Mfs->MasterRecord.MapSector + SectorOffset));
        return OsError;
    }
    return OsSuccess;
}

OsStatus_t
MfsSwitchToNextBucketLink(
        _In_ FileSystemBase_t* fileSystemBase,
        _In_ FileSystemHandleMFS_t*       handle,
        _In_ size_t                  bucketSizeBytes)
{
    MapRecord_t Link;
    uint32_t    NextDataBucketPosition;

    // We have to lookup the link for current bucket
    if (MfsGetBucketLink(fileSystemBase, handle->DataBucketPosition, &Link) != OsSuccess) {
        ERROR("Failed to get link for bucket %u", handle->DataBucketPosition);
        return OsDeviceError;
    }

    // Check for EOL
    if (Link.Link == MFS_ENDOFCHAIN) {
        return OsDoesNotExist;
    }
    NextDataBucketPosition = Link.Link;

    // Lookup length of link
    if (MfsGetBucketLink(fileSystemBase, handle->DataBucketPosition, &Link) != OsSuccess) {
        ERROR("Failed to get length for bucket %u", handle->DataBucketPosition);
        return OsDeviceError;
    }

    // Store length & Update bucket boundary
    handle->DataBucketPosition = NextDataBucketPosition;
    handle->DataBucketLength   = Link.Length;
    handle->BucketByteBoundary  += (Link.Length * bucketSizeBytes);
    return OsSuccess;
}

OsStatus_t
MfsAllocateBuckets(
        _In_ FileSystemBase_t*    fileSystemBase,
        _In_ size_t                     bucketCount,
        _In_ MapRecord_t*               mapRecord)
{
    FileSystemMFS_t*  Mfs = (FileSystemMFS_t*)fileSystemBase->ExtensionData;
    uint32_t        PreviousBucket  = 0;
    uint32_t        Bucket          = Mfs->MasterRecord.FreeBucket;
    size_t          Counter         = bucketCount;
    MapRecord_t     Record;

    TRACE("MfsAllocateBuckets(FreeAt %u, Count %u)", Bucket, bucketCount);

    mapRecord->Link   = Mfs->MasterRecord.FreeBucket;
    mapRecord->Length = 0;

    // Do allocation in a for-loop as bucket-sizes
    // are variable and thus we might need multiple
    // allocations to satisfy the demand
    while (Counter > 0) {
        // Get next free bucket
        if (MfsGetBucketLink(fileSystemBase, Bucket, &Record) != OsSuccess) {
            ERROR("Failed to retrieve link for bucket %u", Bucket);
            return OsError;
        }

        // Bucket points to the free bucket index
        // Record.Link holds the link of <Bucket>
        // Record.Length holds the length of <Bucket>

        // We now have two cases, either the next block is
        // larger than the number of buckets we are asking for
        // or it's smaller
        if (Record.Length > Counter) {
            // Ok, this block is larger than what we need
            // We now need to first, update the free index to these values
            // Map[Bucket] = (Counter) | (MFS_ENDOFCHAIN)
            // Map[Bucket + Counter] = (Length - Counter) | Link
            MapRecord_t Update, Next;

            // Set update
            Update.Link     = MFS_ENDOFCHAIN;
            Update.Length   = Counter;

            // Set next
            Next.Link       = Record.Link;
            Next.Length     = Record.Length - Counter;

            // Make sure only to update out once, we just need
            // the initial size, not for each new allocation
            if (mapRecord->Length == 0) {
                mapRecord->Length = Update.Length;
            }

            // We have to adjust now, since we are taking 
            // only a chunk of the available length
            // Map[Bucket] = (Counter) | (MFS_ENDOFCHAIN)
            // Map[Bucket + Counter] = (Length - Counter) | PreviousLink
            if (MfsSetBucketLink(fileSystemBase, Bucket, &Update, 1) != OsSuccess &&
                MfsSetBucketLink(fileSystemBase, Bucket + Counter, &Next, 1) != OsSuccess) {
                ERROR("Failed to update link for bucket %u and %u", 
                    Bucket, Bucket + Counter);
                return OsError;
            }
            Mfs->MasterRecord.FreeBucket = Bucket + Counter;
            return MfsUpdateMasterRecord(fileSystemBase);
        }
        else {
            // Ok, block is either exactly the size we need or less
            // than what we need

            // Make sure only to update out once, we just need
            // the initial size, not for each new allocation
            if (mapRecord->Length == 0) {
                mapRecord->Length = Record.Length;
            }
            Counter         -= Record.Length;
            PreviousBucket  = Bucket;
            Bucket          = Record.Link;
        }
    }

    // If we reach here it was because we encountered a block
    // that was exactly the fit we needed. So we set FreeIndex to Bucket
    // We set Record.Link to ENDOFCHAIN. We leave size unchanged

    // We want to update the last bucket of the chain but not update the length
    Record.Link = MFS_ENDOFCHAIN;

    // Update the previous bucket to MFS_ENDOFCHAIN
    if (MfsSetBucketLink(fileSystemBase, PreviousBucket, &Record, 0) != OsSuccess) {
        ERROR("Failed to update link for bucket %u", PreviousBucket);
        return OsError;
    }
    
    // Update the master-record and we are done
    Mfs->MasterRecord.FreeBucket = Bucket;
    return MfsUpdateMasterRecord(fileSystemBase);
}

/* MfsFreeBuckets
 * Frees an entire chain of buckets that has been allocated for a file-record */
OsStatus_t
MfsFreeBuckets(
        _In_ FileSystemBase_t*    fileSystemBase,
        _In_ uint32_t                   startBucket,
        _In_ uint32_t                   startLength)
{
    FileSystemMFS_t*  Mfs = (FileSystemMFS_t*)fileSystemBase->ExtensionData;
    uint32_t        PreviousBucket;
    MapRecord_t     Record;

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
    Record.Link = startBucket;

    // Start by iterating to the last bucket
    PreviousBucket = MFS_ENDOFCHAIN;
    while (Record.Link != MFS_ENDOFCHAIN) {
        PreviousBucket = Record.Link;
        if (MfsGetBucketLink(fileSystemBase, Record.Link, &Record) != OsSuccess) {
            ERROR("Failed to retrieve the next bucket-link");
            return OsError;
        }
    }

    // If there was no allocated buckets to start with then do nothing
    if (PreviousBucket != MFS_ENDOFCHAIN) {
        Record.Link = Mfs->MasterRecord.FreeBucket;

        // Ok, so now update the pointer to free list
        if (MfsSetBucketLink(fileSystemBase, PreviousBucket, &Record, 0)) {
            ERROR("Failed to update the next bucket-link");
            return OsError;
        }
        Mfs->MasterRecord.FreeBucket = startBucket;
        return MfsUpdateMasterRecord(fileSystemBase);
    }
    return OsSuccess;
}

/* MfsZeroBucket
 * Wipes the given bucket and count with zero values useful for clearing clusters of sectors */
OsStatus_t
MfsZeroBucket(
        _In_ FileSystemBase_t*    fileSystemBase,
        _In_ uint32_t                   bucket,
        _In_ size_t                     count)
{
    FileSystemMFS_t*  Mfs = (FileSystemMFS_t*)fileSystemBase->ExtensionData;
    size_t          i;
    size_t          SectorsTransferred;

    TRACE("MfsZeroBucket(Bucket %u, Count %u)", bucket, count);

    memset(Mfs->TransferBuffer.buffer, 0, Mfs->TransferBuffer.length);
    for (i = 0; i < count; i++) {
        // Calculate the sector
        uint64_t AbsoluteSector = MFS_GETSECTOR(Mfs, bucket + i);
        if (MfsWriteSectors(fileSystemBase, Mfs->TransferBuffer.handle, 0, AbsoluteSector,
                            Mfs->SectorsPerBucket, &SectorsTransferred) != OsSuccess) {
            ERROR("Failed to write bucket to disk");
            return OsError;
        }
    }
    return OsSuccess;
}

unsigned int
MfsVfsFlagsToFileRecordFlags(
    _In_ unsigned int flags,
    _In_ unsigned int permissions)
{
    unsigned int nativeFlags = 0;

    if (flags & __FILE_DIRECTORY) {
        nativeFlags |= MFS_FILERECORD_DIRECTORY;
    }
    else if (flags & __FILE_LINK) {
        nativeFlags |= MFS_FILERECORD_LINK;
    }
    return nativeFlags;
}

void
MfsFileRecordFlagsToVfsFlags(
    _In_  FileRecord_t* fileRecord,
    _Out_ unsigned int*      flags,
    _Out_ unsigned int*      permissions)
{
    // Permissions are not really implemented
    *permissions = (FILE_PERMISSION_READ | FILE_PERMISSION_WRITE | FILE_PERMISSION_EXECUTE);
    *flags       = 0;

    if (fileRecord->Flags & MFS_FILERECORD_DIRECTORY) {
        *flags |= FILE_FLAG_DIRECTORY;
    }
    else if (fileRecord->Flags & MFS_FILERECORD_LINK) {
        *flags |= FILE_FLAG_LINK;
    }
}

void
MfsFileRecordToVfsFile(
        _In_ FileSystemBase_t* fileSystemBase,
        _In_ FileRecord_t*           nativeEntry,
        _In_ FileSystemEntryMFS_t*             mfsEntry)
{
    // Skip the bucket placement and path
    mfsEntry->Base.Descriptor.StorageId     = (int)fileSystemBase->Disk.device_id; // ???
    // VfsEntry->Base.Descriptor.Id = ??
    mfsEntry->Base.Name                     = MStringCreate((const char*)&nativeEntry->Name[0], StrUTF8);
    mfsEntry->NativeFlags                   = nativeEntry->Flags;
    mfsEntry->Base.Descriptor.Size.QuadPart = nativeEntry->Size;
    mfsEntry->AllocatedSize                 = nativeEntry->AllocatedSize;
    mfsEntry->StartBucket                   = nativeEntry->StartBucket;
    mfsEntry->StartLength                   = nativeEntry->StartLength;

    // Convert flags to generic vfs flags and permissions
    MfsFileRecordFlagsToVfsFlags(nativeEntry,
                                 &mfsEntry->Base.Descriptor.Flags,
                                 &mfsEntry->Base.Descriptor.Permissions);

    // Convert dates
    // VfsEntry->Base.DescriptorCreatedAt;
    // VfsEntry->Base.DescriptorModifiedAt;
    // VfsEntry->Base.DescriptorAccessedAt;
}

OsStatus_t
MfsUpdateRecord(
        _In_ FileSystemBase_t* fileSystemBase,
        _In_ FileSystemEntryMFS_t*             entry,
        _In_ int                     action)
{
    FileSystemMFS_t* mfs = (FileSystemMFS_t*)fileSystemBase->ExtensionData;
    OsStatus_t     osStatus = OsSuccess;
    FileRecord_t*  record;
    size_t         sectorsTransferred;

    TRACE("MfsUpdateEntry(File %s)", MStringRaw(entry->Base.Name));

    // Read the stored data bucket where the record is
    if (MfsReadSectors(fileSystemBase, mfs->TransferBuffer.handle, 0,
                       MFS_GETSECTOR(mfs, entry->DirectoryBucket),
                       mfs->SectorsPerBucket * entry->DirectoryLength, &sectorsTransferred) != OsSuccess) {
        ERROR("Failed to read bucket %u", entry->DirectoryBucket);
        osStatus = OsDeviceError;
        goto Cleanup;
    }

    record = (FileRecord_t*)((uint8_t*)mfs->TransferBuffer.buffer + (sizeof(FileRecord_t) * entry->DirectoryIndex));

    // We have two over-all cases here, as create/modify share
    // some code, and that is delete as the second. If we delete
    // we zero out the entry and set the status to deleted
    if (action == MFS_ACTION_DELETE) {
        memset((void*)record, 0, sizeof(FileRecord_t));
    }
    else {
        // Now we have two sub cases, but create just needs some
        // extra updates otherwise they share
        if (action == MFS_ACTION_CREATE) {
            memset(&record->Integrated[0], 0, 512);
            memset(&record->Name[0], 0, 300);
            memcpy(&record->Name[0], MStringRaw(entry->Base.Name), MStringSize(entry->Base.Name));
        }

        // Update stats that are modifiable
        record->Flags       = entry->NativeFlags | MFS_FILERECORD_INUSE;
        record->StartBucket = entry->StartBucket;
        record->StartLength = entry->StartLength;

        // Update modified / accessed dates

        // Update sizes
        record->Size          = entry->Base.Descriptor.Size.QuadPart;
        record->AllocatedSize = entry->AllocatedSize;
    }
    
    // Write the bucket back to the disk
    if (MfsWriteSectors(fileSystemBase, mfs->TransferBuffer.handle, 0, MFS_GETSECTOR(mfs, entry->DirectoryBucket),
                        mfs->SectorsPerBucket * entry->DirectoryLength, &sectorsTransferred) != OsSuccess) {
        ERROR("Failed to update bucket %u", entry->DirectoryBucket);
        osStatus = OsDeviceError;
    }

    // Cleanup and exit
Cleanup:
    return osStatus;
}

/* MfsEnsureRecordSpace
 * Ensures that the given record has the space neccessary for the required data. */
OsStatus_t
MfsEnsureRecordSpace(
        _In_ FileSystemBase_t* fileSystemBase,
        _In_ FileSystemEntryMFS_t*             entry,
        _In_ uint64_t                spaceRequired)
{
    FileSystemMFS_t* Mfs = (FileSystemMFS_t*)fileSystemBase->ExtensionData;
    size_t         BucketSizeBytes = Mfs->SectorsPerBucket * fileSystemBase->Disk.descriptor.SectorSize;
    TRACE("MfsEnsureRecordSpace(%u)", LODWORD(spaceRequired));

    if (spaceRequired > entry->AllocatedSize) {
        // Calculate the number of sectors, then number of buckets
        size_t      NumSectors = (size_t)(DIVUP((spaceRequired - entry->AllocatedSize), fileSystemBase->Disk.descriptor.SectorSize));
        size_t      NumBuckets = DIVUP(NumSectors, Mfs->SectorsPerBucket);
        uint32_t    BucketPointer, PreviousBucketPointer;
        MapRecord_t Iterator, Link;

        // Perform the allocation of buckets
        if (MfsAllocateBuckets(fileSystemBase, NumBuckets, &Link) != OsSuccess) {
            ERROR("Failed to allocate %u buckets for file", NumBuckets);
            return OsDeviceError;
        }

        // Now iterate to end
        BucketPointer         = entry->StartBucket;
        PreviousBucketPointer = MFS_ENDOFCHAIN;
        while (BucketPointer != MFS_ENDOFCHAIN) {
            PreviousBucketPointer = BucketPointer;
            if (MfsGetBucketLink(fileSystemBase, BucketPointer, &Iterator) != OsSuccess) {
                ERROR("Failed to get link for bucket %u", BucketPointer);
                return OsDeviceError;
            }
            BucketPointer = Iterator.Link;
        }

        // We have a special case if previous == MFS_ENDOFCHAIN
        if (PreviousBucketPointer == MFS_ENDOFCHAIN) {
            // This means file had nothing allocated
            entry->StartBucket = Link.Link;
            entry->StartLength = Link.Length;
        }
        else {
            if (MfsSetBucketLink(fileSystemBase, PreviousBucketPointer, &Link, 1) != OsSuccess) {
                ERROR("Failed to set link for bucket %u", PreviousBucketPointer);
                return OsDeviceError;
            }
        }

        // Adjust the allocated-size of record
        entry->AllocatedSize += (NumBuckets * BucketSizeBytes);
        entry->ActionOnClose  = MFS_ACTION_UPDATE;
    }
    return OsSuccess;
}
