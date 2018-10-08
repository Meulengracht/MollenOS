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
 * MollenOS - General File System (MFS) Driver
 *  - Contains the implementation of the MFS driver for mollenos
 */
//#define __TRACE

#include <os/utils.h>
#include "mfs.h"
#include <stdlib.h>
#include <string.h>

/* MfsExtractToken 
 * Path utility to extract the next directory/file path token
 * from the given path. If it's end of path the RemainingPath will be NULL */
OsStatus_t
MfsExtractToken(
    _In_  MString_t*    Path, 
    _Out_ MString_t**   RemainingPath,
    _Out_ MString_t**   Token)
{
    // Step 1 is to extract the next token we searching for in this directory
    // we do also detect if that is the last token
    int StrIndex    = MStringFind(Path, '/', 0);
    int StrLength   = MStringLength(Path);

    // So, if StrIndex is MSTRING_NOT_FOUND now, we
    // can pretty much assume this was the last token
    // unless that StrIndex == Last character
    if (StrIndex == MSTRING_NOT_FOUND || StrIndex == (int)(StrLength - 1)) {
        if (StrIndex == (int)(StrLength - 1) && StrIndex != 0) {
            *Token = MStringSubString(Path, 0, StrIndex);
        }
        else if (StrIndex != 0) {
            *Token = MStringCreate((void*)MStringRaw(Path), StrUTF8);
        }
        else {
            *Token = NULL;
        }
        
        *RemainingPath = NULL;
        return OsSuccess;
    }

    *Token          = MStringSubString(Path, 0, StrIndex);
    *RemainingPath  = MStringSubString(Path, StrIndex + 1, (StrLength - (StrIndex + 1)));
    return OsSuccess;
}

/* MfsLocateRecord
 * Locates a given file-record by the path given, all sub entries must be 
 * directories. File is only allocated and set if the function returns FsOk */
FileSystemCode_t
MfsLocateRecord(
    _In_ FileSystemDescriptor_t*    FileSystem,
    _In_ uint32_t                   BucketOfDirectory,
    _In_ MfsEntry_t*                Entry,
    _In_ MString_t*                 Path)
{
    MfsInstance_t*      Mfs             = (MfsInstance_t*)FileSystem->ExtensionData;
    FileSystemCode_t    Result          = FsOk;
    MString_t*          Remaining       = NULL;
    MString_t*          Token           = NULL;
    uint32_t            CurrentBucket   = BucketOfDirectory;
    int                 IsEndOfPath     = 0;
    int                 IsEndOfFolder   = 0;
    size_t              i;

    TRACE("MfsLocateRecord(Directory-Bucket %u, Path %s)", BucketOfDirectory, MStringRaw(Path));

    if (MStringLength(Path) != 0) {
        MfsExtractToken(Path, &Remaining, &Token);
        if (Remaining == NULL) {
            IsEndOfPath = 1;
            if (Token == NULL) {
                MfsFileRecordToVfsFile(FileSystem, &Mfs->RootRecord, Entry);
                return FsOk;
            }
        }
    }
    else {
        MfsFileRecordToVfsFile(FileSystem, &Mfs->RootRecord, Entry);
        return FsOk;
    }

    // Iterate untill we reach end of folder
    while (!IsEndOfFolder) {
        FileRecord_t *Record = NULL;
        MapRecord_t Link;

        // Get the length of the bucket
        if (MfsGetBucketLink(FileSystem, CurrentBucket, &Link) != OsSuccess) {
            ERROR("Failed to get length of bucket %u", CurrentBucket);
            Result = FsDiskError;
            goto Cleanup;
        }

        TRACE("Reading bucket %u with length %u, link 0x%x", CurrentBucket, Link.Length, Link.Link);
        
        // Start out by loading the bucket buffer with data
        if (MfsReadSectors(FileSystem, Mfs->TransferBuffer, MFS_GETSECTOR(Mfs, CurrentBucket), 
            Mfs->SectorsPerBucket * Link.Length) != OsSuccess) {
            ERROR("Failed to read directory-bucket %u", CurrentBucket);
            Result = FsDiskError;
            goto Cleanup;
        }

        // Iterate the number of records in a bucket
        // A record spans two sectors
        Record = (FileRecord_t*)GetBufferDataPointer(Mfs->TransferBuffer);
        for (i = 0; i < ((Mfs->SectorsPerBucket * Link.Length) / 2); i++) {
            MString_t* Filename;
            int CompareResult;

            if (!(Record->Flags & MFS_FILERECORD_INUSE)) { // Skip unused records
                Record++;
                continue;
            }

            // Convert the filename into a mstring object
            // and try to match it with our token (ignore case)
            Filename        = MStringCreate(&Record->Name[0], StrUTF8);
            CompareResult   = MStringCompare(Token, Filename, 1);
            TRACE("Matching token %s == %s: %i", MStringRaw(Token), MStringRaw(Filename), CompareResult);
            MStringDestroy(Filename);

            if (CompareResult != MSTRING_NO_MATCH) {
                // Two cases, if we are not at end of given path, then this
                // entry must be a directory and it must have data
                if (IsEndOfPath == 0) {
                    if (!(Record->Flags & MFS_FILERECORD_DIRECTORY)) {
                        Result = FsPathIsNotDirectory;
                        goto Cleanup;
                    }
                    if (Record->StartBucket == MFS_ENDOFCHAIN) {
                        Result = FsPathNotFound;
                        goto Cleanup;
                    }

                    TRACE("Following the trail into bucket %u with the remaining path %s",
                        Record->StartBucket, MStringRaw(Remaining));
                    Result = MfsLocateRecord(FileSystem, Record->StartBucket, Entry, Remaining);
                    goto Cleanup;
                }
                else {
                    MfsFileRecordToVfsFile(FileSystem, Record, Entry);

                    // Save where in the directory we found it
                    Entry->DirectoryBucket  = CurrentBucket;
                    Entry->DirectoryLength  = Link.Length;
                    Entry->DirectoryIndex   = i;
                    Result                  = FsOk;
                    goto Cleanup;
                }
            }
            Record++;
        }

        // End of link?
        if (Link.Link == MFS_ENDOFCHAIN) {
            Result          = FsPathNotFound;
            IsEndOfFolder   = 1;
        }
        else {
            CurrentBucket   = Link.Link;
        }
    }

Cleanup:
    // Cleanup the allocated strings
    if (Remaining != NULL) {
        MStringDestroy(Remaining);
    }
    MStringDestroy(Token);
    return Result;
}

