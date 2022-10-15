/**
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
 */

//#define __TRACE
#define CACHE_SEGMENTED

#include <ddk/utils.h>
#include <fs/common.h>
#include <os/types/file.h>
#include <stdlib.h>
#include <string.h>
#include "mfs.h"

static const char* RootEntryName = "<root>";

// File specific operation handlers
oserr_t FsReadFromFile(FileSystemMFS_t*, MFSEntry_t*, uuid_t, void*, size_t, size_t, size_t*);
oserr_t FsWriteToFile(FileSystemMFS_t*, MFSEntry_t*, uuid_t, void*, size_t, size_t, size_t*);
oserr_t FsSeekInFile(FileSystemMFS_t*, MFSEntry_t*, uint64_t);

// Directory specific operation handlers
oserr_t FsReadFromDirectory(FileSystemMFS_t*, MFSEntry_t*, void*, size_t, size_t, size_t*);
oserr_t FsSeekInDirectory(FileSystemMFS_t*, MFSEntry_t*, uint64_t);

static MFSEntry_t* MFSEntryNew(void)
{
    MFSEntry_t* mfsEntry;

    mfsEntry = (MFSEntry_t*)malloc(sizeof(MFSEntry_t));
    if (!mfsEntry) {
        return NULL;
    }

    memset(mfsEntry, 0, sizeof(MFSEntry_t));
    return mfsEntry;
}

static void MFSEntryDelete(MFSEntry_t* entry)
{
    mstr_delete(entry->Name);
    free(entry);
}

oserr_t
FsOpen(
        _In_      void*      instanceData,
        _In_      mstring_t* path,
        _Out_Opt_ void**     dataOut)
{
    FileSystemMFS_t* mfs = instanceData;
    oserr_t          osStatus;
    MFSEntry_t*      mfsEntry;
    WARNING("FsOpen=(path=%ms)", path);

    osStatus = MfsLocateRecord(
            mfs,
            &mfs->RootEntry,
            path,
            &mfsEntry);
    if (osStatus != OsOK) {
        return osStatus;
    }

    *dataOut = mfsEntry;
    return osStatus;
}

oserr_t
FsCreate(
        _In_  void*      instanceData,
        _In_  void*      data,
        _In_  mstring_t* name,
        _In_  uint32_t   owner,
        _In_  uint32_t   flags,
        _In_  uint32_t   permissions,
        _Out_ void**     dataOut)
{
    oserr_t     osStatus;
    MFSEntry_t* entry = (MFSEntry_t*)data;
    MFSEntry_t* result;
    WARNING("FsCreate(parent=%ms, name=%ms)", entry ? entry->Name : NULL, name);

    osStatus = MfsCreateRecord(
            instanceData,
            entry,
            name,
            owner,
            flags,
            permissions,
            &result
    );
    if (osStatus != OsOK) {
        return osStatus;
    }
    WARNING("FsCreate returned %u (0x%llx) %ms", osStatus, result, result->Name);
    *dataOut = result;
    return osStatus;
}

oserr_t
FsClose(
        _In_ void* instanceData,
        _In_ void* data)
{
    MFSEntry_t* entry    = (MFSEntry_t*)data;
    oserr_t     osStatus = OsOK;
    WARNING("FsClose(data=0x%llx)", data);

    WARNING("FsClose(%i)", entry->ActionOnClose);
    if (entry->ActionOnClose) {
        osStatus = MfsUpdateRecord(instanceData, entry, entry->ActionOnClose);
    }
    mstr_delete(entry->Name);
    free(entry);
    return osStatus;
}

oserr_t
FsStat(
        _In_ void*             instanceData,
        _In_ struct VFSStatFS* stat)
{
    FileSystemMFS_t* mfs = instanceData;

    stat->Label = mfs->Label;
    stat->MaxFilenameLength = sizeof(mfs->RootRecord.Name);
    stat->BlockSize = mfs->SectorSize;
    stat->BlocksPerSegment = mfs->SectorsPerBucket;
    stat->SegmentsTotal = mfs->BucketsInMap;
    stat->SegmentsFree = mfs->MasterRecord.FreeBucket; // TODO we don't know how many free
    return OsOK;
}

