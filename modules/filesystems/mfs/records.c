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

#define __TRACE

#include <ddk/utils.h>
#include "mfs.h"
#include <stdlib.h>
#include <string.h>

// /test/ => [remainingPath ], [token ]
static void __ExtractPathToken(
    _In_  MString_t*  path,
    _Out_ MString_t** remainingPath,
    _Out_ MString_t** token)
{
    int strIndex;
    int strLength;
    TRACE("__ExtractPathToken(path=%s [0x%" PRIxIN "])", MStringRaw(path), path);

    // Step 1 is to extract the next token we searching for in this directory
    // we do also detect if that is the last token
    strIndex  = MStringFind(path, '/', 0);
    strLength = MStringLength(path);

    // So, if StrIndex is MSTRING_NOT_FOUND now, we
    // can pretty much assume this was the last token
    // unless that StrIndex == Last character
    if (strIndex == MSTRING_NOT_FOUND || strIndex == (int)(strLength - 1)) {
        if (strIndex == (int)(strLength - 1) && strIndex != 0) {
            *token = MStringSubString(path, 0, strIndex);
        }
        else if (strIndex != 0) {
            *token = MStringCreate((void*)MStringRaw(path), StrUTF8);
        }
        else {
            *token = NULL;
        }

        *remainingPath = NULL;
        TRACE("__ExtractPathToken returns remainingPath=NULL, token=%s [0x%" PRIxIN "]", MStringRaw(*token), token);
        return;
    }

    *token         = MStringSubString(path, 0, strIndex);
    *remainingPath = MStringSubString(path, strIndex + 1, (strLength - (strIndex + 1)));
    TRACE("__ExtractPathToken returns remainingPath=%s [0x%" PRIxIN "], token=%s [0x%" PRIxIN "]",
          MStringRaw(*remainingPath), remainingPath, MStringRaw(*token), token);
}

static inline void __StoreRecord(
        _In_ FileSystemDescriptor_t* fileSystem,
        _In_ FileRecord_t*           record,
        _In_ uint32_t                currentBucket,
        _In_ uint32_t                bucketLength,
        _In_ size_t                  bucketIndex,
        _In_ MfsEntry_t*             entry)
{
    MfsFileRecordToVfsFile(fileSystem, record, entry);

    // Save where in the directory we found it
    entry->DirectoryBucket = currentBucket;
    entry->DirectoryLength = bucketLength;
    entry->DirectoryIndex  = bucketIndex;
}

static OsStatus_t __ReadCurrentBucket(
        _In_ FileSystemDescriptor_t* fileSystem,
        _In_ MfsInstance_t*          mfs,
        _In_ uint32_t                currentBucket,
        _In_ MapRecord_t*            mapRecord)
{
    OsStatus_t osStatus;
    size_t     sectorsTransferred;

    // Get the length of the bucket
    osStatus = MfsGetBucketLink(fileSystem, currentBucket, mapRecord);
    if (osStatus != OsSuccess) {
        ERROR("__ReadCurrentBucket: failed to get length of bucket %u", currentBucket);
        return osStatus;
    }

    if (!mapRecord->Length) {
        ERROR("__ReadCurrentBucket: length of bucket %u was 0", currentBucket);
        return OsError;
    }

    // Start out by loading the bucket buffer with data
    osStatus = MfsReadSectors(fileSystem, mfs->TransferBuffer.handle, 0, MFS_GETSECTOR(mfs, currentBucket),
                              mfs->SectorsPerBucket * mapRecord->Length, &sectorsTransferred);
    if (osStatus != OsSuccess) {
        ERROR("__ReadCurrentBucket: failed to read directory-bucket %u", currentBucket);
    }

    return osStatus;
}