/* MfsLocateFreeRecord
 * Very alike to the MfsLocateRecord except instead of locating a file entry
 * it locates a free entry in the last token of the path, and validates the path as it goes */
FileSystemCode_t
MfsLocateFreeRecord(
    _In_ FileSystemDescriptor_t*    FileSystem,
    _In_ uint32_t                   BucketOfDirectory,
    _In_ MfsEntry_t*                Entry,
    _In_ MString_t*                 Path)
{
    MfsInstance_t*      Mfs             = (MfsInstance_t*)FileSystem->ExtensionData;
    FileSystemCode_t    Result          = FsOk;
    MString_t*          Remaining       = NULL;
    MString_t*          Token           = NULL;
    uint32_t            CurrentBucket   = BucketOfDirectory;
    int                 IsEndOfFolder   = 0;
    int                 IsEndOfPath     = 0;
    size_t              i;

    TRACE("MfsLocateFreeRecord(Directory-Bucket %u, Path %s)", BucketOfDirectory, MStringRaw(Path));

    // Get next token
    MfsExtractToken(Path, &Remaining, &Token);
    if (Remaining == NULL) {
        IsEndOfPath = 1;
    }

    // Iterate untill we reach end of folder
    while (!IsEndOfFolder) {
        FileRecord_t *Record = NULL;
        MapRecord_t Link;

        // Get the length of the bucket
        if (MfsGetBucketLink(FileSystem, CurrentBucket, &Link) != OsSuccess) {
            ERROR("Failed to get length of bucket %u", CurrentBucket);
            Result = FsDiskError;
            goto Cleanup;
        }

        // Trace
        TRACE("Reading bucket %u with length %u, link 0x%x", 
            CurrentBucket, Link.Length, Link.Link);
        
        // Start out by loading the bucket buffer with data
        if (MfsReadSectors(FileSystem, Mfs->TransferBuffer, MFS_GETSECTOR(Mfs, CurrentBucket), 
            Mfs->SectorsPerBucket * Link.Length) != OsSuccess) {
            ERROR("Failed to read directory-bucket %u", CurrentBucket);
            Result = FsDiskError;
            goto Cleanup;
        }

        // Iterate the number of records in a bucket
        // A record spans two sectors
        Record = (FileRecord_t*)GetBufferDataPointer(Mfs->TransferBuffer);
        for (i = 0; i < ((Mfs->SectorsPerBucket * Link.Length) / 2); i++) {
            MString_t* Filename;
            int CompareResult;

            // Look for a file-record that's either deleted or
            // if we encounter the end of the file-record table
            if (!(Record->Flags & MFS_FILERECORD_INUSE)) {
                // Are we at end of path? If we are - we have found our
                // free entry in the file-record-table
                if (IsEndOfPath) {
                    // Store initial stuff, like name
                    Entry->Base.Name        = MStringCreate((void*)MStringRaw(Token), StrUTF8);
                    Entry->DirectoryBucket  = CurrentBucket;
                    Entry->DirectoryLength  = Link.Length;
                    Entry->DirectoryIndex   = i;

                    Result = FsOk;
                    goto Cleanup;
                }
                else {
                    Record++;
                    continue;
                }
            }
            
            // Convert the filename into a mstring object
            // and try to match it with our token (ignore case)
            Filename        = MStringCreate(&Record->Name[0], StrUTF8);
            CompareResult   = MStringCompare(Token, Filename, 1);
            TRACE("Matching token %s == %s: %i", MStringRaw(Token), MStringRaw(Filename), CompareResult);
            MStringDestroy(Filename);

            if (CompareResult != MSTRING_NO_MATCH) {
                if (!IsEndOfPath) {
                    if (!(Record->Flags & MFS_FILERECORD_DIRECTORY)) {
                        Result = FsPathIsNotDirectory;
                        goto Cleanup;
                    }

                    // If directory has no data-bucket allocated then extend the directory
                    if (Record->StartBucket == MFS_ENDOFCHAIN) {
                        MapRecord_t Expansion;

                        // Allocate bucket
                        if (MfsAllocateBuckets(FileSystem, 1, &Expansion) != OsSuccess) {
                            ERROR("Failed to allocate bucket");
                            Result = FsDiskError;
                            goto Cleanup;
                        }

                        // Update record information
                        Record->StartBucket         = Expansion.Link;
                        Record->StartLength         = Expansion.Length;
                        Record->AllocatedSize       = Mfs->SectorsPerBucket 
                            * FileSystem->Disk.Descriptor.SectorSize;

                        // Write back record bucket
                        if (MfsWriteSectors(FileSystem, Mfs->TransferBuffer,
                            MFS_GETSECTOR(Mfs, CurrentBucket), Mfs->SectorsPerBucket) != OsSuccess) {
                            ERROR("Failed to update bucket %u", CurrentBucket);
                            Result = FsDiskError;
                            goto Cleanup;
                        }

                        // Zero the bucket
                        if (MfsZeroBucket(FileSystem, Record->StartBucket, Record->StartLength) != OsSuccess) {
                            ERROR("Failed to zero bucket %u", Record->StartBucket);
                            Result = FsDiskError;
                            goto Cleanup;
                        }
                    }
                    
                    TRACE("Following the trail into bucket %u with the remaining path %s",
                        Record->StartBucket, MStringRaw(Remaining));
                    
                    // Go recursive with the remaining path
                    Result = MfsLocateFreeRecord(FileSystem, Record->StartBucket, Entry, Remaining);
                    goto Cleanup;
                }
                else {
                    MfsFileRecordToVfsFile(FileSystem, Record, Entry);

                    // Save where in the directory we found it
                    Entry->DirectoryBucket  = CurrentBucket;
                    Entry->DirectoryLength  = Link.Length;
                    Entry->DirectoryIndex   = i;
                    Result                  = FsPathExists; // Can't create new entry here
                    goto Cleanup;
                }
            }
            Record++;
        }

        // Retrieve the next part of the directory if
        // we aren't at the end of directory
        if (!IsEndOfFolder) {
            // End of link?
            // Expand directory
            if (Link.Link == MFS_ENDOFCHAIN) {
                // Allocate bucket
                if (MfsAllocateBuckets(FileSystem, MFS_DIRECTORYEXPANSION, &Link) != OsSuccess) {
                    ERROR("Failed to allocate bucket for expansion");
                    Result = FsDiskError;
                    goto Cleanup;
                }

                // Update link
                if (MfsSetBucketLink(FileSystem, CurrentBucket, &Link, 1) != OsSuccess) {
                    ERROR("Failed to update bucket-link for expansion");
                    Result = FsDiskError;
                    goto Cleanup;
                }

                // Zero the bucket
                if (MfsZeroBucket(FileSystem, Link.Link, Link.Length) != OsSuccess) {
                    ERROR("Failed to zero bucket %u", Link.Link);
                    Result = FsDiskError;
                    goto Cleanup;
                }
            }

            // Update current bucket pointer
            CurrentBucket = Link.Link;
        }
    }

Cleanup:
    // Cleanup the allocated strings
    if (Remaining != NULL) {
        MStringDestroy(Remaining);
    }
    MStringDestroy(Token);
    return Result;
}

