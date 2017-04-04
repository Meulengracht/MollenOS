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

/* Includes
 * - System */
#include <os/utils.h>
#include "mfs.h"

/* Includes
 * - Library */
#include <string.h>

/* MfsReadSectors 
 * A wrapper for reading sectors from the disk associated
 * with the file-system descriptor */
OsStatus_t
MfsReadSectors(
	_In_ FileSystemDescriptor_t *Descriptor, 
	_In_ BufferObject_t *Buffer,
	_In_ uint64_t Sector,
	_In_ size_t Count)
{
	// Variables
	uint64_t AbsoluteSector;

	// Calculate the absolute sector
	AbsoluteSector = Descriptor->SectorStart + Sector;

	// Do the actual read
	return DiskRead(Descriptor->Disk.Driver,
		Descriptor->Disk.Device, AbsoluteSector, 
		Buffer->Physical, Count);
}

/* MfsWriteSectors 
 * A wrapper for writing sectors to the disk associated
 * with the file-system descriptor */
OsStatus_t
MfsWriteSectors(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ BufferObject_t *Buffer,
	_In_ uint64_t Sector,
	_In_ size_t Count)
{
	// Variables
	uint64_t AbsoluteSector;

	// Calculate the absolute sector
	AbsoluteSector = Descriptor->SectorStart + Sector;

	// Do the actual read
	return DiskWrite(Descriptor->Disk.Driver,
		Descriptor->Disk.Device, AbsoluteSector,
		Buffer->Physical, Count);
}

/* MfsUpdateMasterRecord
 * master-bucket and it's mirror 
 * by writing the updated stats in our stored data */
OsStatus_t
MfsUpdateMasterRecord(
	_In_ FileSystemDescriptor_t *Descriptor)
{
	// Variables
	MasterRecord_t *MasterRecord = NULL;
	MfsInstance_t *Mfs = NULL;

	// Trace
	TRACE("MfsUpdateMasterRecord()");

	// Instantiate the pointers
	Mfs = (MfsInstance_t*)Descriptor->ExtendedData;

	// Clear buffer
	ZeroBuffer(Mfs->TransferBuffer);

	// Copy data
	WriteBuffer(Mfs->TransferBuffer, &Mfs->MasterRecord, 
		sizeof(MasterRecord_t), NULL);

	// Write the master-record to harddisk
	if (MfsWriteSectors(Descriptor, Mfs->TransferBuffer, Mfs->MasterRecordSector, 1) != OsNoError
		|| MfsWriteSectors(Descriptor, Mfs->TransferBuffer, Mfs->MasterRecordMirrorSector, 1) != OsNoError) {
		ERROR("Failed to write master-record to disk");
		return OsError;
	}

	// Done
	return OsNoError;
}

/* MfsGetBucketLink
 * Looks up the next bucket link by utilizing the cached
 * in-memory version of the bucketmap */
OsStatus_t
MfsGetBucketLink(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ uint32_t Bucket, 
	_Out_ MapRecord_t *Link)
{
	// Variables
	MfsInstance_t *Mfs = NULL;

	// Trace
	TRACE("MfsGetBucketLink(Bucket %u)", Bucket);

	// Instantiate the pointers
	Mfs = (MfsInstance_t*)Descriptor->ExtendedData;

	// Access the entry and update out
	Link->Link = Mfs->BucketMap[(Bucket * 2)];
	Link->Length = Mfs->BucketMap[(Bucket * 2) + 1];

	// Done
	return OsNoError;
}

/* MfsSetBucketLink
 * Updates the next link for the given bucket and flushes
 * the changes to disk */