static OsStatus_t __ExpandDirectory(
        _In_ FileSystemDescriptor_t* fileSystem,
        _In_ uint32_t                currentBucket,
        _In_ MapRecord_t*            mapRecord)
{
    OsStatus_t osStatus;

    // Allocate bucket
    osStatus = MfsAllocateBuckets(fileSystem, MFS_DIRECTORYEXPANSION, mapRecord);
    if (osStatus != OsSuccess) {
        ERROR("__ExpandDirectory failed to allocate bucket for expansion");
        return osStatus;
    }

    // Update link
    osStatus = MfsSetBucketLink(fileSystem, currentBucket, mapRecord, 1);
    if (osStatus != OsSuccess) {
        ERROR("__ExpandDirectory failed to update bucket-link for expansion");
        return osStatus;
    }

    // Zero the bucket
    osStatus = MfsZeroBucket(fileSystem, mapRecord->Link, mapRecord->Length);
    if (osStatus != OsSuccess) {
        ERROR("__ExpandDirectory failed to zero bucket %u", mapRecord->Link);
    }
    return osStatus;
}

/**
 * Finds an entry matching the name given, or returns a free entry. A guarantee of a free entry can only be made
 * if the <allowExpansion> is set to a non-zero value.
 * @param fileSystem        [In] A pointer to an instance of the filesystem data.
 * @param mfs               [In] A pointer to an instance of the mfs data.
 * @param bucketOfDirectory [In] The first data bucket of the directory to search in.
 * @param entryName         [In] Name of the entry that we are searchin for.
 * @param allowExpansion    [In] Whether or not we can expand the directory if no free entry was found.
 * @param resultEntry       [In] A pointer to an MfsEntry_t structure where the found entry can be stored.
 * @return                  OsExists if entry with <entryName> was found, OsDoesNotExist if a free entry was found.
 *                          Any other Os* value is indicative of an error.
 */
static OsStatus_t __FindEntryOrFreeInDirectoryBucket(
        _In_ FileSystemDescriptor_t* fileSystem,
        _In_ MfsInstance_t*          mfs,
        _In_ uint32_t                bucketOfDirectory,
        _In_ MString_t*              entryName,
        _In_ int                     allowExpansion,
        _In_ MfsEntry_t*             resultEntry)
{
    uint32_t   currentBucket = bucketOfDirectory;
    OsStatus_t osStatus;

    // iterate untill end of folder with two tasks in mind, either find matching entry
    // or one thats free so we can create it
    while (1) {
        FileRecord_t* record = NULL;
        MapRecord_t   link;
        int           exitLoop = 0;

        osStatus = __ReadCurrentBucket(fileSystem, mfs, currentBucket, &link);
        if (osStatus != OsSuccess) {
            ERROR("__FindEntryOrFreeInDirectoryBucket failed to read directory bucket");
            break;
        }

        // Iterate the number of records in a bucket
        // A record spans two sectors
        record = (FileRecord_t*)mfs->TransferBuffer.buffer;
        for (size_t i = 0; i < ((mfs->SectorsPerBucket * link.Length) / 2); i++) {
            MString_t* filename;
            int        compareResult;

            // Look for a file-record that's either deleted or
            // if we encounter the end of the file-record table
            if (!(record->Flags & MFS_FILERECORD_INUSE)) {
                if (resultEntry->DirectoryBucket == 0) {
                    resultEntry->DirectoryBucket = currentBucket;
                    resultEntry->DirectoryLength = link.Length;
                    resultEntry->DirectoryIndex  = i;
                }
                record++;
                continue;
            }

            // Convert the filename into a mstring object
            // and try to match it with our token (ignore case)
            filename      = MStringCreate((const char*)&record->Name[0], StrUTF8);
            compareResult = MStringCompare(entryName, filename, 1);
            TRACE("__FindEntryOrFreeInDirectoryBucket matching token %s == %s: %i",
                  MStringRaw(entryName), MStringRaw(filename), compareResult);
            MStringDestroy(filename);

            if (compareResult == MSTRING_FULL_MATCH) {
                // it was end of path, and the entry exists
                __StoreRecord(fileSystem, record, currentBucket, link.Length, i, resultEntry);
                osStatus = OsExists;
                exitLoop = 1;
                break;
            }
            record++;
        }

        if (exitLoop) {
            break;
        }

        // OK when link == MFS_ENDOFCHAIN, then we are at the end of the current directory
        // and we have to expand it. At this point we can assume that file/directory did not exist
        // and can actually move on to creating the file. If we did not find a free entry while
        // iterating, then we have to expand the directory
        if (link.Link == MFS_ENDOFCHAIN) {
            if (resultEntry->DirectoryBucket == 0 && allowExpansion) {
                // Expand directory as we have not found a free record
                osStatus = __ExpandDirectory(fileSystem, currentBucket, &link);
                if (osStatus != OsSuccess) {
                    ERROR("__FindEntryOrFreeInDirectoryBucket failed to expand directory");
                    break;
                }

                // update free record to reflect the first entry in new bucket
                resultEntry->DirectoryBucket = link.Link;
                resultEntry->DirectoryLength = link.Length;
                resultEntry->DirectoryIndex  = 0;
            }
            else {
                osStatus = OsDoesNotExist;
            }
            break;
        }

        // Update current bucket pointer
        currentBucket = link.Link;
    }
    return osStatus;
}

