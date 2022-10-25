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
    if (oserr != OS_EOK) {
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
                return OS_EOOM;
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
    if (oserr != OS_EOK) {
        ERROR("MfsUpdateEntry Failed to update bucket %u", entry->DirectoryBucket);
    }

    // Cleanup and exit
Cleanup:
    return oserr;
}

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
        if (MFSBucketMapAllocate(mfs, bucketCount, &link) != OS_EOK) {
            ERROR("Failed to allocate %u buckets for file", bucketCount);
            return OS_EDEVFAULT;
        }

        // Now iterate to end
        bucketPointer         = entry->StartBucket;
        previousBucketPointer = MFS_ENDOFCHAIN;
        while (bucketPointer != MFS_ENDOFCHAIN) {
            previousBucketPointer = bucketPointer;
            if (MFSBucketMapGetLengthAndLink(mfs, bucketPointer, &iterator) != OS_EOK) {
                ERROR("MfsEnsureRecordSpace failed to get link for bucket %u", bucketPointer);
                return OS_EDEVFAULT;
            }
            bucketPointer = iterator.Link;
        }

        // We have a special case if previous == MFS_ENDOFCHAIN
        if (previousBucketPointer == MFS_ENDOFCHAIN) {
            // This means file had nothing allocated
            entry->StartBucket = link.Link;
            entry->StartLength = link.Length;
        } else {
            if (MFSBucketMapSetLinkAndLength(mfs, previousBucketPointer, link.Link, link.Length, true) != OS_EOK) {
                ERROR("Failed to set link for bucket %u", previousBucketPointer);
                return OS_EDEVFAULT;
            }
        }

        // Adjust the allocated-size of record
        entry->AllocatedSize += (bucketCount * bucketSizeBytes);
        entry->ActionOnClose  = MFS_ACTION_UPDATE;
    }
    return OS_EOK;
}

oserr_t
MFSCloneBucketData(
        _In_ FileSystemMFS_t* mfs,
        _In_ uint32_t         sourceBucket,
        _In_ uint32_t         sourceLength,
        _In_ uint32_t         destinationBucket,
        _In_ uint32_t         destinationLength)
{
    return OS_ENOTSUPPORTED;
}

oserr_t
MFSAdvanceToNextBucket(
        _In_ FileSystemMFS_t* mfs,
        _In_ MFSEntry_t*      entry,
        _In_ size_t           bucketSizeBytes)
{
    MapRecord_t link;
    uint32_t    nextDataBucketPosition;

    // We have to look up the link for current bucket
    if (MFSBucketMapGetLengthAndLink(mfs, entry->DataBucketPosition, &link) != OS_EOK) {
        ERROR("MFSAdvanceToNextBucket failed to get link for bucket %u", entry->DataBucketPosition);
        return OS_EDEVFAULT;
    }

    // Check for EOL
    if (link.Link == MFS_ENDOFCHAIN) {
        return OS_ENOENT;
    }
    nextDataBucketPosition = link.Link;

    // Lookup length of link
    if (MFSBucketMapGetLengthAndLink(mfs, entry->DataBucketPosition, &link) != OS_EOK) {
        ERROR("Failed to get length for bucket %u", entry->DataBucketPosition);
        return OS_EDEVFAULT;
    }

    // Store length & Update bucket boundary
    entry->DataBucketPosition = nextDataBucketPosition;
    entry->DataBucketLength   = link.Length;
    entry->BucketByteBoundary += (link.Length * bucketSizeBytes);
    return OS_EOK;
}
