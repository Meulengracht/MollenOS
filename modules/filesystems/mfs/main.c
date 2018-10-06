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
#include "mfs.h"

// File specific operation handlers
FileSystemCode_t FsReadFromFile(FileSystemDescriptor_t*, MfsEntryHandle_t* , DmaBuffer_t*, size_t, size_t*, size_t*);
FileSystemCode_t FsWriteToFile(FileSystemDescriptor_t*, MfsEntryHandle_t*, DmaBuffer_t*, size_t, size_t*);
FileSystemCode_t FsSeekInFile(FileSystemDescriptor_t*, MfsEntryHandle_t*, uint64_t);

// Directory specific operation handlers
FileSystemCode_t FsReadFromDirectory(FileSystemDescriptor_t*, MfsEntryHandle_t* , DmaBuffer_t*, size_t, size_t*, size_t*);
FileSystemCode_t FsSeekInDirectory(FileSystemDescriptor_t*, MfsEntryHandle_t*, uint64_t);

/* FsOpenEntry 
 * Fills the entry structure with information needed to access and manipulate the given path.
 * The entry can be any given type, file, directory, link etc. */
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
    memset(Entry, 0, sizeof(MfsEntry_t));

    Result      = MfsLocateRecord(FileSystem, Mfs->MasterRecord.RootIndex, Entry, Path);
    *BaseEntry  = (FileSystemEntry_t*)Entry;
    if (Result != FsOk) {
        free(Entry);
    }
    return Result;
}

/* FsCreatePath 
 * Creates the path specified and fills the entry structure with similar information as
 * FsOpenEntry. This function (if success) acts like FsOpenEntry. The entry type is specified
 * by options and can be any type. */
FileSystemCode_t 
FsCreatePath(
    _In_  FileSystemDescriptor_t*   FileSystem,
    _In_  MString_t*                Path,
    _In_  Flags_t                   Options,
    _Out_ FileSystemEntry_t**       BaseEntry)
{
    MfsInstance_t*      Mfs = (MfsInstance_t*)FileSystem->ExtensionData;
    FileSystemCode_t    Result;
    MfsEntry_t*         Entry;
    Flags_t             MfsFlags = MfsVfsFlagsToFileRecordFlags(Options, 0);

    Entry = (MfsEntry_t*)malloc(sizeof(MfsEntry_t));
    memset(Entry, 0, sizeof(MfsEntry_t));
    
    Result = MfsCreateRecord(FileSystem, Mfs->MasterRecord.RootIndex, Entry, Path, MfsFlags);
    *BaseEntry  = (FileSystemEntry_t*)Entry;
    if (Result != FsOk) {
        free(Entry);
    }
    return Result;
}

/* FsCloseEntry 
 * Releases resources allocated in the Open/Create function. If entry was opened in
 * exclusive access this is now released. */
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

/* FsDeleteEntry 
 * Deletes the entry specified. If the entry is a directory it must be opened in
 * exclusive access to lock all subentries. Otherwise this can result in zombie handles. 
 * This also acts as a FsCloseHandle and FsCloseEntry. */
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

/* FsOpenHandle 
 * Opens a new handle to a entry, this allows various interactions with the base entry, 
 * like read and write. Neccessary resources and initialization of the Handle
 * should be done here too */
FileSystemCode_t
FsOpenHandle(
    _In_  FileSystemDescriptor_t*   FileSystem,
    _In_  FileSystemEntry_t*        BaseEntry,
    _Out_ FileSystemEntryHandle_t** BaseHandle)
{
    MfsEntry_t*         Entry   = (MfsEntry_t*)BaseEntry;
    MfsEntryHandle_t*   Handle;

    Handle = (MfsEntryHandle_t*)malloc(sizeof(MfsEntryHandle_t));
    memset(Handle, 0, sizeof(MfsEntryHandle_t));

    Handle->BucketByteBoundary  = 0;
    Handle->DataBucketPosition  = Entry->StartBucket;
    Handle->DataBucketLength    = Entry->StartLength;
    *BaseHandle                 = &Handle->Base;
    return FsOk;
}

/* FsCloseHandle 
 * Closes the entry handle and cleans up any resources allocated by the FsOpenHandle equivelent. 
 * Handle is not released by this function but should be cleaned up. */
FileSystemCode_t
FsCloseHandle(
    _In_ FileSystemDescriptor_t*    FileSystem,
    _In_ FileSystemEntryHandle_t*   BaseHandle)
{
    MfsEntryHandle_t* Handle = (MfsEntryHandle_t*)BaseHandle;
    free(Handle);
    return FsOk;
}

/* FsReadEntry 
 * Reads the requested number of units from the entry handle into the supplied buffer. This
 * can be handled differently based on the type of entry. */
