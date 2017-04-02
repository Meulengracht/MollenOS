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
#include "mfs.h"

/* Includes
 * - Library */
#include <string.h>

/* Open File - This function
 * handles both the opening of files
 * and creation of files depending on the
 * given flags */
VfsErrorCode_t MfsOpenFile(void *FsData, 
	MCoreFile_t *Handle, MString_t *Path, VfsFileFlags_t Flags)
{
	/* Cast */
	MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)FsData;
	MfsData_t *mData = (MfsData_t*)Fs->ExtendedData;
	VfsErrorCode_t RetCode = VfsOk;

	/* This will be a recursive parse of path */
	MfsFile_t *FileInfo = MfsLocateEntry(Fs, mData->RootIndex, Path, &RetCode);

	/* Validation Phase
	 * So how should we handle this? */
	if (RetCode == VfsPathNotFound
		&& Flags & CreateIfNotExists) 
	{
		/* Try to create the file */
		FileInfo = MfsCreateEntry(Fs, mData->RootIndex, Path, MFS_FILE, &RetCode);

		/* Invalid path or path not found 
		 * is enough cause for early break */
		if (RetCode != VfsOk
			&& RetCode != VfsPathExists) {
			return RetCode;
		}
	}
	else if (RetCode != VfsOk) {
		/* This means we have tried to open 
		 * a file normally - and it failed */
		return RetCode;
	}

	/* File exists */
	if (RetCode == VfsPathExists
		&& (Flags & FailIfExists)) {
		/* File already exists (and we didn't just create it)
		 * and for some reason that is not ok */
		kfree(FileInfo);
		return RetCode;
	}

	/* Post functions */
	if (RetCode == VfsPathExists
		&& (Flags & TruncateIfExists))
	{
		/* Free */
		int fRes = MfsFreeBuckets(Fs, FileInfo->DataBucket, FileInfo->InitialBucketLength);

		/* Only update entry if needs to be updated */
		if (!fRes) 
		{
			/* Update Stats */
			FileInfo->DataBucket = MFS_END_OF_CHAIN;
			FileInfo->InitialBucketLength = 0;
			FileInfo->Size = 0;
			
			/* Update Entry */
			RetCode = MfsUpdateEntry(Fs, FileInfo, MFS_ACTION_UPDATE);
		}
	}

	/* Fill out Handle */
	Handle->Name = FileInfo->Name;
	Handle->Size = FileInfo->Size;
	Handle->Data = FileInfo;

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

/* Open Handle - This function
 * initializes a new handle for a file entry 
 * this means we can reuse MCoreFiles */
VfsErrorCode_t MfsOpenHandle(void *FsData, MCoreFile_t *Handle, MCoreFileInstance_t *Instance)
{
	/* Vars */
	MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)FsData;
	MfsData_t *mData = (MfsData_t*)Fs->ExtendedData;
	MfsFileInstance_t *mInstance = NULL;
	MfsFile_t *FileInfo = NULL;

	/* Sanity */
	if (Handle == NULL
		|| Instance == NULL)
		return VfsInvalidParameters;

	/* Cast */
	FileInfo = (MfsFile_t*)Handle->Data;

	/* Allocate mFile instance */
	mInstance = (MfsFileInstance_t*)kmalloc(sizeof(MfsFileInstance_t));

	/* Setup */
	mInstance->BucketByteBoundary = 0;
	mInstance->DataBucketLength = FileInfo->InitialBucketLength;
	mInstance->DataBucketPosition = FileInfo->DataBucket;

	/* Allocate */
	mInstance->BucketBuffer = (uint8_t*)kmalloc((mData->BucketSize * Fs->SectorSize));

	/* Set */
	Instance->Instance = mInstance;

	/* Done! */
	return VfsOk;
}

/* Close Handle - This function cleans
 * up a previously allocated file instance handle */
VfsErrorCode_t MfsCloseHandle(void *FsData, MCoreFileInstance_t *Instance)
{
	/* Vars */
	MfsFileInstance_t *mInstance = NULL;

	/* Sanity */
	if (Instance == NULL
		|| Instance->Instance == NULL)
		return VfsInvalidParameters;

	/* Unused */
	_CRT_UNUSED(FsData);

	/* Cast */
	mInstance = (MfsFileInstance_t*)Instance->Instance;

	/* Free buffer */
	kfree(mInstance->BucketBuffer);

	/* Cleanup */
	kfree(mInstance);

	/* Done! */
	return VfsOk;
}

