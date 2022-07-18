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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS General File System (MFS) Driver
 *  - Contains the implementation of the MFS driver for mollenos
 */

//#define __TRACE
#define CACHE_SEGMENTED

#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>
#include "mfs.h"

static const char* RootEntryName = "<root>";

// File specific operation handlers
oserr_t FsReadFromFile(struct VFSCommonData*, MFSEntry_t*, uuid_t, void*, size_t, size_t, size_t*);
oserr_t FsWriteToFile(struct VFSCommonData*, MFSEntry_t*, uuid_t, void*, size_t, size_t, size_t*);
oserr_t FsSeekInFile(struct VFSCommonData*, MFSEntry_t*, uint64_t);

// Directory specific operation handlers
oserr_t FsReadFromDirectory(struct VFSCommonData*, MFSEntry_t*, void*, size_t, size_t, size_t*);
oserr_t FsSeekInDirectory(struct VFSCommonData*, MFSEntry_t*, uint64_t);

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
        _In_      struct VFSCommonData* vfsCommonData,
        _In_      mstring_t*            path,
        _Out_Opt_ void**                dataOut)
{
    FileSystemMFS_t* mfs = (FileSystemMFS_t*)vfsCommonData->Data;
    oserr_t          osStatus;
    MFSEntry_t*      mfsEntry;

    mfsEntry = MFSEntryNew();
    if (mfsEntry == NULL) {
        return OsOutOfMemory;
    }

    osStatus = MfsLocateRecord(
            vfsCommonData,
            mfs->MasterRecord.RootIndex,
            mfsEntry,
            path);
    if (osStatus != OsOK) {
        free(mfsEntry);
        return osStatus;
    }
    *dataOut = mfsEntry;
    return osStatus;
}

oserr_t
FsCreate(
        _In_  struct VFSCommonData* vfsCommonData,
        _In_  void*                 data,
        _In_  mstring_t*            name,
        _In_  uint32_t              owner,
        _In_  uint32_t              flags,
        _In_  uint32_t              permissions,
        _Out_ void**                dataOut)
{
    oserr_t     osStatus;
    MFSEntry_t* entry = (MFSEntry_t*)data;
    MFSEntry_t* result;

    osStatus = MfsCreateRecord(
            vfsCommonData,
            entry,
            name,
            owner,
            flags,
            permissions,
            &result);
    if (osStatus != OsOK) {
        return osStatus;
    }
    *dataOut = result;
    return osStatus;
}

oserr_t
FsClose(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ void*                 data)
{
    MFSEntry_t* entry    = (MFSEntry_t*)data;
    oserr_t     osStatus = OsOK;
    
    TRACE("FsClose(%i)", entry->ActionOnClose);
    if (entry->ActionOnClose) {
        osStatus = MfsUpdateRecord(vfsCommonData, entry, entry->ActionOnClose);
    }
    if (entry->Name != NULL) {
        mstr_delete(entry->Name);
    }
    free(entry);
    return osStatus;
}

oserr_t
FsStat(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ struct VFSStatFS*     stat)
{

}

oserr_t
FsUnlink(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ mstring_t*            path)
{
    FileSystemMFS_t* mfs = (FileSystemMFS_t*)vfsCommonData->Data;
    oserr_t          osStatus;
    MFSEntry_t*      mfsEntry;

    mfsEntry = MFSEntryNew();
    if (mfsEntry == NULL) {
        return OsOutOfMemory;
    }

    osStatus = MfsLocateRecord(vfsCommonData, mfs->RootRecord.StartBucket, mfsEntry, path);
    if (osStatus != OsOK) {
        goto cleanup;
    }

    osStatus = MfsFreeBuckets(vfsCommonData, mfsEntry->StartBucket, mfsEntry->StartLength);
    if (osStatus != OsOK) {
        ERROR("Failed to free the buckets at start 0x%x, length 0x%x",
              mfsEntry->StartBucket, mfsEntry->StartLength);
        goto cleanup;
    }

    osStatus = MfsUpdateRecord(vfsCommonData, mfsEntry, MFS_ACTION_DELETE);

cleanup:
    MFSEntryDelete(mfsEntry);
    return osStatus;
}