oserr_t
FsUnlink(
        _In_  void*     instanceData,
        _In_ mstring_t* path)
{
    FileSystemMFS_t* mfs = instanceData;
    oserr_t          osStatus;
    MFSEntry_t*      mfsEntry;

    osStatus = MfsLocateRecord(
            mfs,
            &mfs->RootEntry,
            path,
            &mfsEntry);
    if (osStatus != OsOK) {
        return osStatus;
    }

    osStatus = MfsFreeBuckets(mfs, mfsEntry->StartBucket, mfsEntry->StartLength);
    if (osStatus != OsOK) {
        ERROR("Failed to free the buckets at start 0x%x, length 0x%x",
              mfsEntry->StartBucket, mfsEntry->StartLength);
        goto cleanup;
    }

    osStatus = MfsUpdateRecord(mfs, mfsEntry, MFS_ACTION_DELETE);
cleanup:
    MFSEntryDelete(mfsEntry);
    return osStatus;
}

oserr_t
FsLink(
        _In_ void*      instanceData,
        _In_ void*      data,
        _In_ mstring_t* linkName,
        _In_ mstring_t* linkTarget,
        _In_ int        symbolic)
{
    // TODO implement MFS::Link
    return OsNotSupported;
}

oserr_t
FsReadLink(
        _In_ void*       instanceData,
        _In_ mstring_t*  path,
        _In_ mstring_t** pathOut)
{
    // TODO implement MFS::ReadLink
    return OsNotSupported;
}

oserr_t
FsMove(
        _In_ void*      instanceData,
        _In_ mstring_t* from,
        _In_ mstring_t* to,
        _In_ int        copy)
{
    // TODO implement MFS::Move
    return OsNotSupported;
}

oserr_t
FsRead(
        _In_  void*   instanceData,
        _In_  void*   data,
        _In_  uuid_t  bufferHandle,
        _In_  void*   buffer,
        _In_  size_t  bufferOffset,
        _In_  size_t  unitCount,
        _Out_ size_t* unitsRead)
{
    MFSEntry_t* entry = (MFSEntry_t*)data;
    TRACE("FsReadEntry(flags 0x%x, length %u)", entry->Flags, unitCount);

    if (entry->Flags & FILE_FLAG_DIRECTORY) {
        return FsReadFromDirectory(instanceData, entry, buffer, bufferOffset, unitCount, unitsRead);
    }
    else {
        return FsReadFromFile(instanceData, entry, bufferHandle, buffer, bufferOffset, unitCount, unitsRead);
    }
}

oserr_t
FsWrite(
        _In_  void*   instanceData,
        _In_  void*   data,
        _In_  uuid_t  bufferHandle,
        _In_  void*   buffer,
        _In_  size_t  bufferOffset,
        _In_  size_t  unitCount,
        _Out_ size_t* unitsWritten)
{
    MFSEntry_t* entry = (MFSEntry_t*)data;
    TRACE("FsWriteEntry(flags 0x%x, length %u)", entry->Flags, unitCount);

    if (!(entry->Flags & FILE_FLAG_DIRECTORY)) {
        return FsWriteToFile(instanceData, entry, bufferHandle, buffer, bufferOffset, unitCount, unitsWritten);
    }
    return OsInvalidParameters;
}

oserr_t
FsSeek(
        _In_  void*     instanceData,
        _In_  void*     data,
        _In_  uint64_t  absolutePosition,
        _Out_ uint64_t* absolutePositionOut)
{
    MFSEntry_t* entry = (MFSEntry_t*)data;
    oserr_t     osStatus;

    if (entry->Flags & FILE_FLAG_DIRECTORY) {
        osStatus = FsSeekInDirectory(instanceData, entry, absolutePosition);
    } else {
        osStatus = FsSeekInFile(instanceData, entry, absolutePosition);
    }
    if (osStatus == OsOK) {
        *absolutePositionOut = entry->Position;
    }
    return osStatus;
}

