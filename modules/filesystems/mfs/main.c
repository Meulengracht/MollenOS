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
#include <stdlib.h>
#include <string.h>

/* FsOpenFile 
 * Opens a new link to a file and allocates resources
 * for a new open-file in the system */
FileSystemCode_t 
FsOpenFile(
	_In_ FileSystemDescriptor_t *Descriptor,
	_Out_ FileSystemFile_t *File,
	_In_ MString_t *Path)
{
	// Variables
	MfsInstance_t *Mfs = NULL;
	MfsFile_t *fInformation = NULL;
	FileSystemCode_t Result;

	// Trace
	TRACE("FsOpenFile(Path %u)", MStringRaw(Path));

	// Instantiate the pointers
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;

	// Try to locate the given file-record
	Result = MfsLocateRecord(Descriptor, Mfs->MasterRecord.RootIndex, 
		Path, &fInformation);

	// Sanitize the result
	if (Result != FsOk) {
		return Result;
	}

	// Fill out information in _out_
	File->Name = fInformation->Name;
	File->Size = fInformation->Size;
	File->ExtensionData = fInformation;

	// Done
	return Result;
}

/* FsCreateFile 
 * Creates a new link to a file and allocates resources
 * for a new open-file in the system */
FileSystemCode_t 
FsCreateFile(
	_In_ FileSystemDescriptor_t *Descriptor,
	_Out_ FileSystemFile_t *File,
	_In_ MString_t *Path,
	_In_ Flags_t Options)
{
	// Variables
	MfsInstance_t *Mfs = NULL;
	MfsFile_t *fInformation = NULL;
	FileSystemCode_t Result;

	// Trace
	TRACE("FsCreateFile(Path %u, Options 0x%x)", 
		MStringRaw(Path), Options);

	// Instantiate the pointers
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;

	// Create the record
	Result = MfsCreateRecord(Descriptor, Mfs->MasterRecord.RootIndex,
		Path, 0, &fInformation);

	// Sanitize the result
	if (Result != FsOk) {
		return Result;
	}

	// Fill out information in _out_
	File->Name = fInformation->Name;
	File->Size = fInformation->Size;
	File->ExtensionData = fInformation;

	// Done
	return Result;
}

/* FsCloseFile 
 * Closes the given file-link and frees all resources
 * this is only invoked once all handles has been closed
 * to that file link, or the file-system is unmounted */
FileSystemCode_t
FsCloseFile(
	_In_ FileSystemDescriptor_t *Descriptor, 
	_In_ FileSystemFile_t *File)
{
	// Variables
	MfsInstance_t *Mfs = NULL;
	MfsFile_t *fInformation = NULL;
	FileSystemCode_t Result;

	// Trace
	TRACE("FsCloseFile(Hash 0x%x)", File->Hash);

	// Instantiate the pointers
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;
	fInformation = (MfsFile_t*)File->ExtensionData;

	// Sanitize the pointer
	if (fInformation == NULL) {
		return FsOk;
	}

	// Cleanup data
	MStringDestroy(fInformation->Name);
	free(fInformation);
	return FsOk;
}

/* FsOpenHandle 
 * Opens a new handle to a file, this allows various
 * interactions with the base file, like read and write.
 * Neccessary resources and initialization of the Handle
 * should be done here too */
FileSystemCode_t
FsOpenHandle(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ FileSystemFileHandle_t *Handle)
{
	// Variables
	MfsFileInstance_t *fInstance = NULL;
	MfsFile_t *fInformation = NULL;
	MfsInstance_t *Mfs = NULL;

	// Trace
	TRACE("FsOpenHandle(Id 0x%x)", Handle->Id);

	// Instantiate the pointers
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;
	fInformation = (MfsFile_t*)Handle->File->ExtensionData;

	// Sanitize the parameters
	if (fInformation == NULL) {
		return FsInvalidParameters;
	}

	// Allocate a new file-handle
	fInstance = (MfsFileInstance_t*)malloc(sizeof(MfsFileInstance_t));

	// Initiate per-instance members
	fInstance->BucketByteBoundary = 0;
	fInstance->DataBucketPosition = fInformation->StartBucket;
	fInstance->DataBucketLength = fInformation->StartLength;

	// Update out
	Handle->ExtensionData = fInstance;

	// Done
	return FsOk;
}

/* FsCloseHandle 
 * Closes the file handle and cleans up any resources allocated
 * by the OpenHandle equivelent. Renders the handle useless */
