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
#define CACHE_SEGMENTED

#include <ddk/utils.h>
#include <internal/_ipc.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include "mfs.h"

static const char* RootEntryName = "<root>";

// File specific operation handlers
OsStatus_t FsReadFromFile(FileSystemDescriptor_t*, MfsEntryHandle_t*, UUId_t, void*, size_t, size_t, size_t*);
OsStatus_t FsWriteToFile(FileSystemDescriptor_t*, MfsEntryHandle_t*, UUId_t, void*, size_t, size_t, size_t*);
OsStatus_t FsSeekInFile(FileSystemDescriptor_t*, MfsEntryHandle_t*, uint64_t);

// Directory specific operation handlers
OsStatus_t FsReadFromDirectory(FileSystemDescriptor_t*, MfsEntryHandle_t*, UUId_t, void*, size_t, size_t, size_t*);
OsStatus_t FsSeekInDirectory(FileSystemDescriptor_t*, MfsEntryHandle_t*, uint64_t);

OsStatus_t 
FsOpenEntry(
    _In_  FileSystemDescriptor_t*   FileSystem,
    _In_  MString_t*                Path,
    _Out_ FileSystemEntry_t**       BaseEntry)
{
    MfsInstance_t* Mfs = (MfsInstance_t*)FileSystem->ExtensionData;
    OsStatus_t     Result;
    MfsEntry_t*    Entry;

    Entry = (MfsEntry_t*)malloc(sizeof(MfsEntry_t));
    if (!Entry) {
        return OsInvalidParameters;
    }
    
    memset(Entry, 0, sizeof(MfsEntry_t));
    Result     = MfsLocateRecord(FileSystem, Mfs->MasterRecord.RootIndex, Entry, Path);
    *BaseEntry = (FileSystemEntry_t*)Entry;
    if (Result != OsSuccess) {
        free(Entry);
    }
    return Result;
}

OsStatus_t 
FsCreatePath(
    _In_  FileSystemDescriptor_t* FileSystem,
    _In_  MString_t*              Path,
    _In_  unsigned int            Options,
    _Out_ FileSystemEntry_t**     BaseEntry)
{
    MfsInstance_t* mfs;
    OsStatus_t     osStatus;
    MfsEntry_t*    entry;

    if (!FileSystem || !BaseEntry) {
        return OsInvalidParameters;
    }

    mfs      = (MfsInstance_t*)FileSystem->ExtensionData;
    osStatus = MfsCreateRecord(FileSystem, Options, mfs->MasterRecord.RootIndex, Path, &entry);
    if (osStatus == OsSuccess) {
        *BaseEntry = &entry->Base;
    }
    return osStatus;
}

OsStatus_t
FsCloseEntry(
    _In_ FileSystemDescriptor_t*    FileSystem,
    _In_ FileSystemEntry_t*         BaseEntry)
{
    OsStatus_t  Code  = OsSuccess;
    MfsEntry_t* Entry = (MfsEntry_t*)BaseEntry;
    
    TRACE("FsCloseEntry(%i)", Entry->ActionOnClose);
    if (Entry->ActionOnClose) {
        Code = MfsUpdateRecord(FileSystem, Entry, Entry->ActionOnClose);
    }
    if (BaseEntry->Name != NULL) { MStringDestroy(BaseEntry->Name); }
    if (BaseEntry->Path != NULL) { MStringDestroy(BaseEntry->Path); }
    free(Entry);
    return Code;
}

OsStatus_t
FsDeleteEntry(
    _In_ FileSystemDescriptor_t*    FileSystem,
    _In_ FileSystemEntryHandle_t*   BaseHandle)
{
    MfsEntryHandle_t* Handle  = (MfsEntryHandle_t*)BaseHandle;
    MfsEntry_t*       Entry   = (MfsEntry_t*)Handle->Base.Entry;
    OsStatus_t        Code;
    OsStatus_t        Status;

    Status = MfsFreeBuckets(FileSystem, Entry->StartBucket, Entry->StartLength);
    if (Status != OsSuccess) {
        ERROR("Failed to free the buckets at start 0x%x, length 0x%x",
            Entry->StartBucket, Entry->StartLength);
        return OsDeviceError;
    }

    Code = MfsUpdateRecord(FileSystem, Entry, MFS_ACTION_DELETE);
    if (Code == OsSuccess) {
        Code = FsCloseHandle(FileSystem, BaseHandle);
        if (Code == OsSuccess) {
            Code = FsCloseEntry(FileSystem, &Entry->Base);
        }
    }
    return Code;
}