oserr_t
FsLink(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ void*                 data,
        _In_ mstring_t*            linkName,
        _In_ mstring_t*            linkTarget,
        _In_ int                   symbolic)
{

}

oserr_t
FsReadLink(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ mstring_t*            path,
        _In_ mstring_t*            pathOut)
{

}

oserr_t
FsMove(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ mstring_t*            from,
        _In_ mstring_t*            to,
        _In_ int                   copy)
{

}

oserr_t
FsRead(
        _In_  struct VFSCommonData* vfsCommonData,
        _In_  void*                 data,
        _In_  uuid_t                bufferHandle,
        _In_  void*                 buffer,
        _In_  size_t                bufferOffset,
        _In_  size_t                unitCount,
        _Out_ size_t*               unitsRead)
{
    MFSEntry_t* entry = (MFSEntry_t*)data;
    TRACE("FsReadEntry(flags 0x%x, length %u)", entry->Flags, unitCount);

    if (entry->Flags & FILE_FLAG_DIRECTORY) {
        return FsReadFromDirectory(vfsCommonData, entry, buffer, bufferOffset, unitCount, unitsRead);
    }
    else {
        return FsReadFromFile(vfsCommonData, entry, bufferHandle, buffer, bufferOffset, unitCount, unitsRead);
    }
}

oserr_t
FsWrite(
        _In_  struct VFSCommonData* vfsCommonData,
        _In_  void*                 data,
        _In_  uuid_t                bufferHandle,
        _In_  void*                 buffer,
        _In_  size_t                bufferOffset,
        _In_  size_t                unitCount,
        _Out_ size_t*               unitsWritten)
{
    MFSEntry_t* entry = (MFSEntry_t*)data;
    TRACE("FsWriteEntry(flags 0x%x, length %u)", entry->Flags, unitCount);

    if (!(entry->Flags & FILE_FLAG_DIRECTORY)) {
        return FsWriteToFile(vfsCommonData, entry, bufferHandle, buffer, bufferOffset, unitCount, unitsWritten);
    }
    return OsInvalidParameters;
}

oserr_t
FsSeek(
        _In_  struct VFSCommonData* vfsCommonData,
        _In_  void*                 data,
        _In_  uint64_t              absolutePosition,
        _Out_ uint64_t*             absolutePositionOut)
{
    MFSEntry_t* entry = (MFSEntry_t*)data;
    oserr_t     osStatus;

    if (entry->Flags & FILE_FLAG_DIRECTORY) {
        osStatus = FsSeekInDirectory(vfsCommonData, entry, absolutePosition);
    } else {
        osStatus = FsSeekInFile(vfsCommonData, entry, absolutePosition);
    }
    if (osStatus == OsOK) {
        *absolutePositionOut = entry->Position;
    }
    return osStatus;
}

oserr_t
FsDestroy(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ unsigned int          unmountFlags)
{
    FileSystemMFS_t* fileSystem;

    if (!vfsCommonData) {
        return OsInvalidParameters;
    }

    fileSystem = (FileSystemMFS_t*)vfsCommonData->Data;
    if (fileSystem == NULL) {
        return OsInvalidParameters;
    }

    // Which kind of unmount is it?
    if (!(unmountFlags & 0x1)) {
        // Flush everything
        // @todo
    }

    // Cleanup all allocated resources
    if (fileSystem->TransferBuffer.buffer != NULL) {
        dma_attachment_unmap(&fileSystem->TransferBuffer);
        dma_detach(&fileSystem->TransferBuffer);
    }

    // Free the bucket-map
    if (fileSystem->BucketMap != NULL) {
        free(fileSystem->BucketMap);
    }

    // Free structure and return
    free(fileSystem);
    vfsCommonData->Data = NULL;
    return OsOK;
}

void
FsInitializeRootRecord(
        _In_ FileSystemMFS_t* fileSystem)
{
    memset(&fileSystem->RootRecord, 0, sizeof(FileRecord_t));
    memcpy(&fileSystem->RootRecord.Name[0], RootEntryName, strlen(RootEntryName));

    fileSystem->RootRecord.Flags       = MFS_FILERECORD_INUSE | MFS_FILERECORD_LOCKED |
                                         MFS_FILERECORD_SYSTEM | MFS_FILERECORD_DIRECTORY;
    fileSystem->RootRecord.StartBucket = fileSystem->MasterRecord.RootIndex;
    fileSystem->RootRecord.StartLength = MFS_ROOTSIZE;
}

