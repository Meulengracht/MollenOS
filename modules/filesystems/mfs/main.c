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

#include <ddk/services/file.h>
#include <ddk/utils.h>
#include <threads.h>
#include <stdlib.h>
#include <string.h>
#include "mfs.h"

static const char* RootEntryName = "<root>";

// File specific operation handlers
FileSystemCode_t FsReadFromFile(FileSystemDescriptor_t*, MfsEntryHandle_t*, UUId_t, void*, size_t, size_t, size_t*);
FileSystemCode_t FsWriteToFile(FileSystemDescriptor_t*, MfsEntryHandle_t*, UUId_t, void*, size_t, size_t, size_t*);
FileSystemCode_t FsSeekInFile(FileSystemDescriptor_t*, MfsEntryHandle_t*, uint64_t);

// Directory specific operation handlers
FileSystemCode_t FsReadFromDirectory(FileSystemDescriptor_t*, MfsEntryHandle_t*, UUId_t, void*, size_t, size_t, size_t*);
FileSystemCode_t FsSeekInDirectory(FileSystemDescriptor_t*, MfsEntryHandle_t*, uint64_t);

FileSystemCode_t 
FsOpenEntry(
    _In_  FileSystemDescriptor_t*   FileSystem,
    _In_  MString_t*                Path,
    _Out_ FileSystemEntry_t**       BaseEntry)
{
    MfsInstance_t*      Mfs = (MfsInstance_t*)FileSystem->ExtensionData;
    FileSystemCode_t    Result;
    MfsEntry_t*         Entry;

    Entry = (MfsEntry_t*)malloc(sizeof(MfsEntry_t));
    if (!Entry) {
        return FsInvalidParameters;
    }
    
    memset(Entry, 0, sizeof(MfsEntry_t));
    Result     = MfsLocateRecord(FileSystem, Mfs->MasterRecord.RootIndex, Entry, Path);
    *BaseEntry = (FileSystemEntry_t*)Entry;
    if (Result != FsOk) {
        free(Entry);
    }
    return Result;
}

FileSystemCode_t 
FsCreatePath(
    _In_  FileSystemDescriptor_t* FileSystem,
    _In_  MString_t*              Path,
    _In_  Flags_t                 Options,
    _Out_ FileSystemEntry_t**     BaseEntry)
{
    MfsInstance_t*      Mfs = (MfsInstance_t*)FileSystem->ExtensionData;
    FileSystemCode_t    Result;
    MfsEntry_t*         Entry;
    Flags_t             MfsFlags = MfsVfsFlagsToFileRecordFlags(Options, 0);

    Entry = (MfsEntry_t*)malloc(sizeof(MfsEntry_t));
    if (!Entry) {
        return FsInvalidParameters;
    }
    
    memset(Entry, 0, sizeof(MfsEntry_t));
    
    Result = MfsCreateRecord(FileSystem, Mfs->MasterRecord.RootIndex, Entry, Path, MfsFlags);
    *BaseEntry  = (FileSystemEntry_t*)Entry;
    if (Result != FsOk) {
        free(Entry);
    }
    return Result;
}

FileSystemCode_t
FsCloseEntry(
    _In_ FileSystemDescriptor_t*    FileSystem,
    _In_ FileSystemEntry_t*         BaseEntry)
{
    FileSystemCode_t Code   = FsOk;
    MfsEntry_t* Entry       = (MfsEntry_t*)BaseEntry;
    
    TRACE("FsCloseEntry(%i)", Entry->ActionOnClose);
    if (Entry->ActionOnClose) {
        Code = MfsUpdateRecord(FileSystem, Entry, Entry->ActionOnClose);
    }
    if (BaseEntry->Name != NULL) { MStringDestroy(BaseEntry->Name); }
    if (BaseEntry->Path != NULL) { MStringDestroy(BaseEntry->Path); }
    free(Entry);
    return Code;
}