/* MfsCreateRecord
 * Creates a new file-record in a directory. It internally calls MfsLocateFreeRecord to
 * find a viable entry and validate the path */
FileSystemCode_t
MfsCreateRecord(
    _In_ FileSystemDescriptor_t*    FileSystem,
    _In_ uint32_t                   BucketOfDirectory,
    _In_ MfsEntry_t*                Entry,
    _In_ MString_t*                 Path,
    _In_ Flags_t                    Flags)
{
    FileSystemCode_t Result;

    TRACE("MfsCreateRecord(Bucket %u, Path %s, Flags %u)", 
        BucketOfDirectory, MStringRaw(Path), Flags);

    // Locate a free entry, and make sure file does not exist 
    Result = MfsLocateFreeRecord(FileSystem, BucketOfDirectory, Entry, Path);

    // If it failed either of two things happened 
    // 1) Path was invalid 
    // 2) File exists
    if (Result != FsOk) {
        return Result;
    }

    // Initialize the new file record
    Entry->AllocatedSize    = 0;
    Entry->StartBucket      = MFS_ENDOFCHAIN;
    Entry->StartLength      = 0;
    Entry->NativeFlags      = Flags | MFS_FILERECORD_INUSE;
    return MfsUpdateRecord(FileSystem, Entry, MFS_ACTION_CREATE);
}