static OsStatus_t __InitiateDirectory(
        _In_ FileSystemDescriptor_t* fileSystem,
        _In_ MfsInstance_t*          mfs,
        _In_ uint32_t                currentBucket,
        _In_ FileRecord_t*           record)
{
    MapRecord_t expansion;
    OsStatus_t  osStatus;
    size_t      sectorsTransferred;

    // Allocate bucket
    osStatus = MfsAllocateBuckets(fileSystem, 1, &expansion);
    if (osStatus != OsSuccess) {
        ERROR("__InitiateDirectory failed to allocate bucket");
        return osStatus;
    }

    // Update record information
    record->StartBucket   = expansion.Link;
    record->StartLength   = expansion.Length;
    record->AllocatedSize = mfs->SectorsPerBucket * fileSystem->Disk.descriptor.SectorSize;

    // Write back record bucket
    osStatus = MfsWriteSectors(fileSystem, mfs->TransferBuffer.handle,
                               0, MFS_GETSECTOR(mfs, currentBucket),
                               mfs->SectorsPerBucket, &sectorsTransferred);
    if (osStatus != OsSuccess) {
        ERROR("__InitiateDirectory failed to update bucket %u", currentBucket);
        return osStatus;
    }

    // Zero the bucket
    osStatus = MfsZeroBucket(fileSystem, record->StartBucket, record->StartLength);
    if (osStatus != OsSuccess) {
        ERROR("__InitiateDirectory failed to zero bucket %u", record->StartBucket);
    }

    return osStatus;
}

static OsStatus_t __CreateEntryInDirectory(
        _In_  FileSystemDescriptor_t* fileSystem,
        _In_  MString_t*              name,
        _In_  unsigned int            flags,
        _In_  uint32_t                directoryBucket,
        _In_  uint32_t                directoryLength,
        _In_  size_t                  directoryIndex,
        _Out_ MfsEntry_t**            entryOut)
{
    MfsEntry_t   entry       = { { { 0 } } };
    unsigned int nativeFlags = MfsVfsFlagsToFileRecordFlags(flags, 0);
    OsStatus_t   osStatus;

    entry.Base.Name = name;
    entry.StartBucket = MFS_ENDOFCHAIN;
    entry.NativeFlags = nativeFlags | MFS_FILERECORD_INUSE;

    entry.DirectoryBucket = directoryBucket;
    entry.DirectoryLength = directoryLength;
    entry.DirectoryIndex = directoryIndex;

    osStatus = MfsUpdateRecord(fileSystem, &entry, MFS_ACTION_CREATE);
    if (osStatus != OsSuccess) {
        return osStatus;
    }

    if (entryOut) {
        *entryOut = malloc(sizeof(MfsEntry_t));
        if (!(*entryOut)) {
            // @todo What to do here, we are out of memory but entry was created
            return OsOutOfMemory;
        }
        memcpy(*entryOut, &entry, sizeof(MfsEntry_t));
    }
    return OsSuccess;
}