FileSystemCode_t
FsDeleteEntry(
    _In_ FileSystemDescriptor_t*    FileSystem,
    _In_ FileSystemEntryHandle_t*   BaseHandle)
{
    MfsEntryHandle_t*   Handle  = (MfsEntryHandle_t*)BaseHandle;
    MfsEntry_t*         Entry   = (MfsEntry_t*)Handle->Base.Entry;
    FileSystemCode_t    Code;
    OsStatus_t          Status;

    Status = MfsFreeBuckets(FileSystem, Entry->StartBucket, Entry->StartLength);
    if (Status != OsSuccess) {
        ERROR("Failed to free the buckets at start 0x%x, length 0x%x",
            Entry->StartBucket, Entry->StartLength);
        return FsDiskError;
    }

    Code = MfsUpdateRecord(FileSystem, Entry, MFS_ACTION_DELETE);
    if (Code == FsOk) {
        Code = FsCloseHandle(FileSystem, BaseHandle);
        if (Code == FsOk) {
            Code = FsCloseEntry(FileSystem, &Entry->Base);
        }
    }
    return Code;
}

FileSystemCode_t
FsOpenHandle(
    _In_  FileSystemDescriptor_t*   FileSystem,
    _In_  FileSystemEntry_t*        BaseEntry,
    _Out_ FileSystemEntryHandle_t** BaseHandle)
{
    MfsEntry_t*         Entry   = (MfsEntry_t*)BaseEntry;
    MfsEntryHandle_t*   Handle;

    Handle = (MfsEntryHandle_t*)malloc(sizeof(MfsEntryHandle_t));
    if (!Handle) {
        return FsInvalidParameters;
    }
    
    memset(Handle, 0, sizeof(MfsEntryHandle_t));
    Handle->BucketByteBoundary = 0;
    Handle->DataBucketPosition = Entry->StartBucket;
    Handle->DataBucketLength   = Entry->StartLength;
    *BaseHandle                = &Handle->Base;
    return FsOk;
}

FileSystemCode_t
FsCloseHandle(
    _In_ FileSystemDescriptor_t*    FileSystem,
    _In_ FileSystemEntryHandle_t*   BaseHandle)
{
    MfsEntryHandle_t* Handle = (MfsEntryHandle_t*)BaseHandle;
    free(Handle);
    return FsOk;
}

FileSystemCode_t
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

FileSystemCode_t
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
    return FsInvalidParameters;
}

