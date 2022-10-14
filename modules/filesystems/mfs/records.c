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

#define __TRACE

#include <ddk/utils.h>
#include <fs/common.h>
#include "mfs.h"
#include <stdlib.h>
#include <string.h>

// /test/ => [remainingPath ], [token ]
static void __ExtractPathToken(
    _In_  mstring_t*  path,
    _Out_ mstring_t** remainingPath,
    _Out_ mstring_t** token)
{
    int strIndex;
    int strLength;
    TRACE("__ExtractPathToken(path=%ms)", path);

    // Step 1 is to extract the next token we're searching for in this directory
    // we do also detect if that is the last token
    strIndex  = mstr_find_u8(path, "/", 0);
    strLength = mstr_len(path);

    // So, if StrIndex is -1 now, we
    // can pretty much assume this was the last token
    // unless that StrIndex == Last character
    if (strIndex == -1 || strIndex == (int)(strLength - 1)) {
        if (strIndex == (int)(strLength - 1) && strIndex != 0) {
            *token = mstr_substr(path, 0, strIndex);
        }
        else if (strIndex != 0) {
            *token = mstr_clone(path);
        }
        else {
            *token = NULL;
        }

        *remainingPath = NULL;
        TRACE("__ExtractPathToken returns remainingPath=NULL, token=%ms [0x%" PRIxIN "]", *token, token);
        return;
    }

    *token         = mstr_substr(path, 0, strIndex);
    *remainingPath = mstr_substr(path, strIndex + 1, (strLength - (strIndex + 1)));
    TRACE("__ExtractPathToken returns remainingPath=%ms [0x%" PRIxIN "], token=%ms [0x%" PRIxIN "]",
          *remainingPath, remainingPath, *token, token);
}

static inline void __StoreRecord(
        _In_ FileSystemMFS_t* mfs,
        _In_ FileRecord_t*    record,
        _In_ uint32_t         currentBucket,
        _In_ uint32_t         bucketLength,
        _In_ size_t           bucketIndex,
        _In_ MFSEntry_t*      entry)
{
    MfsFileRecordToVfsFile(mfs, record, entry);

    // Save where in the directory we found it
    entry->DirectoryBucket = currentBucket;
    entry->DirectoryLength = bucketLength;
    entry->DirectoryIndex  = bucketIndex;
}

static oserr_t __ReadCurrentBucket(
        _In_ FileSystemMFS_t* mfs,
        _In_ uint32_t         currentBucket,
        _In_ MapRecord_t*     mapRecord)
{
    oserr_t osStatus;
    size_t  sectorsTransferred;

    // Get the length of the bucket
    osStatus = MfsGetBucketLink(mfs, currentBucket, mapRecord);
    if (osStatus != OsOK) {
        ERROR("__ReadCurrentBucket: failed to get length of bucket %u", currentBucket);
        return osStatus;
    }

    if (!mapRecord->Length) {
        ERROR("__ReadCurrentBucket: length of bucket %u was 0", currentBucket);
        return OsError;
    }

    // Start out by loading the bucket buffer with data
    osStatus = FSStorageRead(
            &mfs->Storage,
            mfs->TransferBuffer.handle,
            0,
            &(UInteger64_t) { .QuadPart = MFS_GETSECTOR(mfs, currentBucket) },
            MFS_SECTORCOUNT(mfs, mapRecord->Length),
            &sectorsTransferred
    );
    if (osStatus != OsOK) {
        ERROR("__ReadCurrentBucket: failed to read directory-bucket %u", currentBucket);
    }

    return osStatus;
}