static FileSystemMFS_t* __FileSystemMFSNew(
        _In_ struct VFSStorageParameters* storageParameters,
        _In_ StorageDescriptor_t*         storageStats)
{
    FileSystemMFS_t* mfs;
    DMABuffer_t      bufferInfo;
    oserr_t          oserr;

    mfs = (FileSystemMFS_t*)malloc(sizeof(FileSystemMFS_t));
    if (!mfs) {
        return NULL;
    }
    memset(mfs, 0, sizeof(FileSystemMFS_t));

    // Store various parameters from the storage medium, we need those
    // during read/write operations and misc operations to calculate
    // offsets and sizes
    memcpy(&mfs->Storage, storageParameters, sizeof(struct VFSStorageParameters));
    mfs->SectorSize = storageStats->SectorSize;

    // Create a generic transferbuffer for us to use
    bufferInfo.name     = "mfs_transfer_buffer";
    bufferInfo.length   = storageStats->SectorSize;
    bufferInfo.capacity = storageStats->SectorSize;
    bufferInfo.flags    = 0;
    bufferInfo.type     = DMA_TYPE_DRIVER_32;

    oserr = DmaCreate(&bufferInfo, &mfs->TransferBuffer);
    if (oserr != OsOK) {
        free(mfs);
        return NULL;
    }
    return mfs;
}

static void __FileSystemMFSDelete(
        _In_ FileSystemMFS_t* mfs)
{
    if (mfs->TransferBuffer.buffer != NULL) {
        (void) DmaAttachmentUnmap(&mfs->TransferBuffer);
        (void) DmaDetach(&mfs->TransferBuffer);
    }
    free(mfs->BucketMap);
    free(mfs);
}

static oserr_t __ParseBootRecord(
        _In_ FileSystemMFS_t* mfs,
        _In_ BootRecord_t*    bootRecord)
{
    if (bootRecord->Magic != MFS_BOOTRECORD_MAGIC) {
        ERROR("__ParseBootRecord invalid boot-record signature (0x%x, expected 0x%x)",
              bootRecord->Magic, MFS_BOOTRECORD_MAGIC);
        return OsInvalidParameters;
    }
    TRACE("Fs-Version: %u", bootRecord->Version);

    // Store some data from the boot-record
    mfs->Version                  = (int)bootRecord->Version;
    mfs->Flags                    = (unsigned int)bootRecord->Flags;
    mfs->MasterRecordSector       = bootRecord->MasterRecordSector;
    mfs->MasterRecordMirrorSector = bootRecord->MasterRecordMirror;
    mfs->SectorsPerBucket         = bootRecord->SectorsPerBucket;
    mfs->ReservedSectorCount      = bootRecord->ReservedSectors;

    // Bucket entries are 64 bit (8 bytes) in map
    mfs->BucketsPerSectorInMap = mfs->SectorSize / sizeof(struct MapRecord);
    return OsOK;
}

static oserr_t __ResizeTransferBuffer(
        _In_ FileSystemMFS_t* mfs)
{
    DMABuffer_t bufferInfo;

    // TODO should probably error check these
    (void) DmaAttachmentUnmap(&mfs->TransferBuffer);
    (void) DmaDetach(&mfs->TransferBuffer);

    bufferInfo.length   = mfs->SectorSize;
    bufferInfo.capacity = mfs->SectorSize;

    // Create a new transfer buffer that is more persistant and attached to the fs
    // and will provide the primary intermediate buffer for general usage.
    bufferInfo.name     = "mfs_transfer_buffer";
    bufferInfo.length   = mfs->SectorsPerBucket * mfs->SectorSize * MFS_ROOTSIZE;
    bufferInfo.capacity = mfs->SectorsPerBucket * mfs->SectorSize * MFS_ROOTSIZE;
    bufferInfo.flags    = 0;
    bufferInfo.type     = DMA_TYPE_DRIVER_32;

    return DmaCreate(&bufferInfo, &mfs->TransferBuffer);
}

static oserr_t __ValidateMasterRecord(
        _In_ MasterRecord_t*  masterRecord)
{
    if (masterRecord->Magic != MFS_BOOTRECORD_MAGIC) {
        ERROR("__ValidateMasterRecord invalid master-record signature (0x%x, expected 0x%x)",
              masterRecord->Magic, MFS_BOOTRECORD_MAGIC);
        return OsInvalidParameters;
    }
    return OsOK;
}