OsStatus_t 
MfsSetBucketLink(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ uint32_t Bucket, 
	_In_ MapRecord_t *Link,
	_In_ int UpdateLength)
{
	// Variables
	MfsInstance_t *Mfs = NULL;
	uint8_t *BufferOffset;
	size_t SectorOffset;

	// Trace
	TRACE("MfsSetBucketLink(Bucket %u, Link %u)", 
		Bucket, Link->Link);

	// Instantiate the pointers
	Mfs = (MfsInstance_t*)Descriptor->ExtendedData;

	// Update in-memory map first
	Mfs->BucketMap[(Bucket * 2)] = Link->Link;
	if (UpdateLength) {
		Mfs->BucketMap[(Bucket * 2) + 1] = Link->Length;
	}

	// Calculate which sector that is dirty now
	SectorOffset = Bucket / Mfs->BucketsPerSectorInMap;

	// Calculate offset into buffer
	BufferOffset = (uint8_t*)Mfs->BucketMap;
	BufferOffset += (SectorOffset * Descriptor->Disk.Descriptor.SectorSize);

	// Copy a sector's worth of data into the buffer
	ZeroBuffer(Mfs->TransferBuffer);
	WriteBuffer(Mfs->TransferBuffer, BufferOffset, 
		Descriptor->Disk.Descriptor.SectorSize, NULL);

	// Flush buffer to disk
	if (MfsWriteSectors(Descriptor, Mfs->TransferBuffer, 
		Mfs->MasterRecord.MapSector + SectorOffset, 1) != OsNoError) {
		ERROR("Failed to update the given map-sector %u on disk",
			LODWORD(Mfs->MasterRecord.MapSector + SectorOffset));
		return OsError;
	}

	// Done
	return OsNoError;
}

/* MfsAllocateBuckets
 * Allocates the number of requested buckets in the bucket-map
 * if the allocation could not be done, it'll return OsError */
OsStatus_t
MfsAllocateBuckets(
	_In_ FileSystemDescriptor_t *Descriptor, 
	_In_ size_t BucketCount, 
	_Out_ MapRecord_t *RecordResult)
{
	// Variables
	MfsInstance_t *Mfs = NULL;
	uint32_t Bucket, BucketPrevious;
	size_t Counter;

	// Trace
	TRACE("MfsAllocateBuckets(Bucket %u, Link %u)",
		Bucket, Link->Link);

	// Instantiate the pointers
	Mfs = (MfsInstance_t*)Descriptor->ExtendedData;

	// Instantiate out
	RecordResult->Link = Mfs->MasterRecord.FreeBucket;
	RecordResult->Length = 0;

	// Instantiate our varibles
	Counter = BucketCount;
	Bucket = Mfs->MasterRecord.FreeBucket;
	BucketPrevious = 0;

	// Do allocation in a for-loop as bucket-sizes
	// are variable and thus we might need multiple
	// allocations to satisfy the demand
	while (Counter > 0) {
		MapRecord_t Record;

		// Store the current bucket as previous
		BucketPrevious = Bucket;

		// Get next free bucket
		if (MfsGetBucketLink(Descriptor, Bucket, &Record) != OsNoError) {

		}

		// We now have two cases, either the next block is
		// larger than the number of buckets we are asking for
		// or it's smaller
		if (Record.Length > Counter) {
			uint32_t NextFreeBucket = BucketPrevPtr + Counter;
			uint32_t NextFreeCount = BucketLength - Counter;

			// Make sure only to update out once, we just need
			// the initial size, not for each new allocation
			if (RecordResult->Length == 0) {
				RecordResult->Length = Record.Length;
			}

			// We have to adjust now, since we are taking 
			// only a chunk of the available length
			if (MfsSetNextBucket(Fs, BucketPrevPtr, MFS_END_OF_CHAIN, Counter, 1)
				|| MfsSetNextBucket(Fs, NextFreeBucket, BucketPtr, NextFreeCount, 1))
				return -1;

			// Update the master-record and we are done
			Mfs->MasterRecord.FreeBucket = NextFreeBucket;
			return MfsUpdateMasterRecord(Fs);
		}
		else
		{
			// Make sure only to update out once, we just need
			// the initial size, not for each new allocation
			if (RecordResult->Length == 0) {
				RecordResult->Length = Record.Length;
			}

			// Decrease allocation amount
			Counter -= Record.Length;
		}
	}

	/* Update BucketPrevPtr to 0xFFFFFFFF */
	MfsSetNextBucket(Fs, BucketPrevPtr, MFS_END_OF_CHAIN, 0, 0);
	
	// Update the master-record and we are done
	Mfs->MasterRecord.FreeBucket = Bucket;
	return MfsUpdateMasterRecord(Fs);
}