FileSystemCode_t
FsCloseHandle(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ FileSystemFileHandle_t *Handle)
{
	// Variables
	MfsFileInstance_t *fHandle = NULL;
	MfsInstance_t *Mfs = NULL;

	// Trace
	TRACE("FsCloseHandle(Id 0x%x)", Handle->Id);

	// Instantiate the pointers
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;
	fHandle = (MfsFileInstance_t*)Handle->ExtensionData;

	// Sanitize the parameters
	if (fHandle == NULL) {
		return FsInvalidParameters;
	}

	// Cleanup the instance
	free(fHandle);
	return FsOk;
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

/* FsDeleteFile 
 * Deletes the file connected to the file-handle, this
 * will disconnect all existing file-handles to the file
 * and make them fail on next access */
FileSystemCode_t
FsDeleteFile(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ FileSystemFileHandle_t *Handle)
{
	// Variables
	MfsFileInstance_t *fHandle = NULL;
	MfsFile_t *fInformation = NULL;
	MfsInstance_t *Mfs = NULL;

	// Trace
	TRACE("FsDeleteFile(Id 0x%x)", Handle->Id);

	// Instantiate the pointers
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;
	fHandle = (MfsFileInstance_t*)Handle->ExtensionData;
	fInformation = (MfsFile_t*)Handle->File->ExtensionData;

	// Free all buckets allocated
	if (MfsFreeBuckets(Descriptor, fInformation->StartBucket, 
		fInformation->StartLength) != OsNoError) {
		ERROR("Failed to free the buckets at start 0x%x, length 0x%x",
			fInformation->StartBucket, fInformation->StartLength);
		return FsDiskError;
	}

	// Update the record to being deleted
	return MfsUpdateRecord(Descriptor, fInformation, MFS_ACTION_DELETE);
}

/* FsSeekFile 
 * Seeks in the given file-handle to the absolute position
 * given, must be within boundaries otherwise a seek won't
 * take a place */
FileSystemCode_t
FsSeekFile(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ FileSystemFileHandle_t *Handle,
	_In_ uint64_t AbsolutePosition)
{
	// Variables
	MfsFileInstance_t *fInstance = NULL;
	MfsFile_t *fInformation = NULL;
	MfsInstance_t *Mfs = NULL;
	size_t InitialBucketMax;
	int ConstantLoop = 1;

	// Trace
	TRACE("FsSeekFile(Id 0x%x, Position 0x%x)", 
		Handle->Id, LODWORD(AbsolutePosition));

	// Instantiate the pointers
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;
	fInstance = (MfsFileInstance_t*)Handle->ExtensionData;
	fInformation = (MfsFile_t*)Handle->File->ExtensionData;

	// Sanitize seeking bounds
	if (AbsolutePosition > fInformation->Size
		|| fInformation->Size == 0) {
		return FsInvalidParameters;
	}

	// Step 1, if the new position is in
	// initial bucket, we need to do no actual
	// seeking
	InitialBucketMax = (fInformation->StartLength * 
		(Mfs->SectorsPerBucket * Descriptor->Disk.Descriptor.SectorSize));
	if (AbsolutePosition < InitialBucketMax) {
		fInstance->DataBucketPosition = fInformation->StartBucket;
		fInstance->DataBucketLength = fInformation->StartLength;
		fInstance->BucketByteBoundary = 0;
	}
	else {
		// Step 2. We might still get out easy
		// if we are setting a new position that's 
		// within the current bucket
		uint64_t OldBucketLow, OldBucketHigh;

		// Calculate bucket boundaries
		OldBucketLow = fInstance->BucketByteBoundary;
		OldBucketHigh = OldBucketLow + (fInstance->DataBucketLength 
			* (Mfs->SectorsPerBucket * Descriptor->Disk.Descriptor.SectorSize));

		// If we are seeking inside the same bucket no need
		// to do anything else
		if (AbsolutePosition >= OldBucketLow
			&& AbsolutePosition < OldBucketHigh) {
			// Same bucket
		}
		else {
			// We need to figure out which bucket the position is in
			uint64_t PositionBoundLow = 0;
			uint64_t PositionBoundHigh = InitialBucketMax;
			MapRecord_t Link;

			// Start at the file-bucket
			uint32_t BucketPtr = fInformation->StartBucket;
			uint32_t BucketLength = fInformation->StartLength;
			while (ConstantLoop) {
				// Check if we reached correct bucket
				if (AbsolutePosition >= PositionBoundLow
					&& AbsolutePosition < (PositionBoundLow + PositionBoundHigh)) {
					fInstance->BucketByteBoundary = PositionBoundLow;
					break;
				}

				// Get link
				if (MfsGetBucketLink(Descriptor, BucketPtr, &Link) != OsNoError) {
					ERROR("Failed to get link for bucket %u", BucketPtr);
					return FsDiskError;
				}

				// If we do reach end of chain, something went terribly wrong
				if (Link.Link == MFS_ENDOFCHAIN) {
					ERROR("Reached end of chain during seek");
					return FsInvalidParameters;
				}

				// Update
				BucketPtr = Link.Link;

				// Get length of link
				BucketLength = Link.Length;

				// Calculate bounds for the new bucket
				PositionBoundLow += PositionBoundHigh;
				PositionBoundHigh = (BucketLength * 
					(Mfs->SectorsPerBucket * Descriptor->Disk.Descriptor.SectorSize));
			}

			// Update bucket pointer
			if (BucketPtr != MFS_ENDOFCHAIN) {
				fInstance->DataBucketPosition = BucketPtr;
			}
		}
	}
	
	// Update the new position since everything went ok
	Handle->Position = AbsolutePosition;

	// Done
	return FsOk;
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

/* FsDestroy 
 * Destroys the given filesystem descriptor and cleans
 * up any resources allocated by the filesystem instance */
OsStatus_t
FsDestroy(
	_InOut_ FileSystemDescriptor_t *Descriptor,
	_In_ Flags_t UnmountFlags)
{
	// Variables
	MfsInstance_t *Mfs = NULL;

	// Instantiate the mfs pointer
	Mfs = (MfsInstance_t*)Descriptor->ExtendedData;

	// Sanity
	if (Mfs == NULL) {
		return OsError;
	}

	// Which kind of unmount is it?
	if (!(UnmountFlags & __DISK_FORCED_REMOVE)) {
		// Flush everything
	}

	// Cleanup all allocated resources
	if (Mfs->TransferBuffer != NULL) {
		DestroyBuffer(Mfs->TransferBuffer);
	}

	// Free structure and return
	free(Mfs);
	return OsNoError;
}

/* FsInitialize 
 * Initializes a new instance of the file system
 * and allocates resources for the given descriptor */
OsStatus_t
FsInitialize(
	_InOut_ FileSystemDescriptor_t *Descriptor)
{
	// Variables
	MasterRecord_t *MasterRecord = NULL;
	BootRecord_t *BootRecord = NULL;
	BufferObject_t *Buffer = NULL;
	MfsInstance_t *Mfs = NULL;
	uint8_t *bMap = NULL;
	size_t BucketCount;
	size_t i;

	// Trace
	TRACE("FsInitialize()");

	// Create a generic transferbuffer for us to use
	Buffer = CreateBuffer(Descriptor->Disk.Descriptor.SectorSize);

	// Read the boot-sector
	if (MfsReadSectors(Descriptor, Buffer, 0, 1) != OsNoError) {
		ERROR("Failed to read mfs boot-sector record");
		goto Error;
	}

	// Allocate a new instance of mfs
	Mfs = (MfsInstance_t*)malloc(sizeof(MfsInstance_t));

	// Instantiate the boot-record pointer
	BootRecord = (BootRecord_t*)Buffer->Virtual;

	// Process the boot-record
	if (BootRecord->Magic != MFS_BOOTRECORD_MAGIC) {
		ERROR("Failed to validate boot-record signature (0x%x, expected 0x%x)",
			BootRecord->Magic, MFS_BOOTRECORD_MAGIC);
		goto Error;
	}

	// Trace
	TRACE("Fs-Version: %u", BootRecord->Version);

	// Store some data from the boot-record
	Mfs->Version = (int)BootRecord->Version;
	Mfs->Flags = (Flags_t)BootRecord->Flags;
	Mfs->MasterRecordSector = BootRecord->MasterRecordSector;
	Mfs->MasterRecordMirrorSector = BootRecord->MasterRecordMirror;
	Mfs->SectorsPerBucket = BootRecord->SectorsPerBucket;

	// Calculate where our map sector is
	Mfs->BucketCount = Descriptor->SectorCount / Mfs->SectorsPerBucket;
	
	// Bucket entries are 64 bit (8 bytes) in map
	Mfs->BucketsPerSectorInMap = Descriptor->Disk.Descriptor.SectorSize / 8;

	// Read the master-record
	if (MfsReadSectors(Descriptor, Buffer, Mfs->MasterRecordSector, 1) != OsNoError) {
		ERROR("Failed to read mfs master-sector record");
		goto Error;
	}

	// Instantiate the master-record pointer
	MasterRecord = (MasterRecord_t*)Buffer->Virtual;

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
	Buffer = CreateBuffer(Mfs->SectorsPerBucket * Descriptor->Disk.Descriptor.SectorSize);
	Mfs->TransferBuffer = Buffer;

	// Allocate a buffer for the map
	Mfs->BucketMap = (uint32_t*)malloc(Mfs->MasterRecord.MapSize);

	// Trace
	TRACE("Caching bucket-map (Sector %u - Size %u)",
		LODWORD(Mfs->MasterRecord.MapSector),
		LODWORD(Mfs->MasterRecord.MapSize));

	// Load map
	bMap = (uint8_t*)Mfs->BucketMap;
	BucketCount = DIVUP(Mfs->MasterRecord.MapSize, Mfs->SectorsPerBucket);
	for (i = 0; i < BucketCount; i++) {
		uint64_t MapSector = Mfs->MasterRecord.MapSector 
			+ (i * Mfs->SectorsPerBucket);
		if (MfsReadSectors(Descriptor, Buffer, MapSector, Mfs->SectorsPerBucket) != OsNoError) {
			ERROR("Failed to read sector 0x%x (map) into cache", LODWORD(MapSector));
			goto Error;
		}

		// Read the data into the map
		ReadBuffer(Buffer, (__CONST void*)bMap, Buffer->Length);
		bMap += Buffer->Length;
	}

	// Update the structure
	Descriptor->ExtensionData = (uintptr_t*)Mfs;
	return OsNoError;

Error:
	// Cleanup mfs
	if (Mfs != NULL) {
		free(Mfs);
	}

	// Cleanup the transfer buffer
	DestroyBuffer(Buffer);
	return OsError;
}
