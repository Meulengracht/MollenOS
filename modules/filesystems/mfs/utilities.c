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

/* Get next bucket in chain
 * by looking up next pointer in bucket-map
 * Todo: Have this in memory */
int MfsGetNextBucket(
	_In_ FileSystemDescriptor_t *Descriptor,
	uint32_t Bucket, uint32_t *NextBucket, uint32_t *BucketLength)
{
	/* Vars */
	MfsData_t *mData = (MfsData_t*)Fs->ExtendedData;

	/* Calculate Index */
	uint32_t SectorOffset = Bucket / (uint32_t)mData->BucketsPerSector;
	uint32_t SectorIndex = Bucket % (uint32_t)mData->BucketsPerSector;

	/* Read sector */
	if (mData->BucketBufferOffset != SectorOffset)
	{
		/* Read */
		if (MfsReadSectors(Fs, mData->BucketMapSector + SectorOffset, 
			mData->BucketBuffer, 1) != RequestNoError)
		{
			/* Error */
			LogFatal("MFS1", "GETNEXTBUCKET: Error reading from disk (Bucket %u, Sector %u)",
				Bucket, (size_t)(mData->BucketMapSector + SectorOffset));
			return -1;
		}

		/* Update */
		mData->BucketBufferOffset = SectorOffset;
	}
	
	/* Pointer to array */
	uint8_t *BufPtr = (uint8_t*)mData->BucketBuffer;

	/* Done */
	*NextBucket = *(uint32_t*)&BufPtr[SectorIndex * 8];
	*BucketLength = *(uint32_t*)&BufPtr[SectorIndex * 8 + 4]; 
	return 0;
}

/* Set next bucket in chain 
 * by looking up next pointer in bucket-map
 * Todo: have this in memory */
int MfsSetNextBucket(
	_In_ FileSystemDescriptor_t *Descriptor,
	uint32_t Bucket, uint32_t NextBucket, uint32_t BucketLength, int UpdateLength)
{
	/* Vars */
	MfsData_t *mData = (MfsData_t*)Fs->ExtendedData;

	/* Calculate Index */
	uint32_t SectorOffset = Bucket / (uint32_t)mData->BucketsPerSector;
	uint32_t SectorIndex = Bucket % (uint32_t)mData->BucketsPerSector;

	/* Read sector */
	if (mData->BucketBufferOffset != SectorOffset)
	{
		/* Read */
		if (MfsReadSectors(Fs, mData->BucketMapSector + SectorOffset, 
			mData->BucketBuffer, 1) != RequestNoError)
		{
			/* Error */
			LogFatal("MFS1", "SETNEXTBUCKET: Error reading from disk");
			return -1;
		}

		/* Update */
		mData->BucketBufferOffset = SectorOffset;
	}

	/* Pointer to array */
	uint8_t *BufPtr = (uint8_t*)mData->BucketBuffer;

	/* Edit */
	*(uint32_t*)&BufPtr[SectorIndex * 8] = NextBucket;

	/* Only update if neccessary */
	if (UpdateLength)
		*(uint32_t*)&BufPtr[SectorIndex * 8 + 4] = BucketLength;

	/* Write it back */
	if (MfsWriteSectors(Fs, mData->BucketMapSector + SectorOffset, 
		mData->BucketBuffer, 1) != RequestNoError)
	{
		/* Error */
		LogFatal("MFS1", "SETNEXTBUCKET: Error writing to disk");
		return -1;
	}

	/* Done! */
	return 0;
}

/* Allocates a number of buckets from the 
 * bucket map, and returns the size of the first
 * bucket-allocation */
int MfsAllocateBucket(
	_In_ FileSystemDescriptor_t *Descriptor, uint32_t NumBuckets, uint32_t *InitialBucketSize)
{
	/* Vars */
	MfsData_t *mData = (MfsData_t*)Fs->ExtendedData;

	/* We'll keep track */
	uint32_t Counter = NumBuckets;
	uint32_t BucketPtr = mData->FreeIndex;
	uint32_t BucketPrevPtr = 0;
	uint32_t FirstBucketSize = 0;

	/* Iterate untill we are done */
	while (Counter > 0)
	{
		/* Size storage */
		uint32_t BucketLength = 0;

		/* Deep Call */
		BucketPrevPtr = BucketPtr;
		if (MfsGetNextBucket(Fs, BucketPtr, &BucketPtr, &BucketLength))
			return -1;

		/* Sanity */
		if (BucketLength > Counter)
		{
			/* Calculate next free */
			uint32_t NextFreeBucket = BucketPrevPtr + Counter;
			uint32_t NextFreeCount = BucketLength - Counter;

			if (FirstBucketSize == 0)
				FirstBucketSize = BucketLength;

			/* We have to adjust now,
			* since we are taking only a chunk
			* of the available length */
			if (MfsSetNextBucket(Fs, BucketPrevPtr, MFS_END_OF_CHAIN, Counter, 1)
				|| MfsSetNextBucket(Fs, NextFreeBucket, BucketPtr, NextFreeCount, 1))
				return -1;

			/* Update */
			*InitialBucketSize = FirstBucketSize;
			mData->FreeIndex = NextFreeBucket;

			/* Done */
			return MfsUpdateMb(Fs);
		}
		else
		{
			/* We can just take the whole cake
			* no need to modify it's length */
			if (FirstBucketSize == 0)
				FirstBucketSize = BucketLength;

			/* Next */
			Counter -= BucketLength;
		}
	}

	/* Update BucketPrevPtr to 0xFFFFFFFF */
	MfsSetNextBucket(Fs, BucketPrevPtr, MFS_END_OF_CHAIN, 0, 0);
	mData->FreeIndex = BucketPtr;

	/* Update MB */
	return MfsUpdateMb(Fs);
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
	uint32_t PreviousBucketIterator;
	uint32_t BucketIterator;
	uint32_t BucketLength;

	// Trace
	TRACE("MfsFreeBuckets(Bucket %u, Length %u)",
		StartBucket, StartLength);

	// Instantiate the variables
	Mfs = (MfsInstance_t*)Descriptor->ExtendedData;
	BucketIterator = StartBucket;

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
	while (BucketIterator != MFS_ENDOFCHAIN) {
		PreviousBucketIterator = BucketIterator;
		if (MfsGetNextBucket(Descriptor, BucketIterator, &BucketIterator, &BucketLength)) {
			return -1;
		}
	}

	// Ok, so now update the pointer to free list
	if (MfsSetNextBucket(Fs, PreviousBucketIterator, mData->FreeIndex, BucketLength, 0)) {
		return -1;
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