/* Read File - Reads from a 
 * given MCoreFile entry, the position
 * is stored in our structures and thus not neccessary 
 * File must be opened with read permissions */
size_t MfsReadFile(void *FsData, MCoreFileInstance_t *Instance, uint8_t *Buffer, size_t Size)
{
	/* Variables, cast the neccessary information
	 * and retrieve data */
	MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)FsData;
	MfsFileInstance_t *mInstance = (MfsFileInstance_t*)Instance->Instance;
	MfsData_t *mData = (MfsData_t*)Fs->ExtendedData;

	/* Variables for iteration and 
	 * state-keeping */
	size_t SizeOfBucket = (mData->BucketSize * Fs->SectorSize);
	uint64_t Position = Instance->Position;
	VfsErrorCode_t RetCode = VfsOk;
	uint8_t *BufPtr = Buffer;
	size_t BytesToRead = Size;
	size_t BytesRead = 0;

	/* Sanitize the amount of bytes we want
	 * to read, cap it at bytes available */
	if ((Position + Size) > Instance->File->Size)
		BytesToRead = (size_t)(Instance->File->Size - Position);

	/* Keep reeeading */
	while (BytesToRead)
	{
		/* Calculate initial variables we will need
		 * to read the data, since we MUST read in sector-size boundaries
		 * it's possible we will have to use a temporary buffer */
		uint64_t SectorLba = mData->BucketSize * mInstance->DataBucketPosition;
		uint64_t BucketClusterOffset = Position - mInstance->BucketByteBoundary;
		size_t SizeOfBucketCluster = (mInstance->DataBucketLength * SizeOfBucket);
		size_t nBuckets = (size_t)(BucketClusterOffset / SizeOfBucket);
		size_t AdjustedByteCount = nBuckets * (mData->BucketSize * Fs->SectorSize);

		/* These are WHERE and HOW much will actually 
		 * be transfered for this iteration */
		uint8_t *TransferBuffer = mInstance->BucketBuffer;
		size_t TransferSize = mData->BucketSize;

		/* Special large case, this is for speedups 
		 * if we are trying to read more than the entirety of
		 * this bucket cluster, we can just use the buffer provided 
		if (BytesToRead >= SizeOfBucket && BucketClusterOffset == 0) {
			TransferBuffer = BufPtr;
			TransferSize = (BytesToRead / SizeOfBucket) * mData->BucketSize;
		}*/

		/* Adjust SectorLba, because the bucket we are reading in
		 * might actually consist of a lot of buckets, so nBuckets
		 * contain the bucket offset in the bucket-cluster */
		SectorLba += (nBuckets * mData->BucketSize);

		/* Read the bucket */
		if (MfsReadSectors(Fs, SectorLba, TransferBuffer, TransferSize) != RequestNoError) {
			RetCode = VfsDiskError;
			LogFatal("MFS1", "READFILE: Error reading sector %u from disk",
				(size_t)(mData->BucketSize * mInstance->DataBucketPosition));
			LogFatal("MFS1", "Bucket Position %u, mFile Position %u, mFile Size %u",
				mInstance->DataBucketPosition, (size_t)Position, (size_t)Instance->File->Size);
			break;
		}

		/* Use this to indicate how much
		 * we moved forward with bytes */
		size_t BytesCopied = 0;

		if (TransferBuffer != BufPtr) {
			/* We have to calculate the offset into this buffer we must transfer data */
			size_t bOffset = (size_t)(Position - (mInstance->BucketByteBoundary + AdjustedByteCount));
			size_t BytesLeft = (TransferSize * Fs->SectorSize) - bOffset;

			/* We have a few cases
			* Case 1: We have enough data here
			* Case 2: We have to read more than is here */
			if (BytesToRead > BytesLeft) {
				/* Start out by copying remainder */
				memcpy(BufPtr, (TransferBuffer + bOffset), BytesLeft);
				BytesCopied = BytesLeft;
			}
			else {
				/* Just copy */
				memcpy(BufPtr, (TransferBuffer + bOffset), BytesToRead);
				BytesCopied = BytesToRead;
			}
		}
		else {
			BytesCopied = (TransferSize * Fs->SectorSize);
		}

		/* Advance pointer(s) */
		BytesRead += BytesCopied;
		BufPtr += BytesCopied;
		BytesToRead -= BytesCopied;
		Position += BytesCopied;

		/* Switch to next bucket? */
		if (Position == (mInstance->BucketByteBoundary + SizeOfBucketCluster)) {
			uint32_t NextBucket = 0, BucketLength = 0;

			/* Go to next */
			if (MfsGetNextBucket(Fs, mInstance->DataBucketPosition, &NextBucket, &BucketLength))
				break;

			/* Sanity */
			if (NextBucket != MFS_END_OF_CHAIN) {
				mInstance->DataBucketPosition = NextBucket;
				mInstance->DataBucketLength = BucketLength;

				/* Update bucket boundary */
				mInstance->BucketByteBoundary +=
					(BucketLength * mData->BucketSize * Fs->SectorSize);
			}
			else
				Instance->IsEOF = 1;
		}

		/* Sanity */
		if (Instance->IsEOF)
			break;
	}

	/* Sanity */
	if (Position >= Instance->File->Size)
		Instance->IsEOF = 1;

	/* Done! */
	Instance->Code = RetCode;
	return BytesRead;
}

