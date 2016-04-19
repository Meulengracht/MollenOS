/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS MCore - MollenOS File System
* TODO:
* - Query
* - General Improvements
*/

/* Includes */
#include <Module.h>
#include "Mfs.h"

#include <MString.h>
#include <Heap.h>
#include <Log.h>

/* CLib */
#include <string.h>

/* Read Sectors Wrapper 
 * This makes sure to wrap our read requests 
 * into maximum allowed blocks */
DeviceRequestStatus_t MfsReadSectors(MCoreFileSystem_t *Fs, uint64_t Sector, void *Buffer, size_t Count)
{
	/* Sanity */
	uint64_t SectorToRead = Fs->SectorStart + Sector;
	uint8_t *BufferPointer = (uint8_t*)Buffer;
	size_t BytesToRead = (Count * Fs->SectorSize);
	size_t SectorsPerRequest = DEVICEMANAGER_MAX_IO_SIZE / Fs->SectorSize;

	/* Split into blocks */
	while (BytesToRead)
	{
		/* Keep */
		MCoreDeviceRequest_t Request;

		/* Setup request */
		Request.Type = RequestRead;
		Request.DeviceId = Fs->DiskId;
		Request.SectorLBA = SectorToRead;
		Request.Buffer = BufferPointer;

		/* Make sure we don't read to much */
		if (BytesToRead > (SectorsPerRequest * Fs->SectorSize))
			Request.Length = SectorsPerRequest * Fs->SectorSize;
		else
			Request.Length = BytesToRead;

		/* Create Request */
		DmCreateRequest(&Request);

		/* Wait */
		DmWaitRequest(&Request);

		/* Sanity */
		if (Request.Status != RequestOk)
			return Request.Status;

		/* Increase */
		if (BytesToRead > (SectorsPerRequest * Fs->SectorSize)) {
			SectorToRead += SectorsPerRequest;
			BufferPointer += SectorsPerRequest * Fs->SectorSize;
			BytesToRead -= SectorsPerRequest * Fs->SectorSize;
		}
		else
			break;
	}

	/* Done! */
	return RequestOk;
}

/* Write Sectors Wrapper
* This makes sure to wrap our write requests
* into maximum allowed blocks */
DeviceRequestStatus_t MfsWriteSectors(MCoreFileSystem_t *Fs, uint64_t Sector, void *Buffer, size_t Count)
{
	/* Sanity */
	uint64_t SectorToWrite = Fs->SectorStart + Sector;
	uint8_t *BufferPointer = (uint8_t*)Buffer;
	size_t BytesToWrite = (Count * Fs->SectorSize);
	size_t SectorsPerRequest = DEVICEMANAGER_MAX_IO_SIZE / Fs->SectorSize;

	/* Split into blocks */
	while (BytesToWrite)
	{
		/* Keep */
		MCoreDeviceRequest_t Request;

		/* Setup request */
		Request.Type = RequestWrite;
		Request.DeviceId = Fs->DiskId;
		Request.SectorLBA = SectorToWrite;
		Request.Buffer = BufferPointer;

		/* Make sure we don't read to much */
		if (BytesToWrite > (SectorsPerRequest * Fs->SectorSize))
			Request.Length = SectorsPerRequest * Fs->SectorSize;
		else
			Request.Length = BytesToWrite;

		/* Create Request */
		DmCreateRequest(&Request);

		/* Wait */
		DmWaitRequest(&Request);

		/* Sanity */
		if (Request.Status != RequestOk)
			return Request.Status;

		/* Increase */
		if (BytesToWrite > (SectorsPerRequest * Fs->SectorSize)) {
			SectorToWrite += SectorsPerRequest;
			BufferPointer += SectorsPerRequest * Fs->SectorSize;
			BytesToWrite -= SectorsPerRequest * Fs->SectorSize;
		}
		else
			break;
	}

	/* Done! */
	return RequestOk;
}

/* Update Mb - Updates the 
 * master-bucket and it's mirror 
 * by writing the updated stats in our stored data */
int MfsUpdateMb(MCoreFileSystem_t *Fs)
{
	/* Vars */
	MfsData_t *mData = (MfsData_t*)Fs->FsData;
	uint8_t *MbBuffer = (uint8_t*)kmalloc(Fs->SectorSize);
	memset((void*)MbBuffer, 0, Fs->SectorSize);
	MfsMasterBucket_t *MbPtr = (MfsMasterBucket_t*)MbBuffer;

	/* Set data */
	MbPtr->Magic = MFS_MAGIC;
	MbPtr->Flags = mData->MbFlags;
	MbPtr->RootIndex = mData->RootIndex;
	MbPtr->FreeBucket = mData->FreeIndex;
	MbPtr->BadBucketIndex = mData->BadIndex;

	/* Write MB */
	if (MfsWriteSectors(Fs, mData->MbSector, MbBuffer, 1) != RequestOk
		|| MfsWriteSectors(Fs, mData->MbMirrorSector, MbBuffer, 1) != RequestOk)
	{
		/* Error */
		LogFatal("MFS1", "UPDATEMB: Error writing to disk");
		return -1;
	}

	/* Done! */
	kfree(MbBuffer);
	return 0;
}