/* Frees an entire chain of buckets
 * that has been allocated for a file */
OsStatus_t
MfsFreeBuckets(
	_In_ FileSystemDescriptor_t *Descriptor, 
	_In_ uint32_t StartBucket,
	_In_ uint32_t StartLength)
{
	// Variables
	MfsInstance_t *Mfs = NULL;
	MapRecord_t Record;
	uint32_t PreviousBucket;

	// Trace
	TRACE("MfsFreeBuckets(Bucket %u, Length %u)",
		StartBucket, StartLength);

	// Instantiate the variables
	Mfs = (MfsInstance_t*)Descriptor->ExtendedData;
	Record.Link = StartBucket;

	// Sanitize params
	if (StartBucket == MFS_ENDOFCHAIN || StartLength == 0) {
		return OsError;
	}

	// Essentially there is two algorithms we can deploy here
	// The quick one - Which is just to add the allocated bucket list
	// to the free and set the last allocated to point to the first free
	// OR there is the slow one that makes sure that buckets are <in order> as
	// they get freed, and gets inserted or extended correctly. This will reduce
	// fragmentation by A LOT

	// So I'm already limited by time due to life, so i'll with the quick

	// Start by iterating to the last bucket
	while (Record.Link != MFS_ENDOFCHAIN) {
		PreviousBucket = Record.Link;
		if (MfsGetBucketLink(Descriptor, Record.Link, &Record) != OsNoError) {
			ERROR("Failed to retrieve the next bucket-link");
			return OsError;
		}
	}

	// Update record
	Record.Link = Mfs->MasterRecord.FreeBucket;

	// Ok, so now update the pointer to free list
	if (MfsSetBucketLink(Descriptor, PreviousBucket, &Record, 0)) {
		ERROR("Failed to update the next bucket-link");
		return OsError;
	}

	// Update initial free bucket
	Mfs->MasterRecord.FreeBucket = StartBucket;

	// As a last step update the master-record
	return MfsUpdateMasterRecord(Descriptor);
}

/* MfsZeroBucket
 * Wipes the given bucket and count with zero values
 * useful for clearing clusters of sectors */
OsStatus_t
MfsZeroBucket(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ uint32_t Bucket,
	_In_ size_t Count)
{
	// Variables
	MfsInstance_t *Mfs = NULL;
	size_t i;

	// Trace
	TRACE("MfsZeroBucket(Bucket %u, Count %u)",
		Bucket, Count);

	// Instantiate the mfs pointer
	Mfs = (MfsInstance_t*)Descriptor->ExtendedData;

	// Reset buffer
	ZeroBuffer(Mfs->TransferBuffer);

	// Iterate the bucket count and reset
	for (i = 0; i < Count; i++) {
		// Calculate the sector
		uint64_t AbsoluteSector = MFS_GETSECTOR(Mfs, Bucket + i);
		if (MfsWriteSectors(Descriptor, Mfs->TransferBuffer, AbsoluteSector, Mfs->SectorsPerBucket) != OsNoError) {
			ERROR("Failed to write bucket to disk");
			return OsError;
		}
	}
	
	// We are done
	return OsNoError;
}

/* Updates a mfs entry in a directory 
 * This is used when we modify size, name
 * access times and flags for convenience */