static oserr_t __ExpandDirectory(
        _In_ FileSystemMFS_t* mfs,
        _In_ uint32_t         currentBucket,
        _In_ MapRecord_t*     mapRecord)
{
    oserr_t osStatus;

    // Allocate bucket
    osStatus = MfsAllocateBuckets(mfs, MFS_DIRECTORYEXPANSION, mapRecord);
    if (osStatus != OsOK) {
        ERROR("__ExpandDirectory failed to allocate bucket for expansion");
        return osStatus;
    }

    // Update link
    osStatus = MfsSetBucketLink(mfs, currentBucket, mapRecord, 1);
    if (osStatus != OsOK) {
        ERROR("__ExpandDirectory failed to update bucket-link for expansion");
        return osStatus;
    }

    // Zero the bucket
    osStatus = MfsZeroBucket(mfs, mapRecord->Link, mapRecord->Length);
    if (osStatus != OsOK) {
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
 * @return                  OsExists if entry with <entryName> was found, OsNotExists if a free entry was found.
 *                          Any other Os* value is indicative of an error.
 */
static oserr_t __FindEntryOrFreeInDirectoryBucket(
        _In_ FileSystemMFS_t* mfs,
        _In_ uint32_t         bucketOfDirectory,
        _In_ mstring_t*       entryName,
        _In_ int              allowExpansion,
        _In_ MFSEntry_t*      resultEntry)
{
    uint32_t currentBucket = bucketOfDirectory;
    oserr_t  osStatus;

    // iterate untill end of folder with two tasks in mind, either find matching entry
    // or one thats free so we can create it
    while (1) {
        FileRecord_t* record = NULL;
        MapRecord_t   link;
        int           exitLoop = 0;

        osStatus = __ReadCurrentBucket(mfs, currentBucket, &link);
        if (osStatus != OsOK) {
            ERROR("__FindEntryOrFreeInDirectoryBucket failed to read directory bucket");
            break;
        }

        // Iterate the number of records in a bucket
        // A record spans two sectors
        record = (FileRecord_t*)mfs->TransferBuffer.buffer;
        for (size_t i = 0; i < ((mfs->SectorsPerBucket * link.Length) / 2); i++) {
            mstring_t* filename;
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
            filename      = mstr_new_u8((const char*)&record->Name[0]);
            compareResult = mstr_icmp(entryName, filename);
            //TRACE("__FindEntryOrFreeInDirectoryBucket matching token %s == %s: %i",
            //      MStringRaw(entryName), MStringRaw(filename), compareResult);
            mstr_delete(filename);

            if (!compareResult) {
                // it was end of path, and the entry exists
                __StoreRecord(mfs, record, currentBucket, link.Length, i, resultEntry);
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
                osStatus = __ExpandDirectory(mfs, currentBucket, &link);
                if (osStatus != OsOK) {
                    ERROR("__FindEntryOrFreeInDirectoryBucket failed to expand directory");
                    break;
                }

                // update free record to reflect the first entry in new bucket
                resultEntry->DirectoryBucket = link.Link;
                resultEntry->DirectoryLength = link.Length;
                resultEntry->DirectoryIndex  = 0;
            }
            else {
                osStatus = OsNotExists;
            }
            break;
        }

        // Update current bucket pointer
        currentBucket = link.Link;
    }
    return osStatus;
}

static MFSEntry_t* __MFSEntryNew(
        _In_  mstring_t*       name,
        _In_  uint32_t         owner,
        _In_  uint32_t         flags,
        _In_  uint32_t         permissions)
{
    MFSEntry_t* entry = malloc(sizeof(MFSEntry_t));
    if (entry == NULL) {
        return NULL;
    }
    memset(entry, 0, sizeof(MFSEntry_t));

    entry->Name = mstr_clone(name);
    if (entry->Name == NULL) {
        free(entry);
        return NULL;
    }

    unsigned int nativeFlags = MfsVfsFlagsToFileRecordFlags(flags, permissions);
    entry->StartBucket = MFS_ENDOFCHAIN;
    entry->NativeFlags = nativeFlags | MFS_FILERECORD_INUSE;
    entry->Owner = owner;
    return entry;
}

static oserr_t __CreateEntryInDirectory(
        _In_  FileSystemMFS_t* mfs,
        _In_  mstring_t*       name,
        _In_  uint32_t         owner,
        _In_  uint32_t         flags,
        _In_  uint32_t         permissions,
        _In_  uint32_t         directoryBucket,
        _In_  uint32_t         directoryLength,
        _In_  size_t           directoryIndex,
        _Out_ MFSEntry_t**     entryOut)
{
    MFSEntry_t*  entry;
    oserr_t      osStatus;

    entry = __MFSEntryNew(name, owner, flags, permissions);
    if (entry == NULL) {
        return OsOutOfMemory;
    }

    entry->DirectoryBucket = directoryBucket;
    entry->DirectoryLength = directoryLength;
    entry->DirectoryIndex = directoryIndex;

    osStatus = MfsUpdateRecord(mfs, entry, MFS_ACTION_CREATE);
    if (osStatus != OsOK) {
        mstr_delete(entry->Name);
        free(entry);
        return osStatus;
    }
    *entryOut = entry;
    return OsOK;
}

oserr_t
MfsLocateRecord(
        _In_ FileSystemMFS_t* mfs,
        _In_ uint32_t         bucketOfDirectory,
        _In_ MFSEntry_t*      entry,
        _In_ mstring_t*       path)
{
    oserr_t              osStatus;
    mstring_t*           remainingPath = NULL;
    mstring_t*           currentToken  = NULL;
    MFSEntry_t           nextEntry     = { 0 };
    int                  isEndOfPath   = 0;

    TRACE("MfsLocateRecord(fileSystem=%ms, bucketOfDirectory=%u, entry=0x%" PRIxIN ", path=%ms)",
          mfs->Label, bucketOfDirectory, entry, path);

    // Either get next part of the path, or end at this entry
    if (mstr_len(path) != 0) {
        __ExtractPathToken(path, &remainingPath, &currentToken);
        if (remainingPath == NULL) {
            isEndOfPath = 1;
            if (currentToken == NULL) {
                MfsFileRecordToVfsFile(mfs, &mfs->RootRecord, entry);
                return OsOK;
            }
        }
    } else {
        // end of path
        MfsFileRecordToVfsFile(mfs, &mfs->RootRecord, entry);
        return OsOK;
    }

    // Iterate untill we reach end of folder
    osStatus = __FindEntryOrFreeInDirectoryBucket(
            mfs, bucketOfDirectory,
            currentToken, 0,
            &nextEntry
    );
    if (osStatus == OsExists) {
        if (!isEndOfPath) {
            if (!(nextEntry.NativeFlags & MFS_FILERECORD_DIRECTORY)) {
                osStatus = OsPathIsNotDirectory;
                goto exit;
            }

            if (nextEntry.StartBucket == MFS_ENDOFCHAIN) {
                osStatus = OsNotExists;
                goto exit;
            }

            osStatus = MfsLocateRecord(mfs, nextEntry.StartBucket, entry, remainingPath);
        }
        else {
            memcpy(entry, &nextEntry, sizeof(MFSEntry_t));
            osStatus = OsOK;
        }
    }

    // Cleanup the allocated strings
exit:
    if (remainingPath != NULL) {
        mstr_delete(remainingPath);
    }
    mstr_delete(currentToken);
    return osStatus;
}

oserr_t
MfsCreateRecord(
        _In_  FileSystemMFS_t* mfs,
        _In_  MFSEntry_t*      entry,
        _In_  mstring_t*       name,
        _In_  uint32_t         owner,
        _In_  uint32_t         flags,
        _In_  uint32_t         permissions,
        _Out_ MFSEntry_t**     entryOut)
{
    oserr_t    osStatus;
    MFSEntry_t nextEntry = { 0 };

    TRACE("MfsCreateRecord(fileSystem=%ms, flags=0x%x, path=%ms)",
          mfs->Label, flags, name);

    osStatus = __FindEntryOrFreeInDirectoryBucket(
            mfs,
            entry->StartBucket,
            name,
            1,
            &nextEntry
    );
    if (osStatus == OsNotExists) {
        osStatus = __CreateEntryInDirectory(
                mfs,
                name,
                owner,
                flags,
                permissions,
                nextEntry.DirectoryBucket,
                nextEntry.DirectoryLength,
                nextEntry.DirectoryIndex,
                entryOut);
    }
    TRACE("MfsCreateRecord returns=%u", osStatus);
    return osStatus;
}