OsStatus_t
FsOpenHandle(
    _In_  FileSystemDescriptor_t*   FileSystem,
    _In_  FileSystemEntry_t*        BaseEntry,
    _Out_ FileSystemEntryHandle_t** BaseHandle)
{
    MfsEntry_t*         Entry   = (MfsEntry_t*)BaseEntry;
    MfsEntryHandle_t*   Handle;

    Handle = (MfsEntryHandle_t*)malloc(sizeof(MfsEntryHandle_t));
    if (!Handle) {
        return OsInvalidParameters;
    }
    
    memset(Handle, 0, sizeof(MfsEntryHandle_t));
    Handle->BucketByteBoundary = 0;
    Handle->DataBucketPosition = Entry->StartBucket;
    Handle->DataBucketLength   = Entry->StartLength;
    *BaseHandle                = &Handle->Base;
    return OsSuccess;
}

OsStatus_t
FsCloseHandle(
    _In_ FileSystemDescriptor_t*    FileSystem,
    _In_ FileSystemEntryHandle_t*   BaseHandle)
{
    MfsEntryHandle_t* Handle = (MfsEntryHandle_t*)BaseHandle;
    free(Handle);
    return OsSuccess;
}

OsStatus_t
FsReadEntry(
    _In_  FileSystemDescriptor_t*   FileSystem,
    _In_  FileSystemEntryHandle_t*  BaseHandle,
    _In_  UUId_t                    BufferHandle,
    _In_  void*                     Buffer,
    _In_  size_t                    BufferOffset,
    _In_  size_t                    UnitCount,
    _Out_ size_t*                   UnitsRead)
{
    MfsEntryHandle_t* Handle = (MfsEntryHandle_t*)BaseHandle;
    TRACE("FsReadEntry(flags 0x%x, length %u)", Handle->Base.Entry->Descriptor.Flags, UnitCount);
    if (Handle->Base.Entry->Descriptor.Flags & FILE_FLAG_DIRECTORY) {
        return FsReadFromDirectory(FileSystem, Handle, BufferHandle, Buffer, BufferOffset, UnitCount, UnitsRead);
    }
    else {
        return FsReadFromFile(FileSystem, Handle, BufferHandle, Buffer, BufferOffset, UnitCount, UnitsRead);
    }
}

OsStatus_t
FsWriteEntry(
    _In_  FileSystemDescriptor_t*   FileSystem,
    _In_  FileSystemEntryHandle_t*  BaseHandle,
    _In_  UUId_t                    BufferHandle,
    _In_  void*                     Buffer,
    _In_  size_t                    BufferOffset,
    _In_  size_t                    UnitCount,
    _Out_ size_t*                   UnitsWritten)
{
    MfsEntryHandle_t* Handle = (MfsEntryHandle_t*)BaseHandle;
    TRACE("FsWriteEntry(flags 0x%x, length %u)", Handle->Base.Entry->Descriptor.Flags, UnitCount);
    if (!(Handle->Base.Entry->Descriptor.Flags & FILE_FLAG_DIRECTORY)) {
        return FsWriteToFile(FileSystem, Handle, BufferHandle, Buffer, BufferOffset, UnitCount, UnitsWritten);
    }
    return OsInvalidParameters;
}