FileSystemCode_t
FsReadEntry(
    _In_  FileSystemDescriptor_t*   FileSystem,
    _In_  FileSystemEntryHandle_t*  BaseHandle,
    _In_  DmaBuffer_t*              BufferObject,
    _In_  size_t                    UnitCount,
    _Out_ size_t*                   UnitsAt,
    _Out_ size_t*                   UnitsRead)
{
    MfsEntryHandle_t* Handle = (MfsEntryHandle_t*)BaseHandle;
    TRACE("FsReadEntry(flags 0x%x, length %u)", Handle->Base.Entry->Descriptor.Flags, UnitCount);
    if (Handle->Base.Entry->Descriptor.Flags & FILE_FLAG_DIRECTORY) {
        return FsReadFromDirectory(FileSystem, Handle, BufferObject, UnitCount, UnitsAt, UnitsRead);
    }
    else {
        return FsReadFromFile(FileSystem, Handle, BufferObject, UnitCount, UnitsAt, UnitsRead);
    }
}

/* FsWriteEntry 
 * Writes the requested number of bytes to the given
 * file handle and outputs the number of bytes actually written */
FileSystemCode_t
FsWriteEntry(
    _In_  FileSystemDescriptor_t*   FileSystem,
    _In_  FileSystemEntryHandle_t*  BaseHandle,
    _In_  DmaBuffer_t*              BufferObject,
    _In_  size_t                    Length,
    _Out_ size_t*                   BytesWritten)
{
    MfsEntryHandle_t* Handle = (MfsEntryHandle_t*)BaseHandle;
    TRACE("FsWriteEntry(flags 0x%x, length %u)", Handle->Base.Entry->Descriptor.Flags, Length);
    if (!(Handle->Base.Entry->Descriptor.Flags & FILE_FLAG_DIRECTORY)) {
        return FsWriteToFile(FileSystem, Handle, BufferObject, Length, BytesWritten);
    }
    return FsInvalidParameters;
}

/* FsSeekInEntry 
 * Seeks in the given entry-handle to the absolute position
 * given, must be within boundaries otherwise a seek won't take a place */
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

/* FsDestroy 
 * Destroys the given filesystem descriptor and cleans
 * up any resources allocated by the filesystem instance */
OsStatus_t
FsDestroy(
    _In_ FileSystemDescriptor_t*    Descriptor,
    _In_ Flags_t                    UnmountFlags)
{
    // Variables
    MfsInstance_t *Mfs = NULL;

    // Instantiate the mfs pointer
    Mfs = (MfsInstance_t*)Descriptor->ExtensionData;

    // Sanity
    if (Mfs == NULL) {
        return OsError;
    }

    // Which kind of unmount is it?
    if (!(UnmountFlags & __DISK_FORCED_REMOVE)) {
        // Flush everything
        // @todo
    }

    // Cleanup all allocated resources
    if (Mfs->TransferBuffer != NULL) {
        DestroyBuffer(Mfs->TransferBuffer);
    }

    // Free the bucket-map
    if (Mfs->BucketMap != NULL) {
        free(Mfs->BucketMap);
    }

    // Free structure and return
    free(Mfs);
    return OsSuccess;
}

/* FsInitialize 
 * Initializes a new instance of the file system
 * and allocates resources for the given descriptor */
