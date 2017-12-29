/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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

/* Includes
 * - System */
#include <os/utils.h>
#include "mfs.h"

/* Includes
 * - Library */
#include <stdlib.h>
#include <string.h>

/* MfsExtractToken 
 * Path utility to extract the next directory/file path token
 * from the given path. If it's end of path the RemainingPath
 * will be NULL */
OsStatus_t
MfsExtractToken(
	_In_  MString_t*    Path, 
	_Out_ MString_t**   RemainingPath,
	_Out_ MString_t**   Token)
{
	// Variables
	int StrIndex = -1;

	// Step 1 is to extract the next token we searching for in this directory
	// we do also detect if that is the last token
	StrIndex = MStringFind(Path, '/');

	// So, if StrIndex is MSTRING_NOT_FOUND now, we
	// can pretty much assume this was the last token
	// unless that StrIndex == Last character
	if (StrIndex == MSTRING_NOT_FOUND || StrIndex == (int)(MStringLength(Path) - 1)) {
		*Token = MStringCreate((void*)MStringRaw(Path), StrUTF8);
		*RemainingPath = NULL;
		return OsSuccess;
	}
	
	// Create token string
	*Token = MStringSubString(Path, 0, StrIndex);

	// Split rest of string into remaining
	*RemainingPath = MStringSubString(Path, StrIndex + 1,
		(MStringLength(Path) - (StrIndex + 1)));

	// Done
	return OsSuccess;
}

/* MfsLocateRecord
 * Locates a given file-record by the path given, all sub entries must be 
 * directories. File is only allocated and set if the function returns FsOk */