OsStatus_t
FsSeekInEntry(
    _In_ FileSystemDescriptor_t*    FileSystem,
    _In_ FileSystemEntryHandle_t*   BaseHandle,
    _In_ uint64_t                   AbsolutePosition)
{
    MfsEntryHandle_t* Handle = (MfsEntryHandle_t*)BaseHandle;
    if (Handle->Base.Entry->Descriptor.Flags & FILE_FLAG_DIRECTORY) {
        return FsSeekInDirectory(FileSystem, Handle, AbsolutePosition);
    }
    else {
        return FsSeekInFile(FileSystem, Handle, AbsolutePosition);
    }
}

OsStatus_t
FsDestroy(
    _In_ FileSystemDescriptor_t* Descriptor,
    _In_ unsigned int                 UnmountFlags)
{
    MfsInstance_t *Mfs;

    if (!Descriptor) {
        return OsInvalidParameters;
    }

    Mfs = (MfsInstance_t*)Descriptor->ExtensionData;
    if (Mfs == NULL) {
        return OsInvalidParameters;
    }

    // Which kind of unmount is it?
    if (!(UnmountFlags & 0x1)) {
        // Flush everything
        // @todo
    }

    // Cleanup all allocated resources
    if (Mfs->TransferBuffer.buffer != NULL) {
        dma_attachment_unmap(&Mfs->TransferBuffer);
        dma_detach(&Mfs->TransferBuffer);
    }

    // Free the bucket-map
    if (Mfs->BucketMap != NULL) {
        free(Mfs->BucketMap);
    }

    // Free structure and return
    free(Mfs);
    Descriptor->ExtensionData = NULL;
    return OsSuccess;
}

void
FsInitializeRootRecord(
    _In_ MfsInstance_t* FileSystem)
{
    memset(&FileSystem->RootRecord, 0, sizeof(FileRecord_t));
    memcpy(&FileSystem->RootRecord.Name[0], RootEntryName, strlen(RootEntryName));
    
    FileSystem->RootRecord.Flags = MFS_FILERECORD_INUSE | MFS_FILERECORD_LOCKED | 
        MFS_FILERECORD_SYSTEM | MFS_FILERECORD_DIRECTORY;
    FileSystem->RootRecord.StartBucket = FileSystem->MasterRecord.RootIndex;
    FileSystem->RootRecord.StartLength = MFS_ROOTSIZE;
}