oserr_t
FsInitialize(
        _In_ struct VFSCommonData* vfsCommonData)
{
    MasterRecord_t*  masterRecord;
    BootRecord_t*    bootRecord;
    FileSystemMFS_t* mfsInstance;
    uint8_t*         bMap;
    uint64_t         bytesRead;
    uint64_t         bytesLeft;
    oserr_t          osStatus;
    size_t           i, imax;
    size_t           sectorsTransferred;
    
    struct dma_buffer_info bufferInfo = { 0 };

    TRACE("FsInitialize()");

    mfsInstance = (FileSystemMFS_t*)malloc(sizeof(FileSystemMFS_t));
    if (!mfsInstance) {
        ERROR("Failed to allocate memory for mfs variable");
        osStatus = OsOutOfMemory;
        goto error_exit;
    }
    memset(mfsInstance, 0, sizeof(FileSystemMFS_t));
    
    // Create a generic transferbuffer for us to use
    bufferInfo.length   = vfsCommonData->Storage.SectorSize;
    bufferInfo.capacity = vfsCommonData->Storage.SectorSize;
    bufferInfo.flags    = 0;
    bufferInfo.type     = DMA_TYPE_DRIVER_32;

    osStatus = dma_create(&bufferInfo, &mfsInstance->TransferBuffer);
    if (osStatus != OsOK) {
        free(mfsInstance);
        return osStatus;
    }

    // Read the boot-sector
    if (MfsReadSectors(vfsCommonData, mfsInstance->TransferBuffer.handle,
                       0, 0, 1, &sectorsTransferred) != OsOK) {
        ERROR("Failed to read mfs boot-sector record");
        goto error_exit;
    }

    vfsCommonData->Data = (uintptr_t*)mfsInstance;
    bootRecord = (BootRecord_t*)mfsInstance->TransferBuffer.buffer;
    if (bootRecord->Magic != MFS_BOOTRECORD_MAGIC) {
        ERROR("Failed to validate boot-record signature (0x%x, expected 0x%x)",
              bootRecord->Magic, MFS_BOOTRECORD_MAGIC);
        osStatus = OsInvalidParameters;
        goto error_exit;
    }
    TRACE("Fs-Version: %u", bootRecord->Version);

    // Store some data from the boot-record
    mfsInstance->Version                  = (int)bootRecord->Version;
    mfsInstance->Flags                    = (unsigned int)bootRecord->Flags;
    mfsInstance->MasterRecordSector       = bootRecord->MasterRecordSector;
    mfsInstance->MasterRecordMirrorSector = bootRecord->MasterRecordMirror;
    mfsInstance->SectorsPerBucket         = bootRecord->SectorsPerBucket;
    mfsInstance->ReservedSectorCount      = bootRecord->ReservedSectors;

    // Calculate where our map sector is
    mfsInstance->BucketCount = vfsCommonData->SectorCount / mfsInstance->SectorsPerBucket;
    
    // Bucket entries are 64 bit (8 bytes) in map
    mfsInstance->BucketsPerSectorInMap = vfsCommonData->Storage.SectorSize / 8;

    // Read the master-record
    if (MfsReadSectors(vfsCommonData, mfsInstance->TransferBuffer.handle, 0,
                       mfsInstance->MasterRecordSector, 1, &sectorsTransferred) != OsOK) {
        ERROR("Failed to read mfs master-sectofferfferr record");
        osStatus = OsError;
        goto error_exit;
    }
    masterRecord = (MasterRecord_t*)mfsInstance->TransferBuffer.buffer;

    // Process the master-record
    if (masterRecord->Magic != MFS_BOOTRECORD_MAGIC) {
        ERROR("Failed to validate master-record signature (0x%x, expected 0x%x)",
              masterRecord->Magic, MFS_BOOTRECORD_MAGIC);
        osStatus = OsInvalidParameters;
        goto error_exit;
    }
    TRACE("Partition-name: %s", &masterRecord->PartitionName[0]);
    memcpy(&mfsInstance->MasterRecord, masterRecord, sizeof(MasterRecord_t));

    // Parse the master record
    vfsCommonData->Label = mstr_new_u8((const char*)&masterRecord->PartitionName[0]);
    TRACE("Partition flags: 0x%x", fileSystemBase->Flags);

    dma_attachment_unmap(&mfsInstance->TransferBuffer);
    dma_detach(&mfsInstance->TransferBuffer);

    // Create a new transfer buffer that is more persistant and attached to the fs
    // and will provide the primary intermediate buffer for general usage.
    bufferInfo.length   = mfsInstance->SectorsPerBucket * vfsCommonData->Storage.SectorSize * MFS_ROOTSIZE;
    bufferInfo.capacity = mfsInstance->SectorsPerBucket * vfsCommonData->Storage.SectorSize * MFS_ROOTSIZE;
    osStatus = dma_create(&bufferInfo, &mfsInstance->TransferBuffer);
    if (osStatus != OsOK) {
        free(mfsInstance);
        return osStatus;
    }
    
    TRACE("Caching bucket-map (Sector %u - Size %u Bytes)",
        LODWORD(mfsInstance->MasterRecord.MapSector),
        LODWORD(mfsInstance->MasterRecord.MapSize));

    // Load map
    mfsInstance->BucketMap = (uint32_t*)malloc((size_t)mfsInstance->MasterRecord.MapSize + vfsCommonData->Storage.SectorSize);
    bMap      = (uint8_t*)mfsInstance->BucketMap;
    bytesLeft = mfsInstance->MasterRecord.MapSize;
    bytesRead = 0;
    i         = 0;
    imax      = DIVUP(bytesLeft, (mfsInstance->SectorsPerBucket * vfsCommonData->Storage.SectorSize));

#ifdef CACHE_SEGMENTED
    while (bytesLeft) {
        uint64_t mapSector    = mfsInstance->MasterRecord.MapSector + (i * mfsInstance->SectorsPerBucket);
        size_t   transferSize = MIN((mfsInstance->SectorsPerBucket * vfsCommonData->Storage.SectorSize), (size_t)bytesLeft);
        size_t   sectorCount  = DIVUP(transferSize, vfsCommonData->Storage.SectorSize);

        osStatus = MfsReadSectors(vfsCommonData, mfsInstance->TransferBuffer.handle, 0,
                                  mapSector, sectorCount, &sectorsTransferred);
        if (osStatus != OsOK) {
            ERROR("Failed to read sector 0x%x (map) into cache", LODWORD(mapSector));
            goto error_exit;
        }

        if (sectorsTransferred != sectorCount) {
            ERROR("Read %u sectors instead of %u from sector %u [bytesleft %u]",
                  LODWORD(sectorsTransferred), LODWORD(sectorCount),
                  LODWORD(vfsCommonData->SectorStart + mapSector),
                  LODWORD(bytesLeft));
            osStatus = OsDeviceError;
            goto error_exit;
        }

        // Reset buffer position to 0 and read the data into the map
        memcpy(bMap, mfsInstance->TransferBuffer.buffer, transferSize);
        bytesLeft -= transferSize;
        bytesRead += transferSize;
        bMap      += transferSize;
        i++;
        if (i == (imax / 4) || i == (imax / 2) || i == ((imax / 4) * 3)) {
            TRACE("Cached %u/%u bytes of sector-map", LODWORD(bytesRead), LODWORD(mfsInstance->MasterRecord.MapSize));
        }
    }
    TRACE("Bucket map was cached");
#else
    struct dma_buffer_info mapInfo;
    struct dma_attachment  mapAttachment;
    uint64_t               mapSector   = Mfs->MasterRecord.MapSector + (i * Mfs->SectorsPerBucket);
    size_t                 sectorCount = DIVUP((size_t)Mfs->MasterRecord.MapSize,
        Descriptor->Disk.descriptor.SectorSize);
    
    mapInfo.name     = "mfs_mapbuffer";
    mapInfo.length   = (size_t)Mfs->MasterRecord.MapSize + Descriptor->Disk.descriptor.SectorSize;
    mapInfo.capacity = (size_t)Mfs->MasterRecord.MapSize + Descriptor->Disk.descriptor.SectorSize;
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
    
    FsInitializeRootRecord(mfsInstance);
    return OsOK;

error_exit:
    FsDestroy(vfsCommonData, 0);
    return osStatus;
}