/* Get next bucket in chain
 * by looking up next pointer in bucket-map
 * Todo: Have this in memory */
int MfsGetNextBucket(MCoreFileSystem_t *Fs, 
	uint32_t Bucket, uint32_t *NextBucket, uint32_t *BucketLength)
{
	/* Vars */
	MfsData_t *mData = (MfsData_t*)Fs->FsData;

	/* Calculate Index */
	uint32_t SectorOffset = Bucket / (uint32_t)mData->BucketsPerSector;
	uint32_t SectorIndex = Bucket % (uint32_t)mData->BucketsPerSector;

	/* Read sector */
	if (mData->BucketBufferOffset != SectorOffset)
	{
		/* Read */
		if (MfsReadSectors(Fs, mData->BucketMapSector + SectorOffset, 
			mData->BucketBuffer, 1) != RequestOk)
		{
			/* Error */
			LogFatal("MFS1", "GETNEXTBUCKET: Error reading from disk");
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
int MfsSetNextBucket(MCoreFileSystem_t *Fs, 
	uint32_t Bucket, uint32_t NextBucket, uint32_t BucketLength, int UpdateLength)
{
	/* Vars */
	MfsData_t *mData = (MfsData_t*)Fs->FsData;

	/* Calculate Index */
	uint32_t SectorOffset = Bucket / (uint32_t)mData->BucketsPerSector;
	uint32_t SectorIndex = Bucket % (uint32_t)mData->BucketsPerSector;

	/* Read sector */
	if (mData->BucketBufferOffset != SectorOffset)
	{
		/* Read */
		if (MfsReadSectors(Fs, mData->BucketMapSector + SectorOffset, 
			mData->BucketBuffer, 1) != RequestOk)
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
		mData->BucketBuffer, 1) != RequestOk)
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
int MfsAllocateBucket(MCoreFileSystem_t *Fs, uint32_t NumBuckets, uint32_t *InitialBucketSize)
{
	/* Vars */
	MfsData_t *mData = (MfsData_t*)Fs->FsData;

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
			mData->FreeIndex = BucketPtr;

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
int MfsFreeBuckets(MCoreFileSystem_t *Fs, uint32_t StartBucket, uint32_t StartLength)
{
	/* Vars */
	uint32_t bIterator = StartBucket, bLength = 0, pIterator = 0;
	MfsData_t *mData = (MfsData_t*)Fs->FsData;

	/* Sanity */
	if (StartBucket == MFS_END_OF_CHAIN
		|| StartLength == 0)
		return -1;

	/* Essentially there is two algorithms we can deploy here
	 * The quick one - Which is just to add the allocated bucket list
	 * to the free and set the last allocated to point to the first free
	 * OR there is the slow one that makes sure that buckets are <in order> as
	 * they get freed, and gets inserted or extended correctly. This will reduce
	 * fragmentation by A LOT */

	/* So I'm already limited by time due to life, so i'll with the quick */

	/* Step 1, iterate to the last bucket */
	while (bIterator != MFS_END_OF_CHAIN) {
		pIterator = bIterator;
		if (MfsGetNextBucket(Fs, bIterator, &bIterator, &bLength)) {
			return -1;
		}
	}

	/* Ok, so now update the pointer to free list */
	if (MfsSetNextBucket(Fs, pIterator, mData->FreeIndex, bLength, 0)) {
		return -1;
	}

	/* Update initial free bucket */
	mData->FreeIndex = StartBucket;

	/* Done! */
	return MfsUpdateMb(Fs);
}

/* Zero Bucket - Tool for 
 * wiping a bucket, needed for directory
 * expansion and allocation, as we need entries
 * to be initially null */
int MfsZeroBucket(MCoreFileSystem_t *Fs, uint32_t Bucket, uint32_t NumBuckets)
{
	/* Vars */
	MfsData_t *mData = (MfsData_t*)Fs->FsData;
	uint64_t Sector = mData->BucketSize * Bucket;
	uint32_t Count = mData->BucketSize * NumBuckets;

	/* Allocate a null buffer */
	void *NullBuffer = (void*)kmalloc(sizeof(Count * mData->BucketSize * Fs->SectorSize));
	memset(NullBuffer, 0, sizeof(Count * mData->BucketSize * Fs->SectorSize));

	/* Write it */
	if (MfsReadSectors(Fs, Sector, NullBuffer, Count) != RequestOk) {
		/* Error */
		LogFatal("MFS1", "ZEROBUCKET: Error writing to disk");
		kfree(NullBuffer);
		return -1;
	}

	/* Cleanup */
	kfree(NullBuffer);

	/* Done! */
	return 0;
}

/* Updates a mfs entry in a directory 
 * This is used when we modify size, name
 * access times and flags for convenience */
VfsErrorCode_t MfsUpdateEntry(MCoreFileSystem_t *Fs, MfsFile_t *Handle, int Action)
{
	/* Cast */
	MfsData_t *mData = (MfsData_t*)Fs->FsData;
	VfsErrorCode_t RetCode = VfsOk;
	uint32_t i;

	/* Allocate buffer for data */
	uint8_t *EntryBuffer = (uint8_t*)kmalloc(mData->BucketSize * Fs->SectorSize);

	/* Read in the bucket of where the entry lies */
	if (MfsReadSectors(Fs, mData->BucketSize * Handle->DirBucket,
		EntryBuffer, mData->BucketSize) != RequestOk)
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
			memcpy(&Iterator->Name[0], Handle->Name->Data, Handle->Name->Length);

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
		EntryBuffer, mData->BucketSize) != RequestOk)
		RetCode = VfsDiskError;

	/* Done! */
Done:
	kfree(EntryBuffer);
	return RetCode;
}

/* This is our primary searcher function
 * Given a path it validates and locates a 
 * mfs-entry in a directory-path */
MfsFile_t *MfsLocateEntry(MCoreFileSystem_t *Fs, uint32_t DirBucket, MString_t *Path)
{
	/* Vars */
	MfsData_t *mData = (MfsData_t*)Fs->FsData;
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
			EntryBuffer, mData->BucketSize) != RequestOk)
		{
			/* Error */
			LogFatal("MFS1", "LOCATEENTRY: Error reading from disk");
			break;
		}

		/* Iterate buffer */
		MfsTableEntry_t *Entry = (MfsTableEntry_t*)EntryBuffer;
		for (i = 0; i < (mData->BucketSize / 2); i++)
		{
			/* Sanity, end of table */
			if (Entry->Status == MFS_STATUS_END)
			{
				IsEnd = 1;
				break;
			}

			/* Sanity, deleted entry? */
			if (Entry->Status == MFS_STATUS_DELETED) 
			{
				/* Go on to next */
				Entry++;
				continue;
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
						MfsFile_t *RetData = (MfsFile_t*)kmalloc(sizeof(MfsFile_t));
						RetData->Status = VfsPathIsNotDirectory;
						return RetData;
					}

					/* Sanity the bucket beforehand */
					if (Entry->StartBucket == MFS_END_OF_CHAIN)
					{
						/* Cleanup */
						kfree(EntryBuffer);
						MStringDestroy(NodeName);
						MStringDestroy(Token);

						/* Path not found ofc */
						MfsFile_t *RetData = (MfsFile_t*)kmalloc(sizeof(MfsFile_t));
						RetData->Status = VfsPathNotFound;
						return RetData;
					}

					/* Create a new sub-string with rest */
					MString_t *RestOfPath = 
						MStringSubString(Path, StrIndex + 1, 
						(MStringLength(Path) - (StrIndex + 1)));

					/* Go deeper */
					MfsFile_t *Ret = MfsLocateEntry(Fs, Entry->StartBucket, RestOfPath);

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
					Ret->DataBucketPosition = Entry->StartBucket;
					Ret->InitialBucketLength = Entry->StartLength;
					Ret->DataBucketLength = Entry->StartLength;
					Ret->Status = VfsOk;

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
			uint32_t Unused = 0;
			if (MfsGetNextBucket(Fs, CurrentBucket, &CurrentBucket, &Unused))
				IsEnd = 1;

			if (CurrentBucket == MFS_END_OF_CHAIN)
				IsEnd = 1;
		}
	}

	/* Cleanup */
	kfree(EntryBuffer);
	MStringDestroy(Token);
	
	/* If IsEnd is set, we couldn't find it 
	 * If IsEnd is not set, we should not be here... */
	MfsFile_t *RetData = (MfsFile_t*)kmalloc(sizeof(MfsFile_t));
	RetData->Status = VfsPathNotFound;
	return RetData;
}

