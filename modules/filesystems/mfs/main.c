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
OsStatus_t FsReadFromFile(FileSystemBase_t*, FileSystemEntryMFS_t*, FileSystemHandleMFS_t*, UUId_t, void*, size_t, size_t, size_t*);
OsStatus_t FsWriteToFile(FileSystemBase_t*, FileSystemEntryMFS_t*, FileSystemHandleMFS_t*, UUId_t, void*, size_t, size_t, size_t*);
OsStatus_t FsSeekInFile(FileSystemBase_t*, FileSystemEntryMFS_t*, FileSystemHandleMFS_t*, uint64_t);

// Directory specific operation handlers
OsStatus_t FsReadFromDirectory(FileSystemBase_t*, FileSystemEntryMFS_t*, FileSystemHandleMFS_t*, void*, size_t, size_t, size_t*);
OsStatus_t FsSeekInDirectory(FileSystemBase_t*, FileSystemEntryMFS_t*, FileSystemHandleMFS_t*, uint64_t);

OsStatus_t 
FsOpenEntry(
        _In_  FileSystemBase_t*       fileSystemBase,
        _In_  MString_t*              path,
        _Out_ FileSystemEntryBase_t** baseOut)
{
    FileSystemMFS_t*      mfs = (FileSystemMFS_t*)fileSystemBase->ExtensionData;
    OsStatus_t            osStatus;
    FileSystemEntryMFS_t* mfsEntry;

    mfsEntry = (FileSystemEntryMFS_t*)malloc(sizeof(FileSystemEntryMFS_t));
    if (!mfsEntry) {
        return OsOutOfMemory;
    }
    
    memset(mfsEntry, 0, sizeof(FileSystemEntryMFS_t));
    osStatus = MfsLocateRecord(fileSystemBase, mfs->MasterRecord.RootIndex, mfsEntry, path);
    *baseOut = (FileSystemEntryBase_t*)mfsEntry;
    if (osStatus != OsSuccess) {
        free(mfsEntry);
    }
    return osStatus;
}

OsStatus_t 
FsCreatePath(
        _In_  FileSystemBase_t*       fileSystemBase,
        _In_  MString_t*              path,
        _In_  unsigned int            options,
        _Out_ FileSystemEntryBase_t** baseOut)
{
    FileSystemMFS_t*      mfs;
    OsStatus_t            osStatus;
    FileSystemEntryMFS_t* entry;

    if (!fileSystemBase || !baseOut) {
        return OsInvalidParameters;
    }

    mfs      = (FileSystemMFS_t*)fileSystemBase->ExtensionData;
    osStatus = MfsCreateRecord(fileSystemBase, options, mfs->MasterRecord.RootIndex, path, &entry);
    if (osStatus == OsSuccess) {
        *baseOut = &entry->Base;
    }
    return osStatus;
}

OsStatus_t
FsCloseEntry(
        _In_ FileSystemBase_t*      fileSystemBase,
        _In_ FileSystemEntryBase_t* entryBase)
{
    OsStatus_t            osStatus = OsSuccess;
    FileSystemEntryMFS_t* entry = (FileSystemEntryMFS_t*)entryBase;
    
    TRACE("FsCloseEntry(%i)", entry->ActionOnClose);
    if (entry->ActionOnClose) {
        osStatus = MfsUpdateRecord(fileSystemBase, entry, entry->ActionOnClose);
    }
    if (entryBase->Name != NULL) {
        MStringDestroy(entryBase->Name);
    }
    free(entry);
    return osStatus;
}

OsStatus_t
FsDeleteEntry(
        _In_ FileSystemBase_t*      fileSystemBase,
        _In_ FileSystemEntryBase_t* entryBase)
{
    FileSystemEntryMFS_t*  entry = (FileSystemEntryMFS_t*)entryBase;
    OsStatus_t             osStatus;

    osStatus = MfsFreeBuckets(fileSystemBase, entry->StartBucket, entry->StartLength);
    if (osStatus != OsSuccess) {
        ERROR("Failed to free the buckets at start 0x%x, length 0x%x",
              entry->StartBucket, entry->StartLength);
        return OsDeviceError;
    }

    osStatus = MfsUpdateRecord(fileSystemBase, entry, MFS_ACTION_DELETE);
    if (osStatus == OsSuccess) {
        osStatus = FsCloseEntry(fileSystemBase, &entry->Base);
    }
    return osStatus;
}

OsStatus_t
FsOpenHandle(
        _In_  FileSystemBase_t*        fileSystemBase,
        _In_  FileSystemEntryBase_t*   entryBase,
        _Out_ FileSystemHandleBase_t** handleBaseOut)
{
    FileSystemEntryMFS_t*  entry = (FileSystemEntryMFS_t*)entryBase;
    FileSystemHandleMFS_t* handle;

    handle = (FileSystemHandleMFS_t*)malloc(sizeof(FileSystemHandleMFS_t));
    if (!handle) {
        return OsOutOfMemory;
    }
    
    memset(handle, 0, sizeof(FileSystemHandleMFS_t));
    handle->BucketByteBoundary = 0;
    handle->DataBucketPosition = entry->StartBucket;
    handle->DataBucketLength   = entry->StartLength;
    *handleBaseOut = &handle->Base;
    return OsSuccess;
}