static OsStatus_t __CreateDirectory(
        _In_ FileSystemDescriptor_t* fileSystem,
        _In_ MfsInstance_t*          mfs,
        _In_ MString_t*              name,
        _In_ MfsEntry_t*             directoryEntry)
{
    OsStatus_t    osStatus;
    FileRecord_t* record;

    osStatus = __CreateEntryInDirectory(fileSystem, name, MFS_FILERECORD_DIRECTORY,
                                        directoryEntry->DirectoryBucket, directoryEntry->DirectoryLength,
                                        directoryEntry->DirectoryIndex, NULL);
    if (osStatus != OsSuccess) {
        ERROR("__CreateDirectory failed to create directory record");
        return osStatus;
    }

    record   = (FileRecord_t*)((uint8_t*)mfs->TransferBuffer.buffer + (sizeof(FileRecord_t) * directoryEntry->DirectoryIndex));
    osStatus = __InitiateDirectory(fileSystem, mfs, directoryEntry->DirectoryBucket, record);
    if (osStatus != OsSuccess) {
        ERROR("__CreateDirectory failed to initiate new directory record");
    }
    return osStatus;
}

OsStatus_t
MfsLocateRecord(
        _In_ FileSystemDescriptor_t* fileSystem,
        _In_ uint32_t                bucketOfDirectory,
        _In_ MfsEntry_t*             entry,
        _In_ MString_t*              path)
{
    MfsInstance_t* mfs;
    OsStatus_t     osStatus;
    MString_t*     remainingPath = NULL;
    MString_t*     currentToken  = NULL;
    MfsEntry_t     nextEntry = { { { 0 } } };
    int            isEndOfPath = 0;

    TRACE("MfsLocateRecord(fileSystem=0x%" PRIxIN ", bucketOfDirectory=%u, entry=0x%" PRIxIN ", path=%s [0x%" PRIxIN "])",
          fileSystem, bucketOfDirectory, entry, MStringRaw(path), path);

    if (!fileSystem || !entry) {
        return OsInvalidParameters;
    }

    mfs = (MfsInstance_t*)fileSystem->ExtensionData;

    // Either get next part of the path, or end at this entry
    if (MStringLength(path) != 0) {
        __ExtractPathToken(path, &remainingPath, &currentToken);
        if (remainingPath == NULL) {
            isEndOfPath = 1;
            if (currentToken == NULL) {
                MfsFileRecordToVfsFile(fileSystem, &mfs->RootRecord, entry);
                return OsSuccess;
            }
        }
    }
    else {
        // end of path
        MfsFileRecordToVfsFile(fileSystem, &mfs->RootRecord, entry);
        return OsSuccess;
    }

    // Iterate untill we reach end of folder
    osStatus = __FindEntryOrFreeInDirectoryBucket(fileSystem, mfs, bucketOfDirectory,
                                                  currentToken, 0, &nextEntry);
    if (osStatus == OsExists) {
        if (!isEndOfPath) {
            if (!(nextEntry.NativeFlags & MFS_FILERECORD_DIRECTORY)) {
                osStatus = OsPathIsNotDirectory;
                goto exit;
            }

            if (nextEntry.StartBucket == MFS_ENDOFCHAIN) {
                osStatus = OsDoesNotExist;
                goto exit;
            }

            osStatus = MfsLocateRecord(fileSystem, nextEntry.StartBucket, entry, remainingPath);
        }
        else {
            memcpy(entry, &nextEntry, sizeof(MfsEntry_t));
            osStatus = OsSuccess;
        }
    }

    // Cleanup the allocated strings
exit:
    if (remainingPath != NULL) {
        MStringDestroy(remainingPath);
    }
    MStringDestroy(currentToken);
    return osStatus;
}