/* Very alike to the MfsLocateEntry
 * except instead of locating a file entry
 * it locates a free entry in the last token of
 * the path, and validates the path as it goes */
MfsFile_t *MfsFindFreeEntry(MCoreFileSystem_t *Fs, uint32_t DirBucket, MString_t *Path)
{
	/* Vars */
	MfsData_t *mData = (MfsData_t*)Fs->FsData;
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
			EntryBuffer, mData->BucketSize) != RequestOk)
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
					Ret->Status = VfsOk;

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
						MfsFile_t *RetData = (MfsFile_t*)kmalloc(sizeof(MfsFile_t));
						RetData->Status = VfsPathIsNotDirectory;
						return RetData;
					}

					/* Sanity the bucket beforehand */
					if (Entry->StartBucket == MFS_END_OF_CHAIN)
					{
						/* Vars */
						uint32_t Unused = 0;

						/* Allocate bucket for directory */
						Entry->StartBucket = mData->FreeIndex;
						Entry->StartLength = 1;
						MfsAllocateBucket(Fs, 1, &Unused);

						/* Write back buffer */
						if (MfsWriteSectors(Fs, mData->BucketSize * CurrentBucket,
							EntryBuffer, mData->BucketSize) != RequestOk)
						{
							/* Error */
							LogFatal("MFS1", "CREATEENTRY: Error writing to disk");
							
							/* Cleanup */
							kfree(EntryBuffer);
							MStringDestroy(NodeName);
							MStringDestroy(Token);

							/* Path not found ofc */
							MfsFile_t *RetData = (MfsFile_t*)kmalloc(sizeof(MfsFile_t));
							RetData->Status = VfsDiskError;
							return RetData;
						}

						/* Zero out new bucket */
						MfsZeroBucket(Fs, Entry->StartBucket, 1);
					}

					/* Create a new sub-string with rest */
					MString_t *RestOfPath =
						MStringSubString(Path, StrIndex + 1,
						(MStringLength(Path) - (StrIndex + 1)));

					/* Go deeper */
					MfsFile_t *Ret = MfsLocateEntry(Fs, Entry->StartBucket, RestOfPath);

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
					Ret->DataBucketPosition = Entry->StartBucket;
					Ret->InitialBucketLength = Entry->StartLength;
					Ret->DataBucketLength = Entry->StartLength;
					Ret->Status = VfsPathExists;

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
			if (CurrentBucket == MFS_END_OF_CHAIN) {
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
	MfsFile_t *RetData = (MfsFile_t*)kmalloc(sizeof(MfsFile_t));
	RetData->Status = VfsPathNotFound;
	return RetData;
}

