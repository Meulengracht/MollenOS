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

static oserr_t __InitiateDirectory(
        _In_ FileSystemMFS_t* mfs,
        _In_ MFSEntry_t*      entry,
        _In_ uint32_t*        bucketOut)
{
    MapRecord_t record;
    oserr_t     oserr;

    // Allocate bucket
    oserr = MfsAllocateBuckets(mfs, MFS_DIRECTORYEXPANSION, &record);
    if (oserr != OsOK) {
        ERROR("__InitiateDirectory failed to allocate bucket for expansion");
        return oserr;
    }

    // Update the stored record
    oserr = MfsUpdateRecord(mfs, entry, MFS_ACTION_UPDATE);
    if (oserr != OsOK) {
        ERROR("__InitiateDirectory failed to update bucket-link for expansion");
        return oserr;
    }

    // Zero the bucket
    oserr = MfsZeroBucket(mfs, record.Link, record.Length);
    if (oserr != OsOK) {
        ERROR("__InitiateDirectory failed to zero bucket %u", record.Link);
    }

    *bucketOut = record.Link;
    return oserr;
}

/**
 * Finds an entry matching the name given, or returns a free entry. A guarantee of a free entry can only be made
 * if the <allowExpansion> is set to a non-zero value.
 * @param fileSystem        [In] A pointer to an instance of the filesystem data.
 * @param mfs               [In] A pointer to an instance of the mfs data.
 * @param directory         [In] The directory that should be searched.
 * @param entryName         [In] Name of the entry that we are searchin for.
 * @param allowExpansion    [In] Whether or not we can expand the directory if no free entry was found.
 * @param resultEntry       [In] A pointer to an MfsEntry_t structure where the found entry can be stored.
 * @return                  OsExists if entry with <entryName> was found, OsNotExists if a free entry was found.
 *                          Any other Os* value is indicative of an error.
 */