OsStatus_t
FsInitialize(
    _In_ FileSystemDescriptor_t*    Descriptor)
{
    // Variables
    MasterRecord_t *MasterRecord    = NULL;
    BootRecord_t *BootRecord        = NULL;
    DmaBuffer_t *Buffer              = NULL;
    MfsInstance_t *Mfs              = NULL;
    uint8_t *bMap                   = NULL;
    uint64_t BytesRead              = 0;
    uint64_t BytesLeft              = 0;
    size_t i, imax;

    // Trace
    TRACE("FsInitialize()");

    // Create a generic transferbuffer for us to use
    Buffer = CreateBuffer(UUID_INVALID, Descriptor->Disk.Descriptor.SectorSize);

    // Read the boot-sector
    if (MfsReadSectors(Descriptor, Buffer, 0, 1) != OsSuccess) {
        ERROR("Failed to read mfs boot-sector record");
        goto Error;
    }

    // Allocate a new instance of mfs
    Mfs                         = (MfsInstance_t*)malloc(sizeof(MfsInstance_t));
    Descriptor->ExtensionData   = (uintptr_t*)Mfs;

    // Instantiate the boot-record pointer
    BootRecord                  = (BootRecord_t*)GetBufferDataPointer(Buffer);

    // Process the boot-record
    if (BootRecord->Magic != MFS_BOOTRECORD_MAGIC) {
        ERROR("Failed to validate boot-record signature (0x%x, expected 0x%x)",
            BootRecord->Magic, MFS_BOOTRECORD_MAGIC);
        goto Error;
    }

    // Trace
    TRACE("Fs-Version: %u", BootRecord->Version);

    // Store some data from the boot-record
    Mfs->Version                    = (int)BootRecord->Version;
    Mfs->Flags                      = (Flags_t)BootRecord->Flags;
    Mfs->MasterRecordSector         = BootRecord->MasterRecordSector;
    Mfs->MasterRecordMirrorSector   = BootRecord->MasterRecordMirror;
    Mfs->SectorsPerBucket           = BootRecord->SectorsPerBucket;

    // Calculate where our map sector is
    Mfs->BucketCount                = Descriptor->SectorCount / Mfs->SectorsPerBucket;
    
    // Bucket entries are 64 bit (8 bytes) in map
    Mfs->BucketsPerSectorInMap      = Descriptor->Disk.Descriptor.SectorSize / 8;

    // Read the master-record
    if (MfsReadSectors(Descriptor, Buffer, Mfs->MasterRecordSector, 1) != OsSuccess) {
        ERROR("Failed to read mfs master-sector record");
        goto Error;
    }

    // Instantiate the master-record pointer
    MasterRecord                    = (MasterRecord_t*)GetBufferDataPointer(Buffer);

    // Process the master-record
    if (MasterRecord->Magic != MFS_BOOTRECORD_MAGIC) {
        ERROR("Failed to validate master-record signature (0x%x, expected 0x%x)",
            MasterRecord->Magic, MFS_BOOTRECORD_MAGIC);
        goto Error;
    }

    // Trace
    TRACE("Partition-name: %s", &MasterRecord->PartitionName[0]);

    // Copy the master-record data
    memcpy(&Mfs->MasterRecord, MasterRecord, sizeof(MasterRecord_t));

    // Cleanup the transfer buffer
    DestroyBuffer(Buffer);

    // Allocate a new in the size of a bucket
    Buffer                          = CreateBuffer(UUID_INVALID, Mfs->SectorsPerBucket 
        * Descriptor->Disk.Descriptor.SectorSize * MFS_ROOTSIZE);
    Mfs->TransferBuffer             = Buffer;

    // Allocate a buffer for the map
    Mfs->BucketMap = (uint32_t*)malloc((size_t)Mfs->MasterRecord.MapSize);

    // Trace
    TRACE("Caching bucket-map (Sector %u - Size %u Bytes)",
        LODWORD(Mfs->MasterRecord.MapSector),
        LODWORD(Mfs->MasterRecord.MapSize));

    // Load map
    bMap        = (uint8_t*)Mfs->BucketMap;
    BytesLeft   = Mfs->MasterRecord.MapSize;
    BytesRead   = 0;
    i           = 0;
    imax        = DIVUP(BytesLeft, (Mfs->SectorsPerBucket * Descriptor->Disk.Descriptor.SectorSize)); //GetBufferSize(Buffer)
    while (BytesLeft) {
        // Variables
        uint64_t MapSector = Mfs->MasterRecord.MapSector + (i * Mfs->SectorsPerBucket);
        size_t TransferSize = MIN((Mfs->SectorsPerBucket * Descriptor->Disk.Descriptor.SectorSize), (size_t)BytesLeft);
        size_t SectorCount = DIVUP(TransferSize, Descriptor->Disk.Descriptor.SectorSize);

        // Read sectors
        if (MfsReadSectors(Descriptor, Buffer, MapSector, SectorCount) != OsSuccess) {
            ERROR("Failed to read sector 0x%x (map) into cache", LODWORD(MapSector));
            goto Error;
        }

        // Reset buffer position to 0 and read the data into the map
        SeekBuffer(Buffer, 0);
        ReadBuffer(Buffer, (const void*)bMap, TransferSize, NULL);
        BytesLeft   -= TransferSize;
        BytesRead   += TransferSize;
        bMap        += TransferSize;
        i++;
        if (i == (imax / 4) || i == (imax / 2) || i == ((imax / 4) * 3)) {
            WARNING("Cached %u/%u bytes of sector-map", LODWORD(BytesRead), LODWORD(Mfs->MasterRecord.MapSize));
        }
    }

    // Update the structure
    return OsSuccess;

Error:
    // Cleanup mfs
    if (Mfs != NULL) {
        free(Mfs);
    }

    // Clear extension data
    Descriptor->ExtensionData = NULL;

    // Cleanup the transfer buffer
    DestroyBuffer(Buffer);
    return OsError;
}