/* Create entry in a directory bucket
 * It internally calls MfsFindFreeEntry to
 * find a viable entry and validate the path */
MfsFile_t *MfsCreateEntry(MCoreFileSystem_t *Fs, uint32_t DirBucket, MString_t *Path, int Flags) 
{
	/* Locate a free entry, and 
	 * make sure file does not exist */
	MfsFile_t *mEntry = MfsFindFreeEntry(Fs, DirBucket, Path);

	/* Validate */
	if (mEntry->Status != VfsOk) {
		/* Either of two things happened 
		 * 1) Path was invalid 
		 * 2) File exists */
		return mEntry;
	}

	/* Ok, initialize the entry 
	 * we found a new one */
	mEntry->DataBucket = MFS_END_OF_CHAIN;
	mEntry->DataBucketPosition = MFS_END_OF_CHAIN;
	mEntry->DataBucketLength = 0;
	mEntry->InitialBucketLength = 0;
	mEntry->Size = 0;
	mEntry->AllocatedSize = 0;
	mEntry->Flags = (uint16_t)Flags;

	/* Write the entry back to the filesystem */
	mEntry->Status = MfsUpdateEntry(Fs, mEntry, MFS_ACTION_CREATE);

	/* Done! */
	return mEntry;
}

/* Open File - This function
 * handles both the opening of files
 * and creation of files depending on the
 * given flags */
VfsErrorCode_t MfsOpenFile(void *FsData, 
	MCoreFile_t *Handle, MString_t *Path, VfsFileFlags_t Flags)
{
	/* Cast */
	MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)FsData;
	MfsData_t *mData = (MfsData_t*)Fs->FsData;
	VfsErrorCode_t RetCode = VfsOk;

	/* This will be a recursive parse of path */
	MfsFile_t *FileInfo = MfsLocateEntry(Fs, mData->RootIndex, Path);

	/* Validation Phase
	 * So how should we handle this? */
	if (FileInfo->Status == VfsPathNotFound
		&& Flags & CreateIfNotExists) 
	{
		/* Cleanup */
		kfree(FileInfo);

		/* Try to create the file */
		FileInfo = MfsCreateEntry(Fs, mData->RootIndex, Path, MFS_FILE);

		/* Invalid path or path not found 
		 * is enough cause for early break */
		if (FileInfo->Status != VfsOk
			&& FileInfo->Status != VfsPathExists) 
		{
			/* Store return code, cleanup */
			RetCode = FileInfo->Status;
			kfree(FileInfo);
			return RetCode;
		}
	}
	else if (FileInfo->Status != VfsOk)
	{
		/* This means we have tried to open 
		 * a file normally - and it failed */

		/* Cleanup */
		RetCode = FileInfo->Status;
		kfree(FileInfo);
		return RetCode;
	}

	/* File exists */
	if (FileInfo->Status == VfsPathExists
		&& (Flags & FailIfExists)) 
	{
		/* File already exists (and we didn't just create it)
		 * and for some reason that is not ok */
		RetCode = FileInfo->Status = VfsPathExists;
		kfree(FileInfo);
		return RetCode;
	}

	/* Post functions */
	if (FileInfo->Status == VfsPathExists
		&& (Flags & TruncateIfExists))
	{
		/* Free */
		int fRes = MfsFreeBuckets(Fs, FileInfo->DataBucket, FileInfo->InitialBucketLength);

		/* Only update entry if needs to be updated */
		if (!fRes) 
		{
			/* Update Stats */
			FileInfo->DataBucket = MFS_END_OF_CHAIN;
			FileInfo->DataBucketLength = FileInfo->InitialBucketLength = 0;
			FileInfo->Size = 0;
			
			/* Update Entry */
			FileInfo->Status = MfsUpdateEntry(Fs, FileInfo, MFS_ACTION_UPDATE);
		}
	}

	/* Fill out Handle */
	Handle->Data = FileInfo;
	Handle->Flags = Flags;

	Handle->Position = 0;
	Handle->Size = FileInfo->Size;
	Handle->Name = FileInfo->Name;
	
	/* Update EOF */
	if (Handle->Size == 0)
		Handle->IsEOF = 1;
	else
		Handle->IsEOF = 0;

	/* Done */
	return RetCode;
}