OsStatus_t
FsCloseHandle(
        _In_ FileSystemBase_t*       fileSystemBase,
        _In_ FileSystemHandleBase_t* handleBase)
{
    FileSystemHandleMFS_t* handle = (FileSystemHandleMFS_t*)handleBase;
    free(handle);
    return OsSuccess;
}

OsStatus_t
FsReadEntry(
        _In_  FileSystemBase_t*       fileSystemBase,
        _In_  FileSystemEntryBase_t*  entryBase,
        _In_  FileSystemHandleBase_t* handleBase,
        _In_  UUId_t                  bufferHandle,
        _In_  void*                   buffer,
        _In_  size_t                  bufferOffset,
        _In_  size_t                  unitCount,
        _Out_ size_t*                 unitsRead)
{
    FileSystemHandleMFS_t* handle = (FileSystemHandleMFS_t*)handleBase;
    FileSystemEntryMFS_t*  entry  = (FileSystemEntryMFS_t*)entryBase;
    TRACE("FsReadEntry(flags 0x%x, length %u)", entryBase->Descriptor.Flags, unitCount);

    if (entryBase->Descriptor.Flags & FILE_FLAG_DIRECTORY) {
        return FsReadFromDirectory(fileSystemBase, entry, handle, buffer, bufferOffset, unitCount, unitsRead);
    }
    else {
        return FsReadFromFile(fileSystemBase, entry, handle, bufferHandle, buffer, bufferOffset, unitCount, unitsRead);
    }
}

OsStatus_t
FsWriteEntry(
        _In_  FileSystemBase_t*       fileSystemBase,
        _In_  FileSystemEntryBase_t*  entryBase,
        _In_  FileSystemHandleBase_t* handleBase,
        _In_  UUId_t                  bufferHandle,
        _In_  void*                   buffer,
        _In_  size_t                  bufferOffset,
        _In_  size_t                  unitCount,
        _Out_ size_t*                 unitsWritten)
{
    FileSystemHandleMFS_t* handle = (FileSystemHandleMFS_t*)handleBase;
    FileSystemEntryMFS_t*  entry  = (FileSystemEntryMFS_t*)entryBase;
    TRACE("FsWriteEntry(flags 0x%x, length %u)", entryBase->Descriptor.Flags, unitCount);

    if (!(entryBase->Descriptor.Flags & FILE_FLAG_DIRECTORY)) {
        return FsWriteToFile(fileSystemBase, entry, handle, bufferHandle, buffer, bufferOffset, unitCount, unitsWritten);
    }
    return OsInvalidParameters;
}

OsStatus_t
FsSeekInEntry(
        _In_ FileSystemBase_t*       fileSystemBase,
        _In_ FileSystemEntryBase_t*  entryBase,
        _In_ FileSystemHandleBase_t* handleBase,
        _In_ uint64_t                absolutePosition)
{
    FileSystemHandleMFS_t* handle = (FileSystemHandleMFS_t*)handleBase;
    FileSystemEntryMFS_t*  entry  = (FileSystemEntryMFS_t*)entryBase;

    if (entryBase->Descriptor.Flags & FILE_FLAG_DIRECTORY) {
        return FsSeekInDirectory(fileSystemBase, entry, handle, absolutePosition);
    }
    else {
        return FsSeekInFile(fileSystemBase, entry, handle, absolutePosition);
    }
}