OsStatus_t
FsInitialize(
    _In_ FileSystemDescriptor_t* Descriptor)
{
    MasterRecord_t* masterRecord;
    BootRecord_t*   bootRecord;
    MfsInstance_t*  mfsInstance;
    uint8_t*        bMap;
    uint64_t        bytesRead;
    uint64_t        bytesLeft;
    OsStatus_t      osStatus;
    size_t          i, imax;
    size_t          sectorsTransferred;
    
    struct dma_buffer_info bufferInfo = { 0 };

    TRACE("FsInitialize()");

    mfsInstance = (MfsInstance_t*)malloc(sizeof(MfsInstance_t));
    if (!mfsInstance) {
        ERROR("Failed to allocate memory for mfs variable");
        osStatus = OsOutOfMemory;
        goto error_exit;
    }
    memset(mfsInstance, 0, sizeof(MfsInstance_t));
    
    // Create a generic transferbuffer for us to use
    bufferInfo.length   = Descriptor->Disk.descriptor.SectorSize;
    bufferInfo.capacity = Descriptor->Disk.descriptor.SectorSize;
    bufferInfo.flags    = 0;

    osStatus = dma_create(&bufferInfo, &mfsInstance->TransferBuffer);
    if (osStatus != OsSuccess) {
        free(mfsInstance);
        return osStatus;
    }

    // Read the boot-sector
    if (MfsReadSectors(Descriptor, mfsInstance->TransferBuffer.handle, 0, 0,
                       1, &sectorsTransferred) != OsSuccess) {
        ERROR("Failed to read mfs boot-sector record");
        goto error_exit;
    }

    Descriptor->ExtensionData = (uintptr_t*)mfsInstance;
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

    // Calculate where our map sector is
    mfsInstance->BucketCount = Descriptor->SectorCount / mfsInstance->SectorsPerBucket;
    
    // Bucket entries are 64 bit (8 bytes) in map
    mfsInstance->BucketsPerSectorInMap = Descriptor->Disk.descriptor.SectorSize / 8;

    // Read the master-record
    if (MfsReadSectors(Descriptor, mfsInstance->TransferBuffer.handle, 0,
                       mfsInstance->MasterRecordSector, 1, &sectorsTransferred) != OsSuccess) {
        ERROR("Failed to read mfs master-sectofferfferr record");
        osStatus = OsError;
        goto error_exit;
    }
    masterRecord                       = (MasterRecord_t*)mfsInstance->TransferBuffer.buffer;

    // Process the master-record
    if (masterRecord->Magic != MFS_BOOTRECORD_MAGIC) {
        ERROR("Failed to validate master-record signature (0x%x, expected 0x%x)",
              masterRecord->Magic, MFS_BOOTRECORD_MAGIC);
        osStatus = OsInvalidParameters;
        goto error_exit;
    }
    TRACE("Partition-name: %s", &MasterRecord->PartitionName[0]);
    memcpy(&mfsInstance->MasterRecord, masterRecord, sizeof(MasterRecord_t));
    dma_attachment_unmap(&mfsInstance->TransferBuffer);
    dma_detach(&mfsInstance->TransferBuffer);

    // Create a new transfer buffer that is more persistant and attached to the fs
    // and will provide the primary intermediate buffer for general usage.
    bufferInfo.length   = mfsInstance->SectorsPerBucket * Descriptor->Disk.descriptor.SectorSize * MFS_ROOTSIZE;
    bufferInfo.capacity = mfsInstance->SectorsPerBucket * Descriptor->Disk.descriptor.SectorSize * MFS_ROOTSIZE;
    osStatus = dma_create(&bufferInfo, &mfsInstance->TransferBuffer);
    if (osStatus != OsSuccess) {
        free(mfsInstance);
        return osStatus;
    }
    
    TRACE("Caching bucket-map (Sector %u - Size %u Bytes)",
        LODWORD(mfsInstance->MasterRecord.MapSector),
        LODWORD(mfsInstance->MasterRecord.MapSize));

    // Load map
    mfsInstance->BucketMap = (uint32_t*)malloc((size_t)mfsInstance->MasterRecord.MapSize + Descriptor->Disk.descriptor.SectorSize);
    bMap      = (uint8_t*)mfsInstance->BucketMap;
    bytesLeft = mfsInstance->MasterRecord.MapSize;
    bytesRead = 0;
    i         = 0;
    imax      = DIVUP(bytesLeft, (mfsInstance->SectorsPerBucket * Descriptor->Disk.descriptor.SectorSize));

#ifdef CACHE_SEGMENTED
    while (bytesLeft) {
        uint64_t mapSector    = mfsInstance->MasterRecord.MapSector + (i * mfsInstance->SectorsPerBucket);
        size_t   transferSize = MIN((mfsInstance->SectorsPerBucket * Descriptor->Disk.descriptor.SectorSize), (size_t)bytesLeft);
        size_t   sectorCount  = DIVUP(transferSize, Descriptor->Disk.descriptor.SectorSize);

        osStatus = MfsReadSectors(Descriptor, mfsInstance->TransferBuffer.handle, 0,
                                  mapSector, sectorCount, &sectorsTransferred);
        if (osStatus != OsSuccess) {
            ERROR("Failed to read sector 0x%x (map) into cache", LODWORD(mapSector));
            goto error_exit;
        }

        if (sectorsTransferred != sectorCount) {
            ERROR("Read %u sectors instead of %u from sector %u",
                  LODWORD(sectorsTransferred), LODWORD(sectorCount), LODWORD(mapSector));
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
            WARNING("Cached %u/%u bytes of sector-map", LODWORD(bytesRead), LODWORD(mfsInstance->MasterRecord.MapSize));
        }
    }
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
    FsDestroy(Descriptor, 0);
    return osStatus;
}