/* Close File 
 * frees resources allocated by Open File
 * and cleans up */
VfsErrorCode_t MfsCloseFile(void *FsData, MCoreFile_t *Handle)
{
	/* Not used */
	_CRT_UNUSED(FsData);

	/* Cast */
	MfsFile_t *FileInfo = (MfsFile_t*)Handle->Data;
	VfsErrorCode_t RetCode = VfsOk;

	/* Sanity */
	if (Handle->Data == NULL)
		return RetCode;

	/* Cleanup */
	kfree(FileInfo);

	/* Done */
	return RetCode;
}

/* Read File - Reads from a 
 * given MCoreFile entry, the position
 * is stored in our structures and thus not neccessary 
 * File must be opened with read permissions */
size_t MfsReadFile(void *FsData, MCoreFile_t *Handle, uint8_t *Buffer, size_t Size)
{
	/* Vars */
	MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)FsData;
	MfsData_t *mData = (MfsData_t*)Fs->FsData;
	MfsFile_t *mFile = (MfsFile_t*)Handle->Data;
	uint8_t *BufPtr = Buffer;
	VfsErrorCode_t RetCode = VfsOk;

	/* Sanity */
	if (Handle->IsEOF
		|| Handle->Position == Handle->Size
		|| Size == 0)
		return 0;

	/* Security Sanity */
	if (!(Handle->Flags & Read))
	{
		Handle->Code = VfsAccessDenied;
		return 0;
	}

	/* BucketPtr for iterating */
	size_t BytesToRead = Size;
	size_t BytesRead = 0;

	/* Sanity */
	if ((Handle->Position + Size) > Handle->Size)
		BytesToRead = (size_t)(Handle->Size - Handle->Position);

	/* Keep reeeading */
	while (BytesToRead)
	{
		/* Calculate buffer size */
		size_t BucketSize = (mFile->DataBucketLength * (mData->BucketSize * Fs->SectorSize));

		/* Allocate buffer for data */
		uint8_t *TempBuffer = (uint8_t*)kmalloc(BucketSize);

		/* Read the bucket */
		if (MfsReadSectors(Fs, (mData->BucketSize * mFile->DataBucketPosition), 
			TempBuffer, (mFile->DataBucketLength * mData->BucketSize)) != RequestOk)
		{
			/* Error */
			RetCode = VfsDiskError;
			LogFatal("MFS1", "READFILE: Error reading from disk");
			break;
		}

		/* We have to calculate the offset into this buffer we must transfer data */
		size_t bOffset = (size_t)(Handle->Position % BucketSize);
		size_t BytesLeft = BucketSize - bOffset;
		size_t BytesCopied = 0;

		/* We have a few cases
		 * Case 1: We have enough data here 
		 * Case 2: We have to read more than is here */
		if (BytesToRead > BytesLeft)
		{
			/* Start out by copying remainder */
			memcpy(BufPtr, (TempBuffer + bOffset), BytesLeft);
			BytesCopied = BytesLeft;
		}
		else
		{
			/* Just copy */
			memcpy(BufPtr, (TempBuffer + bOffset), BytesToRead);
			BytesCopied = BytesToRead;
		}

		/* Done with buffer */
		kfree(TempBuffer);

		/* Advance pointer(s) */
		BytesRead += BytesCopied;
		BufPtr += BytesCopied;
		BytesToRead -= BytesCopied;
		Handle->Position += BytesCopied;

		/* Switch to next bucket? */
		if (BytesLeft == BytesCopied)
		{
			/* Vars */
			uint32_t NextBucket = 0, BucketLength = 0;

			/* Go to next */
			if (MfsGetNextBucket(Fs, mFile->DataBucketPosition, &NextBucket, &BucketLength))
				break;

			/* Sanity */
			if (NextBucket != MFS_END_OF_CHAIN) {
				mFile->DataBucketPosition = NextBucket;
				mFile->DataBucketLength = BucketLength;
			}
			else
				Handle->IsEOF = 1;
		}

		/* Sanity */
		if (Handle->IsEOF)
			break;
	}

	/* Sanity */
	if (Handle->Position == Handle->Size)
		Handle->IsEOF = 1;

	/* Done! */
	Handle->Code = RetCode;
	return BytesRead;
}

/* Write File - Writes to a 
 * given MCoreFile entry, the position
 * is stored in our structures and thus not neccessary 
 * File must be opened with write permissions */