OsStatus_t
MfsCreateRecord(
    _In_ FileSystemDescriptor_t* fileSystem,
    _In_ unsigned int            flags,
    _In_ uint32_t                bucketOfDirectory,
    _In_ MString_t*              path,
    _In_ MfsEntry_t**            entryOut)
{
    MfsInstance_t* mfs;
    OsStatus_t     osStatus;
    MfsEntry_t     nextEntry = { { { 0 } } };
    MString_t*     remainingPath = NULL;
    MString_t*     currentToken  = NULL;
    int            isEndOfPath = 0;

    TRACE("MfsCreateRecord(fileSystem=0x%" PRIxIN "flags=0x%x, bucketOfDirectory=%u, path=%s [0x%" PRIxIN "])",
          fileSystem, flags, bucketOfDirectory, MStringRaw(path), path);

    if (!fileSystem) {
        return OsInvalidParameters;
    }

    mfs = (MfsInstance_t*)fileSystem->ExtensionData;

    // Get next token
    __ExtractPathToken(path, &remainingPath, &currentToken);
    if (remainingPath == NULL) {
        isEndOfPath = 1;
    }

    osStatus = __FindEntryOrFreeInDirectoryBucket(fileSystem, mfs, bucketOfDirectory, currentToken, 1, &nextEntry);
    if (osStatus == OsDoesNotExist) {
        // if this is not the end of path, recursive create flag must be provided
        if (!isEndOfPath && !(flags & __FILE_CREATE_RECURSIVE)) {
            goto exit;
        }

        // create either a new directory entry
        if (!isEndOfPath) {
            osStatus = __CreateDirectory(fileSystem, mfs, currentToken, &nextEntry);
            if (osStatus != OsSuccess) {
                goto exit;
            }

            osStatus = MfsCreateRecord(fileSystem, flags, nextEntry.StartBucket, remainingPath, entryOut);
        }
        else {
            // Last creation step.
            osStatus = __CreateEntryInDirectory(fileSystem, currentToken, flags,
                                                nextEntry.DirectoryBucket, nextEntry.DirectoryLength,
                                                nextEntry.DirectoryIndex, entryOut);
        }
    }
    else if (osStatus == OsExists) {
        // The token of the path we were given already exists, now several cases here
        // 1) We are at end of path, this means we should just exit with OsExists
        if (isEndOfPath) {
            goto exit;
        }

        // 2) We are still en-route of creation, this means we should follow down IFF
        //    2.1) The entry is a directory
        if (!(nextEntry.NativeFlags & MFS_FILERECORD_DIRECTORY)) {
            osStatus = OsPathIsNotDirectory;
            goto exit;
        }

        // If directory has no data-bucket allocated then initiate the directory
        if (nextEntry.StartBucket == MFS_ENDOFCHAIN) {
            FileRecord_t* record = (FileRecord_t*)((uint8_t*)mfs->TransferBuffer.buffer + (sizeof(FileRecord_t) * nextEntry.DirectoryIndex));
            osStatus = __InitiateDirectory(fileSystem, mfs, nextEntry.DirectoryBucket, record);
            if (osStatus != OsSuccess) {
                ERROR("MfsCreateRecord failed to initiate directory");
                goto exit;
            }
        }
        osStatus = MfsCreateRecord(fileSystem, flags, nextEntry.StartBucket, remainingPath, entryOut);
    }

exit:
    // Cleanup the allocated strings
    if (remainingPath != NULL) {
        MStringDestroy(remainingPath);
    }
    MStringDestroy(currentToken);
    TRACE("MfsCreateRecord returns=%u", osStatus);
    return osStatus;
}