static oserr_t
__ParseAndProcessMasterRecord(
        _In_ FileSystemMFS_t* mfs)
{
    uint8_t* bMap;
    uint64_t bytesRead;
    uint64_t bytesLeft;
    oserr_t  oserr;
    size_t   i, imax;
    size_t   sectorsTransferred;

    TRACE("Partition-name: %s", &mfs->MasterRecord.PartitionName[0]);
    mfs->Label = mstr_new_u8((const char*)&mfs->MasterRecord.PartitionName[0]);

    // Parse the master record
    TRACE("Partition flags: 0x%x", mfs->MasterRecord.Flags);
    TRACE("Caching bucket-map (Sector %u - Size %u Bytes)",
          LODWORD(mfs->MasterRecord.MapSector),
          LODWORD(mfs->MasterRecord.MapSize));

    // Calculate the number of entries in the bucket map.
    mfs->BucketsInMap = mfs->MasterRecord.MapSize / sizeof(struct MapRecord);
    mfs->BucketMap    = (uint32_t*)malloc((size_t)mfs->MasterRecord.MapSize + mfs->SectorSize);
    if (mfs->BucketMap == NULL) {
        return OsOutOfMemory;
    }

    // Load map
    bMap      = (uint8_t*)mfs->BucketMap;
    bytesLeft = mfs->MasterRecord.MapSize;
    bytesRead = 0;
    i         = 0;
    imax      = DIVUP(bytesLeft, (mfs->SectorsPerBucket * mfs->SectorSize));

#ifdef CACHE_SEGMENTED
    while (bytesLeft) {
        uint64_t mapSector    = mfs->MasterRecord.MapSector + (i * mfs->SectorsPerBucket);
        size_t   transferSize = MIN((mfs->SectorsPerBucket * mfs->SectorSize), (size_t)bytesLeft);
        size_t   sectorCount  = DIVUP(transferSize, mfs->SectorSize);

        oserr = FSStorageRead(
                &mfs->Storage,
                mfs->TransferBuffer.handle,
                0,
                &(UInteger64_t) { .QuadPart = mapSector },
                sectorCount,
                &sectorsTransferred
        );
        if (oserr != OsOK) {
            ERROR("Failed to read sector 0x%x (map) into cache", LODWORD(mapSector));
            return oserr;
        }

        if (sectorsTransferred != sectorCount) {
            ERROR("Read %u sectors instead of %u from sector %u [bytesleft %u]",
                  LODWORD(sectorsTransferred), LODWORD(sectorCount),
                  LODWORD(mfs->Storage.SectorStart.QuadPart + mapSector),
                  LODWORD(bytesLeft));
            return OsDeviceError;
        }

        // Reset buffer position to 0 and read the data into the map
        memcpy(bMap, mfs->TransferBuffer.buffer, transferSize);
        bytesLeft -= transferSize;
        bytesRead += transferSize;
        bMap      += transferSize;
        i++;
        if (i == (imax / 4) || i == (imax / 2) || i == ((imax / 4) * 3)) {
            TRACE("Cached %u/%u bytes of sector-map", LODWORD(bytesRead), LODWORD(mfs->MasterRecord.MapSize));
        }
    }
    TRACE("Bucket map was cached");
#else
    DMABuffer_t     mapInfo;
    DMAAttachment_t mapAttachment;
    uint64_t        mapSector   = Mfs->MasterRecord.MapSector + (i * Mfs->SectorsPerBucket);
    size_t          sectorCount = DIVUP((size_t)Mfs->MasterRecord.MapSize,
        Descriptor->Disk.descriptor.SectorSize);

    mapInfo.name     = "mfs_mapbuffer";
    mapInfo.length   = (size_t)Mfs->MasterRecord.MapSize + mfs->SectorSize;
    mapInfo.capacity = (size_t)Mfs->MasterRecord.MapSize + mfs->SectorSize;
    mapInfo.flags    = DMA_PERSISTANT;
    DmaInfo.type     = DMA_TYPE_DRIVER_32;

    Status = dma_export(bMap, &mapInfo, &mapAttachment);
    if (Status != OsOK) {
        ERROR("[mfs] [init] failed to export buffer for sector-map");
        goto Error;
    }

    if (MfsReadSectors(Descriptor, mapAttachment.handle, 0,
            mapSector, sectorCount, &SectorsTransferred) != OsOK) {
        ERROR("[mfs] [init] failed to read sector 0x%x (map) into cache", LODWORD(mapSector));
        goto Error;
    }

    if (SectorsTransferred != sectorCount) {
        ERROR("[mfs] [init] read %u sectors instead of %u from sector %u",
            LODWORD(SectorsTransferred), LODWORD(sectorCount), LODWORD(mapSector));
        goto Error;
    }

    dma_detach(&mapAttachment);
#endif
    return OsOK;
}