size_t MfsWriteFile(void *FsData, MCoreFile_t *Handle, uint8_t *Buffer, size_t Size)
{
	/* Vars */
	MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)FsData;
	MfsData_t *mData = (MfsData_t*)Fs->FsData;
	MfsFile_t *mFile = (MfsFile_t*)Handle->Data;
	uint8_t *BufPtr = Buffer;
	VfsErrorCode_t RetCode = VfsOk;

	/* Security Sanity */
	if (!(Handle->Flags & Write))
	{
		Handle->Code = VfsAccessDenied;
		return 0;
	}

	/* BucketPtr for iterating */
	size_t BytesWritten = 0;
	size_t BytesToWrite = Size;

	/* Make sure there is enough room */
	if ((Handle->Position + Size) > mFile->AllocatedSize)
	{
		/* Well... Uhh */
		
		/* Allocate more */
		uint64_t NumSectors = ((Handle->Position + Size) - mFile->AllocatedSize) / Fs->SectorSize;
		if ((((Handle->Position + Size) - mFile->AllocatedSize) % Fs->SectorSize) > 0)
			NumSectors++;

		uint64_t NumBuckets = NumSectors / mData->BucketSize;
		if ((NumSectors % mData->BucketSize) > 0)
			NumBuckets++;

		/* Allocate buckets */
		uint32_t FreeBucket = mData->FreeIndex;
		uint32_t FreeLength = 0;
		MfsAllocateBucket(Fs, (uint32_t)NumBuckets, &FreeLength);

		/* Get last bucket in chain */
		uint32_t BucketPtr = mFile->DataBucket;
		uint32_t BucketPrevPtr = 0;
		uint32_t BucketLength = 0;
		while (BucketPtr != MFS_END_OF_CHAIN)
		{
			BucketPrevPtr = BucketPtr;
			MfsGetNextBucket(Fs, BucketPtr, &BucketPtr, &BucketLength);
		}

		/* Update pointer */
		MfsSetNextBucket(Fs, BucketPrevPtr, FreeBucket, FreeLength, 1);

		/* Adjust allocated size */
		mFile->AllocatedSize += (NumBuckets * mData->BucketSize * Fs->SectorSize);
	}

	/* Allocate buffer for data */
	uint8_t *TempBuffer = (uint8_t*)kmalloc(mData->BucketSize * Fs->SectorSize);

	/* Keep reeeading */
	while (BytesToWrite)
	{
		/* We have to calculate the offset into this buffer we must transfer data */
		uint32_t bOffset = (uint32_t)(Handle->Position % (mData->BucketSize * Fs->SectorSize));
		uint32_t BytesLeft = (mData->BucketSize * Fs->SectorSize) - bOffset;
		uint32_t BytesCopied = 0;

		/* Are we on a bucket boundary ?
		 * and we need to write atleast an entire bucket */
		if (bOffset == 0
			&& BytesToWrite >= (mData->BucketSize * Fs->SectorSize))
		{
			/* Then we don't care about content */
			memcpy(TempBuffer, BufPtr, (mData->BucketSize * Fs->SectorSize));
			BytesCopied = (mData->BucketSize * Fs->SectorSize);
		}
		else
		{
			/* Means we are modifying */

			/* Read the old bucket */
			if (MfsReadSectors(Fs, mData->BucketSize * mFile->DataBucketPosition,
				TempBuffer, mData->BucketSize) != RequestOk)
			{
				/* Error */
				RetCode = VfsDiskError;
				LogFatal("MFS1", "WRITEFILE: Error reading from disk");
				break;
			}
			
			/* Buuuut, we have quite a few cases here 
			 * Case 1 - We need to write less than what is left, easy */
			if (BytesToWrite <= BytesLeft)
			{
				/* Write it */
				memcpy((TempBuffer + bOffset), BufPtr, BytesToWrite);
				BytesCopied = BytesToWrite;
			}
			else
			{
				/* Write whats left */
				memcpy((TempBuffer + bOffset), BufPtr, BytesLeft);
				BytesCopied = BytesLeft;
			}
		}

		/* Write back bucket */
		if (MfsWriteSectors(Fs, mData->BucketSize * mFile->DataBucketPosition,
			TempBuffer, mData->BucketSize) != RequestOk)
		{
			/* Error */
			RetCode = VfsDiskError;
			LogFatal("MFS1", "WRITEFILE: Error writing to disk");
			break;
		}

		/* Switch to next bucket? */
		if (BytesLeft >= BytesCopied)
		{
			/* Go to next */
			uint32_t NextBucket = 0;
			uint32_t BucketLength = 0;
			MfsGetNextBucket(Fs, mFile->DataBucketPosition, &NextBucket, &BucketLength);

			/* Sanity */
			if (NextBucket != MFS_END_OF_CHAIN)
				mFile->DataBucketPosition = NextBucket;
		}

		/* Advance pointer(s) */
		BytesWritten += BytesCopied;
		BufPtr += BytesCopied;
		BytesToWrite -= BytesCopied;
		Handle->Position += BytesCopied;
	}

	/* Cleanup */
	kfree(TempBuffer);

	/* Sanity */
	if (Handle->Position > Handle->Size)
	{
		Handle->Size = Handle->Position;
		mFile->Size = Handle->Position;
	}

	/* Update entry */
	MfsUpdateEntry(Fs, mFile, MFS_ACTION_UPDATE);

	/* Done! */
	Handle->Code = RetCode;
	return BytesWritten;
}