OsStatus_t
FsDestroy(
        _In_ FileSystemBase_t* fileSystemBase,
        _In_ unsigned int      unmountFlags)
{
    FileSystemMFS_t* fileSystem;

    if (!fileSystemBase) {
        return OsInvalidParameters;
    }

    fileSystem = (FileSystemMFS_t*)fileSystemBase->ExtensionData;
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
    fileSystemBase->ExtensionData = NULL;
    return OsSuccess;
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

OsStatus_t
FsInitialize(
        _In_ FileSystemBase_t* fileSystemBase)
{
    MasterRecord_t*  masterRecord;
    BootRecord_t*    bootRecord;
    FileSystemMFS_t* mfsInstance;
    uint8_t*         bMap;
    uint64_t         bytesRead;
    uint64_t         bytesLeft;
    OsStatus_t       osStatus;
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
    bufferInfo.length   = fileSystemBase->Disk.descriptor.SectorSize;
    bufferInfo.capacity = fileSystemBase->Disk.descriptor.SectorSize;
    bufferInfo.flags    = 0;
    bufferInfo.type     = DMA_TYPE_DRIVER_32;

    osStatus = dma_create(&bufferInfo, &mfsInstance->TransferBuffer);
    if (osStatus != OsSuccess) {
        free(mfsInstance);
        return osStatus;
    }

    // Read the boot-sector
    if (MfsReadSectors(fileSystemBase, mfsInstance->TransferBuffer.handle,
                       0, 0, 1, &sectorsTransferred) != OsSuccess) {
        ERROR("Failed to read mfs boot-sector record");
        goto error_exit;
    }

    fileSystemBase->ExtensionData = (uintptr_t*)mfsInstance;
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
    mfsInstance->BucketCount = fileSystemBase->SectorCount / mfsInstance->SectorsPerBucket;
    
    // Bucket entries are 64 bit (8 bytes) in map
    mfsInstance->BucketsPerSectorInMap = fileSystemBase->Disk.descriptor.SectorSize / 8;

    // Read the master-record
    if (MfsReadSectors(fileSystemBase, mfsInstance->TransferBuffer.handle, 0,
                       mfsInstance->MasterRecordSector, 1, &sectorsTransferred) != OsSuccess) {
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
    if (masterRecord->Flags & MFS_MASTERRECORD_SYSTEM_DRIVE) {
        fileSystemBase->Flags |= __FILESYSTEM_BOOT;
    }
    if (masterRecord->Flags & MFS_MASTERRECORD_DATA_DRIVE) {
        fileSystemBase->Flags |= __FILESYSTEM_DATA;
    }
    if (masterRecord->Flags & MFS_MASTERRECORD_USER_DRIVE) {
        fileSystemBase->Flags |= __FILESYSTEM_USER;
    }
    TRACE("Partition flags: 0x%x", fileSystemBase->Flags);

    dma_attachment_unmap(&mfsInstance->TransferBuffer);
    dma_detach(&mfsInstance->TransferBuffer);

    // Create a new transfer buffer that is more persistant and attached to the fs
    // and will provide the primary intermediate buffer for general usage.
    bufferInfo.length   = mfsInstance->SectorsPerBucket * fileSystemBase->Disk.descriptor.SectorSize * MFS_ROOTSIZE;
    bufferInfo.capacity = mfsInstance->SectorsPerBucket * fileSystemBase->Disk.descriptor.SectorSize * MFS_ROOTSIZE;
    osStatus = dma_create(&bufferInfo, &mfsInstance->TransferBuffer);
    if (osStatus != OsSuccess) {
        free(mfsInstance);
        return osStatus;
    }
    
    TRACE("Caching bucket-map (Sector %u - Size %u Bytes)",
        LODWORD(mfsInstance->MasterRecord.MapSector),
        LODWORD(mfsInstance->MasterRecord.MapSize));

    // Load map
    mfsInstance->BucketMap = (uint32_t*)malloc((size_t)mfsInstance->MasterRecord.MapSize + fileSystemBase->Disk.descriptor.SectorSize);
    bMap      = (uint8_t*)mfsInstance->BucketMap;
    bytesLeft = mfsInstance->MasterRecord.MapSize;
    bytesRead = 0;
    i         = 0;
    imax      = DIVUP(bytesLeft, (mfsInstance->SectorsPerBucket * fileSystemBase->Disk.descriptor.SectorSize));

#ifdef CACHE_SEGMENTED
    while (bytesLeft) {
        uint64_t mapSector    = mfsInstance->MasterRecord.MapSector + (i * mfsInstance->SectorsPerBucket);
        size_t   transferSize = MIN((mfsInstance->SectorsPerBucket * fileSystemBase->Disk.descriptor.SectorSize), (size_t)bytesLeft);
        size_t   sectorCount  = DIVUP(transferSize, fileSystemBase->Disk.descriptor.SectorSize);

        osStatus = MfsReadSectors(fileSystemBase, mfsInstance->TransferBuffer.handle, 0,
                                  mapSector, sectorCount, &sectorsTransferred);
        if (osStatus != OsSuccess) {
            ERROR("Failed to read sector 0x%x (map) into cache", LODWORD(mapSector));
            goto error_exit;
        }

        if (sectorsTransferred != sectorCount) {
            ERROR("Read %u sectors instead of %u from sector %u [bytesleft %u]",
                  LODWORD(sectorsTransferred), LODWORD(sectorCount),
                  LODWORD(fileSystemBase->SectorStart + mapSector),
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
    if (Status != OsSuccess) {
        ERROR("[mfs] [init] failed to export buffer for sector-map");
        goto Error;
    }

    if (MfsReadSectors(Descriptor, mapAttachment.handle, 0, 
            mapSector, sectorCount, &SectorsTransferred) != OsSuccess) {
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
    return OsSuccess;

error_exit:
    FsDestroy(fileSystemBase, 0);
    return osStatus;
}