static void __InitializeRootRecord(
        _In_ FileSystemMFS_t* mfs)
{
    memcpy(&mfs->RootRecord.Name[0], RootEntryName, strlen(RootEntryName));
    mfs->RootRecord.Flags       = MFS_FILERECORD_INUSE | MFS_FILERECORD_LOCKED |
                                  MFS_FILERECORD_SYSTEM | MFS_FILERECORD_DIRECTORY;
    mfs->RootRecord.StartBucket = mfs->MasterRecord.RootIndex;
    mfs->RootRecord.StartLength = MFS_ROOTSIZE;
}

oserr_t
FsInitialize(
        _In_  struct VFSStorageParameters* storageParameters,
        _Out_ void**                       instanceData)
{
    FileSystemMFS_t*    mfs;
    oserr_t             oserr;
    size_t              sectorsTransferred;
    StorageDescriptor_t storageStats;

    TRACE("FsInitialize()");

    // Get the geometry of the disk, we need to it to calculate various offsets on
    // the storage medium.
    FSStorageStat(storageParameters, &storageStats);

    mfs = __FileSystemMFSNew(storageParameters, &storageStats);
    if (mfs == NULL) {
        ERROR("FsInitialize Failed to allocate memory for the fileystem");
        return OsOutOfMemory;
    }

    // Read the boot-sector
    oserr = FSStorageRead(
            storageParameters,
            mfs->TransferBuffer.handle,
            0,
            &(UInteger64_t) { .QuadPart = 0 },
            1,
            &sectorsTransferred
    );
    if (oserr != OsOK) {
        ERROR("FsInitialize Failed to read mfs boot-sector record");
        goto error_exit;
    }

    oserr = __ParseBootRecord(
            mfs,
            (BootRecord_t*)mfs->TransferBuffer.buffer
    );
    if (oserr != OsOK) {
        ERROR("FsInitialize failed to parse the boot record");
        goto error_exit;
    }

    // Read the master-record
    oserr = FSStorageRead(
            storageParameters,
            mfs->TransferBuffer.handle,
            0,
            &(UInteger64_t) { .QuadPart = mfs->MasterRecordSector },
            1,
            &sectorsTransferred
    );
    if (oserr != OsOK) {
        ERROR("FsInitialize failed to read the master record");
        goto error_exit;
    }

    // Validate and store the master record immediately, so we can resize the
    // transfer buffer. The moment we do, the data is lost.
    oserr = __ValidateMasterRecord((MasterRecord_t*)mfs->TransferBuffer.buffer);
    if (oserr != OsOK) {
        ERROR("FsInitialize failed to read the master record");
        goto error_exit;
    }
    memcpy(&mfs->MasterRecord, mfs->TransferBuffer.buffer, sizeof(MasterRecord_t));

    oserr = __ResizeTransferBuffer(mfs);
    if (oserr != OsOK) {
        ERROR("FsInitialize failed to allocate memory for resized transfer buffer");
        goto error_exit;
    }

    oserr = __ParseAndProcessMasterRecord(mfs);
    if (oserr != OsOK) {
        ERROR("FsInitialize failed to read the master record");
        goto error_exit;
    }

    __InitializeRootRecord(mfs);
    *instanceData = mfs;
    return OsOK;

error_exit:
    __FileSystemMFSDelete(mfs);
    return oserr;
}

oserr_t
FsDestroy(
        _In_ void*         instanceData,
        _In_ unsigned int  unmountFlags)
{
    FileSystemMFS_t* fileSystem = instanceData;
    if (instanceData == NULL) {
        return OsInvalidParameters;
    }

    // Which kind of unmount is it?
    if (!(unmountFlags & 0x1)) {
        // Flush everything
        // @todo
    }

    __FileSystemMFSDelete(fileSystem);
    return OsOK;
}