/* Delete File - Frees all 
 * buckets associated with the file entry
 * and <nulls> the entry in the directory it
 * resides (marks it deleted) */
VfsErrorCode_t MfsDeleteFile(void *FsData, MCoreFile_t *Handle)
{
	/* Cast */
	MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)FsData;
	MfsFile_t *FileInfo = (MfsFile_t*)Handle->Data;
	VfsErrorCode_t ErrCode = VfsOk;

	/* Step 1 - Free buckets */
	MfsFreeBuckets(Fs, FileInfo->DataBucket, FileInfo->InitialBucketLength);

	/* Step 2 - Mark entry deleted */
	ErrCode = MfsUpdateEntry(Fs, FileInfo, MFS_ACTION_DELETE);

	/* Done! */
	return ErrCode;
}

/* Seek - Sets the current position
 * in a file, we must iterate through the
 * bucket chain to reposition our bucket 
 * iterator correctly as well */
VfsErrorCode_t MfsSeek(void *FsData, MCoreFile_t *Handle, uint64_t Position)
{
	/* Vars */
	MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)FsData;
	MfsData_t *mData = (MfsData_t*)Fs->FsData;
	MfsFile_t *mFile = (MfsFile_t*)Handle->Data;
	int ConstantLoop = 1;

	/* Sanity */
	if (Handle->Position > Handle->Size)
		return VfsInvalidParameters;
	if (Handle->Size == 0)
		return VfsOk;

	/* Step 1, if the new position is in
	 * initial bucket, we need to do no actual
	 * seeking */
	size_t InitialBucketMax = (mFile->InitialBucketLength * (mData->BucketSize * Fs->SectorSize));

	/* Lets see */
	if (Position < InitialBucketMax) {
		mFile->DataBucketPosition = mFile->DataBucket;
		mFile->DataBucketLength = mFile->InitialBucketLength;
	}
	else
	{
		/* Step 2. We might still get out easy
		 * if we are setting a new position that's 
		 * within the current bucket */

		/* Do we cross a boundary? */
		uint64_t OldBucketOffset = Handle->Position / (mData->BucketSize * Fs->SectorSize);
		uint64_t NewBucketOffset = Position / (mData->BucketSize * Fs->SectorSize);

		/* Lets see */
		if (NewBucketOffset != OldBucketOffset)
		{
			/* Keep Track */
			uint64_t PositionBoundLow = 0;
			uint64_t PositionBoundHigh = InitialBucketMax;

			/* Spool to correct bucket */
			uint32_t BucketPtr = mFile->DataBucket;
			uint32_t BucketLength = mFile->InitialBucketLength;
			while (ConstantLoop)
			{
				/* Sanity */
				if (Position >= PositionBoundLow
					&& Position < (PositionBoundLow + PositionBoundHigh))
					break;

				/* Get next */
				if (MfsGetNextBucket(Fs, BucketPtr, &BucketPtr, &BucketLength))
					break;

				/* This should NOT happen */
				if (BucketPtr == MFS_END_OF_CHAIN)
					break;

				/* Calc new bounds */
				PositionBoundLow += PositionBoundHigh;
				PositionBoundHigh = (BucketLength * (mData->BucketSize * Fs->SectorSize));
			}

			/* Update bucket ptr */
			mFile->DataBucketPosition = BucketPtr;
		}
	}
	
	/* Update pointer */
	Handle->Position = Position;
	
	/* Done */
	return VfsOk;
}

/* Query information - This function
 * is used to query a file for information 
 * or a directory for it's entries etc */
VfsErrorCode_t MfsQuery(void *FsData, MCoreFile_t *Handle, 
	VfsQueryFunction_t Function, void *Buffer, size_t Length)
{
	/* Vars & Casts */
	MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)FsData;
	MfsFile_t *mFile = (MfsFile_t*)Handle->Data;

	/* Unused */
	_CRT_UNUSED(Fs);

	/* Which function are we requesting? */
	switch (Function) {

		/* Get stats and information about an handle */
		case QueryStats: 
		{
			/* Cast Handle */
			VQFileStats_t *Stats = (VQFileStats_t*)Buffer;

			/* Sanity length */
			if (Length < sizeof(VQFileStats_t))
				return VfsInvalidParameters;

			/* Copy Stats */
			Stats->Size = mFile->Size;
			Stats->SizeOnDisk = mFile->AllocatedSize;
			Stats->Position = Handle->Position;
			Stats->Access = (int)Handle->Flags;

			/* Should prolly convert this to a generic vfs format .. */
			Stats->Flags = (int)mFile->Flags;

		} break;

		/* Get children of a node -> Must be a directory */
		case QueryChildren: {

		} break;

		/* Ehhh */
		default:
			break;
	}

	/* Done! */
	return VfsOk;
}