/* Write File - Writes to a 
 * given MCoreFile entry, the position
 * is stored in our structures and thus not neccessary 
 * File must be opened with write permissions */
size_t MfsWriteFile(void *FsData, MCoreFileInstance_t *Instance, uint8_t *Buffer, size_t Size)
{
	/* Vars */
	MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)FsData;
	MfsFileInstance_t *mInstance = (MfsFileInstance_t*)Instance->Instance;
	MfsData_t *mData = (MfsData_t*)Fs->ExtendedData;
	MfsFile_t *mFile = (MfsFile_t*)Instance->File->Data;
	VfsErrorCode_t RetCode = VfsOk;
	uint8_t *BufPtr = Buffer;
	uint64_t Position = Instance->Position;

	/* BucketPtr for iterating */
	size_t BytesWritten = 0;
	size_t BytesToWrite = Size;

	/* Make sure there is enough room */
	if ((Instance->Position + Size) > mFile->AllocatedSize
		|| mFile->DataBucket == MFS_END_OF_CHAIN)
	{
		/* Well... Uhh */
		
		/* Allocate more */
		uint64_t NumSectors = DIVUP(((Instance->Position + Size) - mFile->AllocatedSize), Fs->SectorSize);
		uint64_t NumBuckets = DIVUP(NumSectors, mData->BucketSize);

		/* Allocate buckets */
		uint32_t FreeBucket = mData->FreeIndex;
		uint32_t FreeLength = 0;
		MfsAllocateBucket(Fs, (uint32_t)NumBuckets, &FreeLength);

		/* Get last bucket in chain */
		uint32_t BucketPtr = mFile->DataBucket;
		uint32_t BucketPrevPtr = mFile->DataBucket;
		uint32_t BucketLength = 0;

		/* Iterate to last entry */
		while (BucketPtr != MFS_END_OF_CHAIN) {
			BucketPrevPtr = BucketPtr;
			MfsGetNextBucket(Fs, BucketPtr, &BucketPtr, &BucketLength);
		}

		/* Adjust allocated size */
		mFile->AllocatedSize += (NumBuckets * mData->BucketSize * Fs->SectorSize);

		/* Sanity -> First bucket?? */
		if (BucketPrevPtr == MFS_END_OF_CHAIN) {
			mInstance->DataBucketPosition = mFile->DataBucket = FreeBucket;
			mFile->InitialBucketLength = mInstance->DataBucketLength = FreeLength;
		}
		else {
			/* Update pointer */
			MfsSetNextBucket(Fs, BucketPrevPtr, FreeBucket, FreeLength, 1);
		}

		/* Now, update entry on disk 
		 * thats important if next steps fail */
		RetCode = MfsUpdateEntry(Fs, mFile, MFS_ACTION_UPDATE);

		/* Sanity */
		if (RetCode != VfsOk) {
			Instance->Code = RetCode;
			return 0;
		}
	}

	/* Keep wriiiting */
	while (BytesToWrite)
	{
		uint64_t SectorLba = mData->BucketSize * mInstance->DataBucketPosition;
		uint64_t bbOffset = Position - mInstance->BucketByteBoundary;
		size_t SizeOfBucket = (mData->BucketSize * Fs->SectorSize);
		size_t SizeOfThisBucket = (mInstance->DataBucketLength * SizeOfBucket);
		size_t nBuckets = (size_t)(bbOffset / SizeOfBucket);
		size_t AdjustedByteCount = 0;

		/* Allocate buffer for data */
		uint8_t *TransferBuffer = NULL;
		size_t TransferSize = 0;

		/* Determine optimal transfer buffer */
		if (BytesToWrite <= SizeOfBucket
			|| SizeOfThisBucket == SizeOfBucket) {
			/* Use ours */
			TransferBuffer = mInstance->BucketBuffer;
			TransferSize = mData->BucketSize;
		}
		else {
			/* Find optimal size */
			size_t BucketsNeeded = DIVUP(BytesToWrite, SizeOfBucket);
			TransferBuffer = (uint8_t*)kmalloc(BucketsNeeded * SizeOfBucket);
			TransferSize = BucketsNeeded * mData->BucketSize;
		}

		/* Adjust SectorLba */
		AdjustedByteCount = nBuckets * (mData->BucketSize * Fs->SectorSize);
		SectorLba += nBuckets * mData->BucketSize;

		/* We have to calculate the offset into this buffer we must transfer data */
		size_t bOffset = (size_t)(Position - mInstance->BucketByteBoundary - AdjustedByteCount);
		size_t BytesLeft = (TransferSize * Fs->SectorSize) - bOffset;
		size_t BytesCopied = 0;

		/* Are we on a bucket boundary ?
		 * and we need to write atleast an entire bucket */
		if (bOffset == 0 && BytesToWrite >= (TransferSize * Fs->SectorSize))
		{
			/* Then we don't care about content */
			memcpy(TransferBuffer, BufPtr, (TransferSize * Fs->SectorSize));
			BytesCopied = (TransferSize * Fs->SectorSize);
		}
		else
		{
			/* Means we are modifying */

			/* Read the old bucket */
			if (MfsReadSectors(Fs, SectorLba, TransferBuffer, TransferSize) != RequestNoError)
			{
				/* Error */
				RetCode = VfsDiskError;
				LogFatal("MFS1", "WRITEFILE: Error reading sector %u from disk",
					(size_t)(mData->BucketSize * mInstance->DataBucketPosition));
				LogFatal("MFS1", "Bucket Position %u, mFile Position %u, mFile Size %u",
					mInstance->DataBucketPosition, (size_t)Position, (size_t)Instance->File->Size);
				break;
			}
			
			/* Buuuut, we have quite a few cases here 
			 * Case 1 - We need to write less than what is left, easy */
			if (BytesToWrite <= BytesLeft)
			{
				/* Write it */
				memcpy((TransferBuffer + bOffset), BufPtr, BytesToWrite);
				BytesCopied = BytesToWrite;
			}
			else
			{
				/* Write whats left */
				memcpy((TransferBuffer + bOffset), BufPtr, BytesLeft);
				BytesCopied = BytesLeft;
			}
		}

		/* Write back bucket */
		if (MfsWriteSectors(Fs, SectorLba, TransferBuffer, TransferSize) != RequestNoError)
		{
			/* Error */
			RetCode = VfsDiskError;
			LogFatal("MFS1", "WRITEFILE: Error writing to disk");
			break;
		}

		/* Done with buffer */
		if (TransferSize != mData->BucketSize)
			kfree(TransferBuffer);

		/* Advance pointer(s) */
		BytesWritten += BytesCopied;
		BufPtr += BytesCopied;
		BytesToWrite -= BytesCopied;
		Position += BytesCopied;

		/* Switch to next bucket? */
		if (Position == mInstance->BucketByteBoundary + SizeOfThisBucket)
		{
			/* Go to next */
			uint32_t NextBucket = 0;
			uint32_t BucketLength = 0;
			MfsGetNextBucket(Fs, mInstance->DataBucketPosition, &NextBucket, &BucketLength);

			/* Sanity */
			if (NextBucket != MFS_END_OF_CHAIN)
				mInstance->DataBucketPosition = NextBucket;
		}
	}

	/* Update position */
	Instance->Position = Position;

	/* Sanity */
	if (Instance->Position > Instance->File->Size) {
		Instance->File->Size = Instance->Position;
		mFile->Size = Instance->Position;
	}

	/* Update entry */
	RetCode = MfsUpdateEntry(Fs, mFile, MFS_ACTION_UPDATE);

	/* Done! */
	Instance->Code = RetCode;
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
VfsErrorCode_t MfsSeek(void *FsData, MCoreFileInstance_t *Instance, uint64_t Position)
{
	/* Vars */
	MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)FsData;
	MfsFileInstance_t *mInstance = (MfsFileInstance_t*)Instance->Instance;
	MfsData_t *mData = (MfsData_t*)Fs->ExtendedData;
	MfsFile_t *mFile = (MfsFile_t*)Instance->File->Data;
	int ConstantLoop = 1;

	/* Sanity */
	if (Position > Instance->File->Size)
		return VfsInvalidParameters;
	if (Instance->File->Size == 0)
		return VfsOk;

	/* Step 1, if the new position is in
	 * initial bucket, we need to do no actual
	 * seeking */
	size_t InitialBucketMax = (mFile->InitialBucketLength * (mData->BucketSize * Fs->SectorSize));

	/* Lets see */
	if (Position < InitialBucketMax) {
		mInstance->DataBucketPosition = mFile->DataBucket;
		mInstance->DataBucketLength = mFile->InitialBucketLength;
		mInstance->BucketByteBoundary = 0;
	}
	else
	{
		/* Step 2. We might still get out easy
		 * if we are setting a new position that's 
		 * within the current bucket */

		/* Do we cross a boundary? */
		uint64_t OldBucketLow = mInstance->BucketByteBoundary;
		uint64_t OldBucketHigh = OldBucketLow +
			(mInstance->DataBucketLength * (mData->BucketSize * Fs->SectorSize));

		/* Are we still in the same current bucket? */
		if (Position >= OldBucketLow
			&& Position < OldBucketHigh) {
			/* Do Nothing */
		}
		else
		{
			/* We need to do stuff... 
			 * Start over.. */

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
					&& Position < (PositionBoundLow + PositionBoundHigh)) {
					mInstance->BucketByteBoundary = PositionBoundLow;
					break;
				}

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
			if (BucketPtr != MFS_END_OF_CHAIN)
				mInstance->DataBucketPosition = BucketPtr;
		}
	}
	
	/* Update pointer */
	Instance->Position = Position;

	/* Set EOF */
	if (Instance->Position == Instance->File->Size) {
		Instance->IsEOF = 1;
	}
	
	/* Done */
	return VfsOk;
}