VfsErrorCode_t MfsUpdateEntry(MCoreFileSystem_t *Fs, MfsFile_t *Handle, int Action)
{
	/* Cast */
	MfsData_t *mData = (MfsData_t*)Fs->ExtendedData;
	VfsErrorCode_t RetCode = VfsOk;
	uint32_t i;

	/* Allocate buffer for data */
	uint8_t *EntryBuffer = (uint8_t*)kmalloc(mData->BucketSize * Fs->SectorSize);

	/* Read in the bucket of where the entry lies */
	if (MfsReadSectors(Fs, mData->BucketSize * Handle->DirBucket,
		EntryBuffer, mData->BucketSize) != RequestNoError)
	{
		RetCode = VfsDiskError;
		goto Done;
	}
	
	/* Cast */
	MfsTableEntry_t *Iterator = (MfsTableEntry_t*)EntryBuffer;

	/* Loop to correct entry */
	for (i = 0; i < Handle->DirOffset; i++)
		Iterator++;

	/* Delete or modify? */
	if (Action == MFS_ACTION_DELETE) {
		/* Clear out, set status deleted */
		memset((void*)Iterator, 0, sizeof(MfsTableEntry_t));
		Iterator->Status = MFS_STATUS_DELETED;
	}
	else {
		/* Update Status? */
		if (Action == MFS_ACTION_CREATE) {
			/* Set Status */
			Iterator->Status = MFS_STATUS_OK;

			/* Set Name */
			memcpy(&Iterator->Name[0], 
				MStringRaw(Handle->Name), MStringSize(Handle->Name));

			/* Null datablock */
			memset(&Iterator->Data[0], 0, 512);
		}

		/* Update Stats */
		Iterator->Flags = Handle->Flags;
		Iterator->StartBucket = Handle->DataBucket;
		Iterator->StartLength = Handle->InitialBucketLength;

		/* Update times when we support it */

		/* Sizes */
		Iterator->Size = Handle->Size;
		Iterator->AllocatedSize = Handle->AllocatedSize;
	}
	
	/* Write it back */
	if (MfsWriteSectors(Fs, mData->BucketSize * Handle->DirBucket,
		EntryBuffer, mData->BucketSize) != RequestNoError)
		RetCode = VfsDiskError;

	/* Done! */
Done:
	kfree(EntryBuffer);
	return RetCode;
}

/* This is our primary searcher function
 * Given a path it validates and locates a 
 * mfs-entry in a directory-path */