static oserr_t __FindEntryOrFreeInDirectory(
        _In_ FileSystemMFS_t* mfs,
        _In_ MFSEntry_t*      directory,
        _In_ mstring_t*       entryName,
        _In_ int              allowExpansion,
        _In_ MFSEntry_t*      resultEntry)
{
    uint32_t currentBucket = directory->StartBucket;
    oserr_t  oserr;
    TRACE("__FindEntryOrFreeInDirectory(name=%ms, expand=%i)", entryName, allowExpansion);

    // Handle the zero case where a directory is completely empty. In that
    // case a directory will have a Bucket of MFS_ENDOFCHAIN. In that case,
    // if expansion is allowed, we must allocate a new bucket for it first.
    if (currentBucket == MFS_ENDOFCHAIN) {
        if (!allowExpansion) {
            return OsNotExists;
        }
        oserr = __InitiateDirectory(mfs, directory, &currentBucket);
        if (oserr != OsOK) {
            return oserr;
        }
    }

    // Mark the bucket invalid, we use this as a loop indicator
    resultEntry->DirectoryBucket = MFS_ENDOFCHAIN;

    // iterate untill end of folder with two tasks in mind, either find matching entry
    // or one that's free, so we can create it
    while (1) {
        FileRecord_t* record = NULL;
        MapRecord_t   link;
        int           exitLoop = 0;

        oserr = __ReadCurrentBucket(mfs, currentBucket, &link);
        if (oserr != OsOK) {
            ERROR("__FindEntryOrFreeInDirectory failed to read directory bucket");
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
                if (resultEntry->DirectoryBucket == MFS_ENDOFCHAIN) {
                    TRACE("__FindEntryOrFreeInDirectory free entry stored: %u/%llu", currentBucket, i);
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
            TRACE("__FindEntryOrFreeInDirectory matching token %ms == %ms: %i",
                  entryName, filename, compareResult);
            mstr_delete(filename);

            if (!compareResult) {
                TRACE("__FindEntryOrFreeInDirectory found!");
                // it was end of path, and the entry exists
                __StoreRecord(mfs, record, currentBucket, link.Length, i, resultEntry);
                oserr    = OsExists;
                exitLoop = 1;
                break;
            }
            record++;
        }

        if (exitLoop) {
            break;
        }

        // OK when link == MFS_ENDOFCHAIN, then we are at the end of the current directory,
        // and we have to expand it. At this point we can assume that file/directory did not exist
        // and can actually move on to creating the file. If we did not find a free entry while
        // iterating, then we have to expand the directory
        if (link.Link == MFS_ENDOFCHAIN) {
            if (resultEntry->DirectoryBucket == MFS_ENDOFCHAIN && allowExpansion) {
                TRACE("__FindEntryOrFreeInDirectory expanding directory");

                // Expand directory as we have not found a free record
                oserr = __ExpandDirectory(mfs, currentBucket, &link);
                if (oserr != OsOK) {
                    ERROR("__FindEntryOrFreeInDirectory failed to expand directory");
                    break;
                }

                // update free record to reflect the first entry in new bucket
                resultEntry->DirectoryBucket = link.Link;
                resultEntry->DirectoryLength = link.Length;
                resultEntry->DirectoryIndex  = 0;
            } else {
                TRACE("__FindEntryOrFreeInDirectory didn't exist!");
                oserr = OsNotExists;
            }
            break;
        }

        // Update current bucket pointer
        currentBucket = link.Link;
    }
    return oserr;
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
    TRACE("__CreateEntryInDirectory(name=%ms)", name);

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
    TRACE("__CreateEntryInDirectory entry->Name=%ms", entry->Name);
    *entryOut = entry;
    return OsOK;
}

static inline bool __PathIsRoot(mstring_t* path) {
    if (mstr_at(path, 0) == U'/' && mstr_len(path) == 1) {
        return true;
    }
    return false;
}

oserr_t
MfsLocateRecord(
        _In_  FileSystemMFS_t* mfs,
        _In_  MFSEntry_t*      directory,
        _In_  mstring_t*       path,
        _Out_ MFSEntry_t**     entryOut)
{
    oserr_t     oserr;
    MFSEntry_t  currentEntry;
    mstring_t** tokens;
    int         tokenCount;

    TRACE("MfsLocateRecord(fileSystem=%ms, bucketOfDirectory=%u, path=%ms)",
          mfs->Label, directory->Name, path);

    if (__PathIsRoot(path)) {
        MFSEntry_t* entry = __MFSEntryNew(
                mfs->RootEntry.Name,
                mfs->RootEntry.Owner,
                mfs->RootEntry.Flags,
                mfs->RootEntry.Permissions
        );
        if (entry == NULL) {
            return OsOutOfMemory;
        }
        MfsFileRecordToVfsFile(mfs, &mfs->RootRecord, entry);
        *entryOut = entry;
        return OsOK;
    }

    tokenCount = mstr_path_tokens(path, &tokens);
    if (tokenCount <= 0) {
        return OsInvalidParameters;
    }

    // Copy the entry we are looking inside, into currentEntry, we will use that
    // one as the iterator.
    memcpy(&currentEntry, directory, sizeof(MFSEntry_t));

    for (int i = 0; i < tokenCount; i++) {
        mstring_t* token = tokens[i];
        bool       isLast = i == (tokenCount - 1);

        // lookup the token in the current folder, this function will either return
        // OsExists or OsNotExists
        oserr = __FindEntryOrFreeInDirectory(
                mfs, &currentEntry,
                token, 0,
                &currentEntry
        );

        // If the entry was located, we must do some checks
        if (oserr == OsExists) {
            if (isLast) {
                MFSEntry_t* entry = __MFSEntryNew(
                        mfs->RootEntry.Name,
                        mfs->RootEntry.Owner,
                        mfs->RootEntry.Flags,
                        mfs->RootEntry.Permissions
                );
                if (entry == NULL) {
                    return OsOutOfMemory;
                }
                memcpy(entry, &currentEntry, sizeof(MFSEntry_t));
                oserr = OsOK;
                *entryOut = entry;
                break;
            }

            // If we are not at the last entry, then two criterias must be met:
            // 1. The token we found *must* be a directory
            // 2. The directory must have been initialized, otherwise it is empty.
            if (!(currentEntry.NativeFlags & MFS_FILERECORD_DIRECTORY)) {
                oserr = OsPathIsNotDirectory;
                break;
            }

            if (currentEntry.StartBucket == MFS_ENDOFCHAIN) {
                oserr = OsNotExists;
                break;
            }
        } else {
            // Entry was not found, leave the osStatus and break
            break;
        }
    }

    mstrv_delete(tokens);
    return oserr;
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

    osStatus = __FindEntryOrFreeInDirectory(
            mfs,
            entry,
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