FileSystemCode_t
MfsLocateRecord(
	_In_  FileSystemDescriptor_t*   Descriptor, 
	_In_  uint32_t                  BucketOfDirectory, 
	_In_  MString_t*                Path,
	_Out_ MfsFile_t**               File)
{
	// Variables
	FileSystemCode_t Result     = FsOk;
	MfsInstance_t *Mfs          = NULL;
	MString_t *Remaining        = NULL;
    MString_t *Token            = NULL;
	uint32_t CurrentBucket      = BucketOfDirectory;
	int IsEndOfPath             = 0;
    int IsEndOfFolder           = 0;
	size_t i;

	// Trace
	TRACE("MfsLocateRecord(Directory-Bucket %u, Path %s)",
		BucketOfDirectory, MStringRaw(Path));

	// Instantiate the mfs pointer
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;

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
		if (MfsGetBucketLink(Descriptor, CurrentBucket, &Link) != OsSuccess) {
			ERROR("Failed to get length of bucket %u", CurrentBucket);
			Result = FsDiskError;
			goto Cleanup;
		}

		// Trace
		TRACE("Reading bucket %u with length %u, link 0x%x", 
            CurrentBucket, Link.Length, Link.Link);
		
		// Start out by loading the bucket buffer with data
		if (MfsReadSectors(Descriptor, Mfs->TransferBuffer, MFS_GETSECTOR(Mfs, CurrentBucket), 
			Mfs->SectorsPerBucket * Link.Length) != OsSuccess) {
			ERROR("Failed to read directory-bucket %u", CurrentBucket);
			Result = FsDiskError;
			goto Cleanup;
		}

		// Iterate the number of records in a bucket
		// A record spans two sectors
		Record = (FileRecord_t*)GetBufferData(Mfs->TransferBuffer);
		for (i = 0; i < ((Mfs->SectorsPerBucket * Link.Length) / 2); i++) {
			// Variables
			MString_t *Filename = NULL;
			if (!(Record->Flags & MFS_FILERECORD_INUSE)) { // Skip unused records
				Record++;
				continue;
			}

			// Convert the filename into a mstring object
			// and try to match it with our token (ignore case)
			Filename = MStringCreate(&Record->Name[0], StrUTF8);

			// Trace
			TRACE("Matching token %s to record %s",
				MStringRaw(Token), MStringRaw(Filename));

			// Compare
			if (MStringCompare(Token, Filename, 1) != MSTRING_NO_MATCH) {
				// Two cases, if we are not at end of given path, then this
				// entry must be a directory and it must have data
				if (IsEndOfPath == 0) {
					// Cleanup file-name, we don't need it anymore
					MStringDestroy(Filename);

					// Do sanity checks
					if (!(Record->Flags & MFS_FILERECORD_DIRECTORY)) {
						Result = FsPathIsNotDirectory;
						goto Cleanup;
					}
					if (Record->StartBucket == MFS_ENDOFCHAIN) {
						MStringDestroy(Filename);
						Result = FsPathNotFound;
						goto Cleanup;
					}

					// Trace
					TRACE("Following the trail into bucket %u with the remaining path %s",
						Record->StartBucket, MStringRaw(Remaining));

					// Now search for the next token inside this directory
					Result = MfsLocateRecord(Descriptor, Record->StartBucket,
						Remaining, File);
					goto Cleanup;
				}
				else {
					// Convert the file-record into a mfs-file instance
					*File                   = (MfsFile_t*)malloc(sizeof(MfsFile_t));

					// Initialize the data
					(*File)->Name           = Filename;
					(*File)->Flags          = Record->Flags;
					(*File)->Size           = Record->Size;
					(*File)->AllocatedSize  = Record->AllocatedSize;
					(*File)->StartBucket    = Record->StartBucket;
					(*File)->StartLength    = Record->StartLength;

					// Save where in the directory we found it
					(*File)->DirectoryBucket = CurrentBucket;
					(*File)->DirectoryLength = Link.Length;
					(*File)->DirectoryIndex  = i;
					Result                   = FsOk;
					goto Cleanup;
				}
			}

			// Move on to next record
			Record++;
		}

		// Retrieve the next part of the directory if
		// we aren't at the end of directory
		if (!IsEndOfFolder) {			
			// End of link?
			if (Link.Link == MFS_ENDOFCHAIN) {
				Result          = FsPathNotFound;
				IsEndOfFolder   = 1;
			}
			else {
				CurrentBucket   = Link.Link;
			}
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
	_In_  FileSystemDescriptor_t*   Descriptor, 
	_In_  uint32_t                  BucketOfDirectory, 
	_In_  MString_t*                Path,
	_Out_ MfsFile_t**               File)
{
	// Variables
	FileSystemCode_t Result = FsOk;
	MfsInstance_t *Mfs      = NULL;
	MString_t *Remaining    = NULL;
    MString_t *Token        = NULL;
	uint32_t CurrentBucket  = BucketOfDirectory;
	int IsEndOfFolder       = 0;
    int IsEndOfPath         = 0;
	size_t i;

	// Trace
	TRACE("MfsLocateFreeRecord(Directory-Bucket %u, Path %s)",
		BucketOfDirectory, MStringRaw(Path));

	// Instantiate the mfs pointer
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;

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
		if (MfsGetBucketLink(Descriptor, CurrentBucket, &Link) != OsSuccess) {
			ERROR("Failed to get length of bucket %u", CurrentBucket);
			Result = FsDiskError;
			goto Cleanup;
		}

		// Start out by loading the bucket buffer with data
		if (MfsReadSectors(Descriptor, Mfs->TransferBuffer,
			MFS_GETSECTOR(Mfs, CurrentBucket), 
			Mfs->SectorsPerBucket * Link.Length) != OsSuccess) {
			ERROR("Failed to read directory-bucket %u", CurrentBucket);
			Result = FsDiskError;
			goto Cleanup;
		}

		// Iterate the number of records in a bucket
		// A record spans two sectors
		Record = (FileRecord_t*)GetBufferData(Mfs->TransferBuffer);
		for (i = 0; i < ((Mfs->SectorsPerBucket * Link.Length) / 2); i++) {
			// Variables
			MString_t *Filename = NULL;

			// Look for a file-record that's either deleted or
			// if we encounter the end of the file-record table
			if (!(Record->Flags & MFS_FILERECORD_INUSE)) {
				// Are we at end of path? If we are - we have found our
				// free entry in the file-record-table
				if (IsEndOfPath) {
					*File                   = (MfsFile_t*)malloc(sizeof(MfsFile_t));
					memset(*File, 0, sizeof(MfsFile_t));

					// Store initial stuff, like name
					(*File)->Name           = Token;

					// Store it's position in the directory
					(*File)->DirectoryBucket = CurrentBucket;
					(*File)->DirectoryLength = Link.Length;
					(*File)->DirectoryIndex  = i;

					// Set code and cleanup
					Result                   = FsOk;
					goto Cleanup;
				}
				else {
					Record++;
					continue;
				}
			}

			// Convert the filename into a mstring object
			// and try to match it with our token (ignore case)
			Filename = MStringCreate(&Record->Name[0], StrUTF8);
			if (MStringCompare(Token, Filename, 1)) {
				if (!IsEndOfPath) {
					// Cleanup filename
					MStringDestroy(Filename);

					// Do sanity checks
					if (!(Record->Flags & MFS_FILERECORD_DIRECTORY)) {
						Result = FsPathIsNotDirectory;
						goto Cleanup;
					}

					// If directory has no data-bucket allocated
					// then extend the directory
					if (Record->StartBucket == MFS_ENDOFCHAIN) {
						// Variables
						MapRecord_t Expansion;

						// Allocate bucket
						if (MfsAllocateBuckets(Descriptor, 1, &Expansion) != OsSuccess) {
							ERROR("Failed to allocate bucket");
							Result = FsDiskError;
							goto Cleanup;
						}

						// Update record information
						Record->StartBucket         = Expansion.Link;
						Record->StartLength         = Expansion.Length;
						Record->AllocatedSize       = Mfs->SectorsPerBucket 
							* Descriptor->Disk.Descriptor.SectorSize;

						// Write back record bucket
						if (MfsWriteSectors(Descriptor, Mfs->TransferBuffer,
							MFS_GETSECTOR(Mfs, CurrentBucket), Mfs->SectorsPerBucket) != OsSuccess) {
							ERROR("Failed to update bucket %u", CurrentBucket);
							Result = FsDiskError;
							goto Cleanup;
						}

						// Zero the bucket
						if (MfsZeroBucket(Descriptor, Record->StartBucket, Record->StartLength) != OsSuccess) {
							ERROR("Failed to zero bucket %u", Record->StartBucket);
							Result = FsDiskError;
							goto Cleanup;
						}
					}
					
					// Go recursive with the remaining path
					Result = MfsLocateFreeRecord(Descriptor, Record->StartBucket, 
						Remaining, File);
					goto Cleanup;
				}
				else {
					// Convert the file-record into a mfs-file instance
					*File                   = (MfsFile_t*)malloc(sizeof(MfsFile_t));

					// Initialize the data
					(*File)->Name           = Filename;
					(*File)->Flags          = Record->Flags;
					(*File)->Size           = Record->Size;
					(*File)->AllocatedSize  = Record->AllocatedSize;
					(*File)->StartBucket    = Record->StartBucket;
					(*File)->StartLength    = Record->StartLength;

					// Save where in the directory we found it
					(*File)->DirectoryBucket = CurrentBucket;
					(*File)->DirectoryLength = Link.Length;
					(*File)->DirectoryIndex  = i;
					Result                   = FsOk;
					goto Cleanup;
				}
			}

			// Go to next record
			Record++;
		}

		// Retrieve the next part of the directory if
		// we aren't at the end of directory
		if (!IsEndOfFolder) {
			// End of link?
			// Expand directory
			if (Link.Link == MFS_ENDOFCHAIN) {
				// Allocate bucket
				if (MfsAllocateBuckets(Descriptor, MFS_DIRECTORYEXPANSION, &Link) != OsSuccess) {
					ERROR("Failed to allocate bucket for expansion");
					Result = FsDiskError;
					goto Cleanup;
				}

				// Update link
				if (MfsSetBucketLink(Descriptor, CurrentBucket, &Link, 1) != OsSuccess) {
					ERROR("Failed to update bucket-link for expansion");
					Result = FsDiskError;
					goto Cleanup;
				}

				// Zero the bucket
				if (MfsZeroBucket(Descriptor, Link.Link, Link.Length) != OsSuccess) {
					ERROR("Failed to zero bucket %u", Link.Link);
					Result = FsDiskError;
					goto Cleanup;
				}

				// Update current bucket pointer
				CurrentBucket = Link.Link;
			}
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
	_In_  FileSystemDescriptor_t*   Descriptor,
	_In_  uint32_t                  BucketOfDirectory,
	_In_  MString_t*                Path,
	_In_  Flags_t                   Flags, 
	_Out_ MfsFile_t**               File)
{
	// Locate a free entry, and make sure file does not exist 
	FileSystemCode_t Result = MfsLocateFreeRecord(Descriptor, 
		BucketOfDirectory, Path, File);

	// If it failed either of two things happened 
	// 1) Path was invalid 
	// 2) File exists
	if (Result != FsOk) {
		return Result;
	}

	// Initialize the new file record
	(*File)->StartBucket    = MFS_ENDOFCHAIN;
	(*File)->StartLength    = 0;
	(*File)->Size           = 0;
	(*File)->AllocatedSize  = 0;
	(*File)->Flags          = Flags | MFS_FILERECORD_INUSE;

	// Update the on-disk record with the new data
	return MfsUpdateRecord(Descriptor, *File, MFS_ACTION_CREATE);
}