/* Unload MFS Driver 
 * If it's forced, we can't save
 * stuff back to the disk :/ */
OsResult_t MfsDestroy(void *FsData, uint32_t Forced)
{
	/* Cast */
	MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)FsData;
	MfsData_t *mData = (MfsData_t*)Fs->FsData;

	/* Sanity */
	if (!Forced)
	{
		/* Flush operation buffer and buffers - TODO */
	}

	/* Free resources */
	kfree(mData->BucketBuffer);
	kfree(mData->VolumeLabel);
	kfree(mData);

	/* Done */
	return OsOk;
}

/* Entry point of a module */
MODULES_API void ModuleInit(void *Data)
{
	/* Allocate structures */
	MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)Data;
	void *TmpBuffer = (void*)kmalloc(Fs->SectorSize);
	MfsBootRecord_t *BootRecord = NULL;

	/* Read bootsector */
	if (MfsReadSectors(Fs, 0, TmpBuffer, 1) != RequestOk)
	{
		/* Error */
		Fs->State = VfsStateFailed;
		LogFatal("MFS1", "INIT: Error reading from disk");
		kfree(TmpBuffer);
		return;
	}

	/* Cast */
	BootRecord = (MfsBootRecord_t*)TmpBuffer;
	
	/* Validate Magic */
	if (BootRecord->Magic != MFS_MAGIC)
	{
		Fs->State = VfsStateFailed;
		LogFatal("MFS1", "INIT: Invalid Magic 0x%x", BootRecord->Magic);
		kfree(TmpBuffer);
		return;
	}

	/* Validate Version */
	if (BootRecord->Version != 0x1)
	{
		Fs->State = VfsStateFailed;
		LogFatal("MFS1", "INIT: Invalid Version");
		kfree(TmpBuffer);
		return;
	}

	/* Allocate */
	MfsData_t *mData = (MfsData_t*)kmalloc(sizeof(MfsData_t));

	/* Save some of the data */
	mData->MbSector = BootRecord->MasterBucketSector;
	mData->MbMirrorSector = BootRecord->MasterBucketMirror;
	mData->Version = (uint32_t)BootRecord->Version;
	mData->BucketSize = (uint32_t)BootRecord->SectorsPerBucket;
	mData->Flags = (uint32_t)BootRecord->Flags;

	/* Boot Drive? */
	if (BootRecord->Flags & MFS_OSDRIVE)
		Fs->Flags |= VFS_MAIN_DRIVE;

	/* Calculate the bucket-map sector */
	mData->BucketCount = Fs->SectorCount / mData->BucketSize;
	mData->BucketsPerSector = Fs->SectorSize / 8;

	/* Copy the volume label over */
	mData->VolumeLabel = (char*)kmalloc(8 + 1);
	memset(mData->VolumeLabel, 0, 9);
	memcpy(mData->VolumeLabel, BootRecord->BootLabel, 8);

	/* Read the MB */
	if (MfsReadSectors(Fs, mData->MbSector, TmpBuffer, 1) != RequestOk)
	{
		/* Error */
		Fs->State = VfsStateFailed;
		LogFatal("MFS1", "INIT: Error reading MB from disk");
		kfree(TmpBuffer);
		kfree(mData->VolumeLabel);
		kfree(mData);
		return;
	}

	/* Validate MB */
	MfsMasterBucket_t *Mb = (MfsMasterBucket_t*)TmpBuffer;

	/* Sanity */
	if (Mb->Magic != MFS_MAGIC)
	{
		Fs->State = VfsStateFailed;
		LogFatal("MFS1", "INIT: Invalid MB-Magic 0x%x", Mb->Magic);
		kfree(TmpBuffer);
		kfree(mData->VolumeLabel);
		kfree(mData);
		return;
	}

	/* Parse */
	mData->RootIndex = Mb->RootIndex;
	mData->FreeIndex = Mb->FreeBucket;
	mData->BadIndex = Mb->BadBucketIndex;
	mData->MbFlags = Mb->Flags;
	mData->BucketMapSector = Mb->BucketMapSector;
	mData->BucketMapSize = Mb->BucketMapSize;

	/* Setup buffer */
	mData->BucketBuffer = kmalloc(Fs->SectorSize);
	mData->BucketBufferOffset = 0xFFFFFFFF;

	/* Setup Fs */
	Fs->State = VfsStateActive;
	Fs->FsData = mData;

	/* Setup functions */
	Fs->Destory = MfsDestroy;
	Fs->OpenFile = MfsOpenFile;
	Fs->CloseFile = MfsCloseFile;
	Fs->ReadFile = MfsReadFile;
	Fs->WriteFile = MfsWriteFile;
	Fs->DeleteFile = MfsDeleteFile;
	Fs->Query = MfsQuery;
	Fs->Seek = MfsSeek;

	/* Done, cleanup */
	kfree(TmpBuffer);
	return;
}