MfsFile_t *MfsLocateEntry(MCoreFileSystem_t *Fs, uint32_t DirBucket, MString_t *Path, VfsErrorCode_t *ErrCode)
{
	/* Variables needed for iteration
	 * of the given bucket, this is a recursive function */
	int StrIndex = MSTRING_NOT_FOUND, IsEndOfFolder = 0, IsEndOfPath = 0;
	MfsData_t *mData = (MfsData_t*)Fs->ExtendedData;
	uint32_t CurrentBucket = DirBucket;
	void *EntryBuffer = NULL;
	MString_t *Token = NULL;
	size_t i;

	/* Step 1 is to extract the next token we 
	 * searching for in this directory 
	 * we do also detect if that is the last token */
	StrIndex = MStringFind(Path, '/');

	/* So, if StrIndex is MSTRING_NOT_FOUND now, we 
	 * can pretty much assume this was the last token 
	 * unless that StrIndex == Last character */
	if (StrIndex == MSTRING_NOT_FOUND
		|| StrIndex == (int)(MStringLength(Path) - 1)) {
		IsEndOfPath = 1;
		Token = Path;
	}
	else {
		Token = MStringSubString(Path, 0, StrIndex);
	}

	/* Allocate buffer for data */
	EntryBuffer = kmalloc(mData->BucketSize * Fs->SectorSize);
	 
	/* Let's iterate */
	while (!IsEndOfFolder)
	{
		/* Our entry iterator */
		MfsTableEntry_t *Entry = NULL;

		/* The first thing we do each loop is to load 
		 * the next bucket of this directory */
		if (MfsReadSectors(Fs, mData->BucketSize * CurrentBucket, 
			EntryBuffer, mData->BucketSize) != RequestNoError)
		{
			/* Error */
			LogFatal("MFS1", "LOCATEENTRY: Error reading from disk");
			break;
		}

		/* Iterate buffer */
		Entry = (MfsTableEntry_t*)EntryBuffer;
		for (i = 0; i < (mData->BucketSize / 2); i++)
		{
			/* Sanity, end of table? 
			 * If it's end, we break out of this totally */
			if (Entry->Status == MFS_STATUS_END) {
				IsEndOfFolder = 1;
				break;
			}

			/* Sanity, deleted entry? 
			 * In case of a deleted entry we don't break
			 * we simply just skip it */
			if (Entry->Status == MFS_STATUS_DELETED) {
				/* Go on to next */
				Entry++;
				continue;
			}

			/* Load UTF8 */
			MString_t *NodeName = MStringCreate(Entry->Name, StrUTF8);

			/* If we find a match, and we are at end 
			 * we are done. Otherwise, go deeper */
			if (MStringCompare(Token, NodeName, 1) != MSTRING_NO_MATCH)
			{
				/* Match */
				if (!IsEndOfPath)
				{
					/* This should be a directory */
					if (!(Entry->Flags & MFS_DIRECTORY))
					{
						/* Cleanup */
						kfree(EntryBuffer);
						MStringDestroy(NodeName);
						MStringDestroy(Token);

						/* Path not found ofc */
						*ErrCode = VfsPathIsNotDirectory;
						return NULL;
					}

					/* Sanity the bucket beforehand */
					if (Entry->StartBucket == MFS_END_OF_CHAIN)
					{
						/* Cleanup */
						kfree(EntryBuffer);
						MStringDestroy(NodeName);
						MStringDestroy(Token);

						/* Path not found ofc */
						*ErrCode = VfsPathNotFound;
						return NULL;
					}

					/* Create a new sub-string with rest */
					MString_t *RestOfPath = 
						MStringSubString(Path, StrIndex + 1, 
						(MStringLength(Path) - (StrIndex + 1)));

					/* Go deeper */
					MfsFile_t *Ret = MfsLocateEntry(Fs, Entry->StartBucket, RestOfPath, ErrCode);

					/* Cleanup */
					kfree(EntryBuffer);
					MStringDestroy(RestOfPath);
					MStringDestroy(NodeName);
					MStringDestroy(Token);

					/* Done */
					return Ret;
				}
				else
				{
					/* Yay, proxy data, cleanup, done! */
					MfsFile_t *Ret = (MfsFile_t*)kmalloc(sizeof(MfsFile_t));

					/* Proxy */
					Ret->Name = NodeName;
					Ret->Flags = Entry->Flags;
					Ret->Size = Entry->Size;
					Ret->AllocatedSize = Entry->AllocatedSize;
					Ret->DataBucket = Entry->StartBucket;
					Ret->InitialBucketLength = Entry->StartLength;
					*ErrCode = VfsOk;

					/* Save position */
					Ret->DirBucket = CurrentBucket;
					Ret->DirOffset = i;

					/* Cleanup */
					kfree(EntryBuffer);
					MStringDestroy(Token);

					/* Done */
					return Ret;
				}
			}

			/* Cleanup */
			MStringDestroy(NodeName);

			/* Go on to next */
			Entry++;
		}

		/* Get next bucket */
		if (!IsEndOfFolder)
		{
			uint32_t Unused = 0;
			if (MfsGetNextBucket(Fs, CurrentBucket, &CurrentBucket, &Unused))
				IsEndOfFolder = 1;

			if (CurrentBucket == MFS_END_OF_CHAIN)
				IsEndOfFolder = 1;
		}
	}

	/* Cleanup */
	kfree(EntryBuffer);
	MStringDestroy(Token);
	
	/* If IsEnd is set, we couldn't find it 
	 * If IsEnd is not set, we should not be here... */
	*ErrCode = VfsPathNotFound;
	return NULL;
}

/* Very alike to the MfsLocateEntry
 * except instead of locating a file entry
 * it locates a free entry in the last token of
 * the path, and validates the path as it goes */