/* Query information - This function
 * is used to query a file for information 
 * or a directory for it's entries etc */
VfsErrorCode_t MfsQuery(void *FsData, MCoreFileInstance_t *Instance, 
	VfsQueryFunction_t Function, void *Buffer, size_t Length)
{
	/* Vars & Casts */
	MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)FsData;
	MfsFile_t *mFile = (MfsFile_t*)Instance->File->Data;

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
			Stats->Position = Instance->Position + Instance->oBufferPosition;
			Stats->Access = (int)Instance->Flags;

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
OsStatus_t MfsDestroy(void *FsData, int Forced)
{
	/* Cast */
	MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)FsData;
	MfsData_t *mData = (MfsData_t*)Fs->ExtendedData;

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
	return OsNoError;
}

/* FsInitialize 
 * Initializes a new instance of the file system
 * and allocates resources for the given descriptor */
OsStatus_t
FsInitialize(
	_In_ FileSystemDescriptor_t *Descriptor)
{
	// Variables
	MasterRecord_t *MasterRecord = NULL;
	BootRecord_t *BootRecord = NULL;
	BufferObject_t *Buffer = NULL;
	MfsInstance_t *Mfs = NULL;

	// Create a generic transferbuffer for us to use
	Buffer = CreateBuffer(Descriptor->Disk.Descriptor.SectorSize);

	// Read the boot-sector

	/* Read bootsector */
	if (MfsReadSectors(Fs, 0, TmpBuffer, 1) != RequestNoError)
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
	if (MfsReadSectors(Fs, mData->MbSector, TmpBuffer, 1) != RequestNoError)
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
	Fs->ExtendedData = mData;

	/* Done, cleanup */
	kfree(TmpBuffer);
	return;
}