FileSystemCode_t
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
    _In_ Flags_t                 UnmountFlags)
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
    if (!(UnmountFlags & __STORAGE_FORCED_REMOVE)) {
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
    MasterRecord_t* MasterRecord;
    BootRecord_t*   BootRecord;
    MfsInstance_t*  Mfs;
    uint8_t*        bMap;
    uint64_t        BytesRead;
    uint64_t        BytesLeft;
    OsStatus_t      Status;
    size_t          i, imax;
    size_t          SectorsTransferred;
    
    struct dma_buffer_info DmaInfo;

    TRACE("FsInitialize()");

    Mfs = (MfsInstance_t*)malloc(sizeof(MfsInstance_t));
    if (!Mfs) {
        ERROR("Failed to allocate memory for mfs variable");
        Status = OsOutOfMemory;
        goto Error;
    }
    memset(Mfs, 0, sizeof(MfsInstance_t));
    
    // Create a generic transferbuffer for us to use
    DmaInfo.length   = Descriptor->Disk.Descriptor.SectorSize;
    DmaInfo.capacity = Descriptor->Disk.Descriptor.SectorSize;
    DmaInfo.flags    = 0;
    
    Status = dma_create(&DmaInfo, &Mfs->TransferBuffer);
    if (Status != OsSuccess) {
        free(Mfs);
        return Status;
    }

    // Read the boot-sector
    if (MfsReadSectors(Descriptor, Mfs->TransferBuffer.handle, 0, 0, 
            1, &SectorsTransferred) != OsSuccess) {
        ERROR("Failed to read mfs boot-sector record");
        goto Error;
    }

    Descriptor->ExtensionData = (uintptr_t*)Mfs;
    BootRecord                = (BootRecord_t*)Mfs->TransferBuffer.buffer;
    if (BootRecord->Magic != MFS_BOOTRECORD_MAGIC) {
        ERROR("Failed to validate boot-record signature (0x%x, expected 0x%x)",
            BootRecord->Magic, MFS_BOOTRECORD_MAGIC);
        Status = OsInvalidParameters;
        goto Error;
    }
    TRACE("Fs-Version: %u", BootRecord->Version);

    // Store some data from the boot-record
    Mfs->Version                  = (int)BootRecord->Version;
    Mfs->Flags                    = (Flags_t)BootRecord->Flags;
    Mfs->MasterRecordSector       = BootRecord->MasterRecordSector;
    Mfs->MasterRecordMirrorSector = BootRecord->MasterRecordMirror;
    Mfs->SectorsPerBucket         = BootRecord->SectorsPerBucket;

    // Calculate where our map sector is
    Mfs->BucketCount = Descriptor->SectorCount / Mfs->SectorsPerBucket;
    
    // Bucket entries are 64 bit (8 bytes) in map
    Mfs->BucketsPerSectorInMap = Descriptor->Disk.Descriptor.SectorSize / 8;

    // Read the master-record
    if (MfsReadSectors(Descriptor, Mfs->TransferBuffer.handle, 0,
            Mfs->MasterRecordSector, 1, &SectorsTransferred) != OsSuccess) {
        ERROR("Failed to read mfs master-sectofferfferr record");
        Status = OsError;
        goto Error;
    }
    MasterRecord = (MasterRecord_t*)Mfs->TransferBuffer.buffer;

    // Process the master-record
    if (MasterRecord->Magic != MFS_BOOTRECORD_MAGIC) {
        ERROR("Failed to validate master-record signature (0x%x, expected 0x%x)",
            MasterRecord->Magic, MFS_BOOTRECORD_MAGIC);
        Status = OsInvalidParameters;
        goto Error;
    }
    TRACE("Partition-name: %s", &MasterRecord->PartitionName[0]);
    memcpy(&Mfs->MasterRecord, MasterRecord, sizeof(MasterRecord_t));
    dma_attachment_unmap(&Mfs->TransferBuffer);
    dma_detach(&Mfs->TransferBuffer);

    // Create a new transfer buffer that is more persistant and attached to the fs
    // and will provide the primary intermediate buffer for general usage.
    DmaInfo.length   = Mfs->SectorsPerBucket * Descriptor->Disk.Descriptor.SectorSize * MFS_ROOTSIZE;
    DmaInfo.capacity = Mfs->SectorsPerBucket * Descriptor->Disk.Descriptor.SectorSize * MFS_ROOTSIZE;
    Status           = dma_create(&DmaInfo, &Mfs->TransferBuffer);
    if (Status != OsSuccess) {
        free(Mfs);
        return Status;
    }
    
    TRACE("Caching bucket-map (Sector %u - Size %u Bytes)",
        LODWORD(Mfs->MasterRecord.MapSector),
        LODWORD(Mfs->MasterRecord.MapSize));

    // Load map
    Mfs->BucketMap = (uint32_t*)malloc((size_t)Mfs->MasterRecord.MapSize);
    bMap           = (uint8_t*)Mfs->BucketMap;
    BytesLeft      = Mfs->MasterRecord.MapSize;
    BytesRead      = 0;
    i              = 0;
    imax           = DIVUP(BytesLeft, (Mfs->SectorsPerBucket * Descriptor->Disk.Descriptor.SectorSize));
    while (BytesLeft) {
        uint64_t MapSector    = Mfs->MasterRecord.MapSector + (i * Mfs->SectorsPerBucket);
        size_t   TransferSize = MIN((Mfs->SectorsPerBucket * Descriptor->Disk.Descriptor.SectorSize), (size_t)BytesLeft);
        size_t   SectorCount  = DIVUP(TransferSize, Descriptor->Disk.Descriptor.SectorSize);

        if (MfsReadSectors(Descriptor, Mfs->TransferBuffer.handle, 0, 
                MapSector, SectorCount, &SectorsTransferred) != OsSuccess) {
            ERROR("Failed to read sector 0x%x (map) into cache", LODWORD(MapSector));
            goto Error;
        }

        if (SectorsTransferred != SectorCount) {
            ERROR("Read %u sectors instead of %u from sector %u", 
                LODWORD(SectorsTransferred), LODWORD(SectorCount), LODWORD(MapSector));
            goto Error;
        }

        // Reset buffer position to 0 and read the data into the map
        memcpy(bMap, Mfs->TransferBuffer.buffer, TransferSize);
        BytesLeft -= TransferSize;
        BytesRead += TransferSize;
        bMap      += TransferSize;
        i++;
        if (i == (imax / 4) || i == (imax / 2) || i == ((imax / 4) * 3)) {
            WARNING("Cached %u/%u bytes of sector-map", LODWORD(BytesRead), LODWORD(Mfs->MasterRecord.MapSize));
        }
    }
    FsInitializeRootRecord(Mfs);
    return OsSuccess;

Error:
    FsDestroy(Descriptor, 0);
    return Status;
}