MfsFile_t *MfsFindFreeEntry(MCoreFileSystem_t *Fs, uint32_t DirBucket, MString_t *Path, VfsErrorCode_t *ErrCode)
{
	/* Vars */
	MfsData_t *mData = (MfsData_t*)Fs->ExtendedData;
	uint32_t CurrentBucket = DirBucket;
	int IsEnd = 0;
	uint32_t i;

	/* Get token */
	int IsEndOfPath = 0;
	MString_t *Token = NULL;
	int StrIndex = MStringFind(Path, (uint32_t)'/');
	if (StrIndex == -1
		|| StrIndex == (int)(MStringLength(Path) - 1))
	{
		/* Set end, and token as rest of path */
		IsEndOfPath = 1;
		Token = Path;
	}
	else
		Token = MStringSubString(Path, 0, StrIndex);

	/* Allocate buffer for data */
	void *EntryBuffer = kmalloc(mData->BucketSize * Fs->SectorSize);

	/* Let's iterate */
	while (!IsEnd)
	{
		/* Load bucket */
		if (MfsReadSectors(Fs, mData->BucketSize * CurrentBucket,
			EntryBuffer, mData->BucketSize) != RequestNoError)
		{
			/* Error */
			LogFatal("MFS1", "CREATEENTRY: Error reading from disk");
			break;
		}

		/* Iterate buffer */
		MfsTableEntry_t *Entry = (MfsTableEntry_t*)EntryBuffer;
		for (i = 0; i < (mData->BucketSize / 2); i++)
		{
			/* Have we reached end of table? 
			 * Or perhaps a free entry? */
			if (Entry->Status == MFS_STATUS_END
				|| Entry->Status == MFS_STATUS_DELETED)
			{
				/* Yes we have, now two cases 
				 * either this is ok or it is not ok */
				if (IsEndOfPath) 
				{
					/* Save this location */
					MfsFile_t *Ret = (MfsFile_t*)kmalloc(sizeof(MfsFile_t));
					memset(Ret, 0, sizeof(MfsFile_t));

					/* Init */
					Ret->Name = Token;

					/* Save position for entry */
					Ret->DirBucket = CurrentBucket;
					Ret->DirOffset = i;
					*ErrCode = VfsOk;

					/* Cleanup */
					kfree(EntryBuffer);

					/* Done */
					return Ret;
				}
				else {
					if (Entry->Status == MFS_STATUS_END) {
						/* Invalid Path */
						IsEnd = 1;
						break;
					}
					else {
						/* Go on to next */
						Entry++;
						continue;
					}
				}
			}

			/* Load UTF8 */
			MString_t *NodeName = MStringCreate(Entry->Name, StrUTF8);

			/* If we find a match, and we are at end
			* we are done. Otherwise, go deeper */
			if (MStringCompare(Token, NodeName, 1))
			{
				/* Match */
				if (!IsEndOfPath)
				{
					/* This should be a directory */
					if (!(Entry->Flags & MFS_DIRECTORY))
					{
						/* Cleanup */
						kfree(EntryBuffer);
						MStringDestroy(NodeName);
						MStringDestroy(Token);

						/* Path not found ofc */
						*ErrCode = VfsPathIsNotDirectory;
						return NULL;
					}

					/* Sanity the bucket beforehand */
					if (Entry->StartBucket == MFS_END_OF_CHAIN)
					{
						/* Vars */
						uint32_t Unused = 0;

						/* Allocate bucket for directory */
						Entry->StartBucket = mData->FreeIndex;
						Entry->StartLength = 1;
						Entry->AllocatedSize = mData->BucketSize * Fs->SectorSize;
						MfsAllocateBucket(Fs, 1, &Unused);

						/* Write back buffer */
						if (MfsWriteSectors(Fs, mData->BucketSize * CurrentBucket,
							EntryBuffer, mData->BucketSize) != RequestNoError)
						{
							/* Error */
							LogFatal("MFS1", "CREATEENTRY: Error writing to disk");
							
							/* Cleanup */
							kfree(EntryBuffer);
							MStringDestroy(NodeName);
							MStringDestroy(Token);

							/* Path not found ofc */
							*ErrCode = VfsDiskError;
							return NULL;
						}

						/* Zero out new bucket */
						MfsZeroBucket(Fs, Entry->StartBucket, 1);
					}

					/* Create a new sub-string with rest */
					MString_t *RestOfPath =
						MStringSubString(Path, StrIndex + 1,
						(MStringLength(Path) - (StrIndex + 1)));

					/* Go deeper */
					MfsFile_t *Ret = MfsFindFreeEntry(Fs, Entry->StartBucket, RestOfPath, ErrCode);

					/* Cleanup */
					kfree(EntryBuffer);
					MStringDestroy(RestOfPath);
					MStringDestroy(NodeName);
					MStringDestroy(Token);

					/* Done */
					return Ret;
				}
				else
				{
					/* File exists, return data */
					MfsFile_t *Ret = (MfsFile_t*)kmalloc(sizeof(MfsFile_t));

					/* Proxy */
					Ret->Name = NodeName;
					Ret->Flags = Entry->Flags;
					Ret->Size = Entry->Size;
					Ret->AllocatedSize = Entry->AllocatedSize;
					Ret->DataBucket = Entry->StartBucket;
					Ret->InitialBucketLength = Entry->StartLength;
					*ErrCode = VfsPathExists;

					/* Save position */
					Ret->DirBucket = CurrentBucket;
					Ret->DirOffset = i;

					/* Cleanup */
					kfree(EntryBuffer);
					MStringDestroy(Token);

					/* Done */
					return Ret;
				}
			}

			/* Cleanup */
			MStringDestroy(NodeName);

			/* Go on to next */
			Entry++;
		}

		/* Get next bucket */
		if (!IsEnd)
		{
			/* Get next bucket */
			uint32_t Unused = 0;
			uint32_t PrevBucket = CurrentBucket;
			if (MfsGetNextBucket(Fs, CurrentBucket, &CurrentBucket, &Unused))
				IsEnd = 1;

			/* Expand Directory? */
			if (CurrentBucket == MFS_END_OF_CHAIN) 
			{
				/* Allocate another bucket */
				CurrentBucket = mData->FreeIndex;

				/* Sanity */
				if (MfsAllocateBucket(Fs, 1, &Unused)
					|| MfsSetNextBucket(Fs, PrevBucket, CurrentBucket, 1, 1)) 
				{
					/* Error */
					LogFatal("MFS1", "CREATEENTRY: Error expanding directory");
					break;
				}

				/* Zero out new bucket */
				MfsZeroBucket(Fs, CurrentBucket, 1);
			}
		}
	}

	/* Cleanup */
	kfree(EntryBuffer);
	MStringDestroy(Token);

	/* If IsEnd is set, we couldn't find it
	* If IsEnd is not set, we should not be here... */
	*ErrCode = VfsPathNotFound;
	return NULL;
}

/* Create entry in a directory bucket
 * It internally calls MfsFindFreeEntry to
 * find a viable entry and validate the path */
MfsFile_t *MfsCreateEntry(MCoreFileSystem_t *Fs, uint32_t DirBucket, MString_t *Path, int Flags, VfsErrorCode_t *ErrCode)
{
	/* Locate a free entry, and 
	 * make sure file does not exist */
	MfsFile_t *mEntry = MfsFindFreeEntry(Fs, DirBucket, Path, ErrCode);

	/* Validate */
	if (*ErrCode != VfsOk) {
		/* Either of two things happened 
		 * 1) Path was invalid 
		 * 2) File exists */
		return mEntry;
	}

	/* Ok, initialize the entry 
	 * we found a new one */
	mEntry->DataBucket = MFS_END_OF_CHAIN;
	mEntry->InitialBucketLength = 0;
	mEntry->Size = 0;
	mEntry->AllocatedSize = 0;
	mEntry->Flags = (uint16_t)Flags;

	/* Write the entry back to the filesystem */
	*ErrCode = MfsUpdateEntry(Fs, mEntry, MFS_ACTION_CREATE);

	/* Done! */
	return mEntry;
}
