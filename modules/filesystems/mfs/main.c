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

#include <os/utils.h>
#include "mfs.h"

#include <stdlib.h>
#include <string.h>

/* FsOpenFile 
 * Opens a new link to a file and allocates resources
 * for a new open-file in the system */
FileSystemCode_t 
FsOpenFile(
	_In_ FileSystemDescriptor_t*    Descriptor,
	_In_ FileSystemFile_t*          File,
	_In_ MString_t*                 Path)
{
	// Variables
	MfsInstance_t *Mfs      = NULL;
	MfsFile_t *fInformation = NULL;
	FileSystemCode_t Result;

	// Trace
	TRACE("FsOpenFile(Path %s)", MStringRaw(Path));

	// Instantiate the pointers
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;

	// Try to locate the given file-record
	Result = MfsLocateRecord(Descriptor, Mfs->MasterRecord.RootIndex, 
		Path, &fInformation);
	if (Result != FsOk) {
		return Result;
	}

	// Fill out information in _out_
	File->Name          = fInformation->Name;
	File->Size          = fInformation->Size;
	File->ExtensionData = (uintptr_t*)fInformation;
	return Result;
}

/* FsCreateFile 
 * Creates a new link to a file and allocates resources
 * for a new open-file in the system */
FileSystemCode_t 
FsCreateFile(
	_In_ FileSystemDescriptor_t*    Descriptor,
	_In_ FileSystemFile_t*          File,
	_In_ MString_t*                 Path,
	_In_ Flags_t                    Options)
{
	// Variables
	MfsInstance_t *Mfs      = NULL;
	MfsFile_t *fInformation = NULL;
	FileSystemCode_t Result;

	// Trace
	TRACE("FsCreateFile(Path %s, Options 0x%x)", MStringRaw(Path), Options);

	// Instantiate the pointers
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;

	// Create the record
	Result = MfsCreateRecord(Descriptor, Mfs->MasterRecord.RootIndex,
		Path, Options, &fInformation);
	if (Result != FsOk) {
		return Result;
	}

	// Fill out information in _out_
	File->Name          = fInformation->Name;
	File->Size          = fInformation->Size;
	File->ExtensionData = (uintptr_t*)fInformation;
	return Result;
}

/* FsCloseFile 
 * Closes the given file-link and frees all resources
 * this is only invoked once all handles has been closed
 * to that file link, or the file-system is unmounted */
FileSystemCode_t
FsCloseFile(
	_In_ FileSystemDescriptor_t*    Descriptor, 
	_In_ FileSystemFile_t*          File)
{
	// Variables
	MfsFile_t *fInformation = NULL;

	// Trace
	TRACE("FsCloseFile(Hash 0x%x)", File->Hash);

	// Instantiate the pointers
	fInformation    = (MfsFile_t*)File->ExtensionData;
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
	_In_ FileSystemDescriptor_t* Descriptor,
	_In_ FileSystemFileHandle_t* Handle)
{
	// Variables
	MfsFileInstance_t *fInstance    = NULL;
	MfsFile_t *fInformation         = NULL;

	// Trace
	TRACE("FsOpenHandle(Id 0x%x)", Handle->Id);

	// Instantiate the pointers
	fInformation = (MfsFile_t*)Handle->File->ExtensionData;
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
	Handle->ExtensionData = (uintptr_t*)fInstance;
	return FsOk;
}

/* FsCloseHandle 
 * Closes the file handle and cleans up any resources allocated
 * by the OpenHandle equivelent. Renders the handle useless */
FileSystemCode_t
FsCloseHandle(
	_In_ FileSystemDescriptor_t* Descriptor,
	_In_ FileSystemFileHandle_t* Handle)
{
	// Variables
	MfsFileInstance_t *fHandle  = NULL;

	// Trace
	TRACE("FsCloseHandle(Id 0x%x)", Handle->Id);

	// Instantiate the pointers
	fHandle = (MfsFileInstance_t*)Handle->ExtensionData;
	if (fHandle == NULL) {
		return FsInvalidParameters;
	}

	// Cleanup the instance
	free(fHandle);
	return FsOk;
}

/* FsReadFile 
 * Reads the requested number of bytes from the given
 * file handle and outputs the number of bytes actually read */
FileSystemCode_t
FsReadFile(
    _In_  FileSystemDescriptor_t*   Descriptor,
    _In_  FileSystemFileHandle_t*   Handle,
    _In_  DmaBuffer_t*              BufferObject,
    _In_  size_t                    Length,
    _Out_ size_t*                   BytesAt,
    _Out_ size_t*                   BytesRead)
{
	// Variables
	MfsFileInstance_t *fInstance    = NULL;
	MfsFile_t *fInformation         = NULL;
	MfsInstance_t *Mfs              = NULL;
	FileSystemCode_t Result         = FsOk;
	uintptr_t DataPointer           = 0;
	uint64_t Position               = 0;
	size_t BucketSizeBytes          = 0;
	size_t BytesToRead              = 0;

	// Trace
	TRACE("FsReadFile(Id 0x%x, Position %u, Length %u)",
		Handle->Id, LODWORD(Handle->Position), Length);

	// Instantiate the pointers
	Mfs             = (MfsInstance_t*)Descriptor->ExtensionData;
	fInstance       = (MfsFileInstance_t*)Handle->ExtensionData;
	fInformation    = (MfsFile_t*)Handle->File->ExtensionData;

	// Instantiate some of the contents
	BucketSizeBytes = Mfs->SectorsPerBucket * Descriptor->Disk.Descriptor.SectorSize;
	DataPointer     = GetBufferDma(BufferObject);
	Position        = Handle->Position;
	BytesToRead     = Length;
	*BytesRead      = 0;
	*BytesAt        = Handle->Position % Descriptor->Disk.Descriptor.SectorSize;

	// Sanitize the amount of bytes we want
	// to read, cap it at bytes available
	if ((Position + BytesToRead) > Handle->File->Size) {
		BytesToRead = (size_t)(Handle->File->Size - Position);
	}

    // Debug initial counters
    TRACE("BucketSize: %u, Physical Pointer: 0x%x, File Position %u, BytesToRead %u",
        BucketSizeBytes, DataPointer, LODWORD(Position), BytesToRead);

	// Read the current sector, update index to where data starts
	// Keep reading consecutive after that untill all bytes requested have
	// been read

	// Read in a loop to make sure we read all requested bytes
	while (BytesToRead) {
		// Calculate which bucket, then the sector offset
		// Then calculate how many sectors of the bucket we need to read
		uint64_t Sector         = MFS_GETSECTOR(Mfs, fInstance->DataBucketPosition);
		uint64_t SectorOffset   = Position % Descriptor->Disk.Descriptor.SectorSize;
		size_t SectorIndex      = (size_t)((Position - fInstance->BucketByteBoundary) / Descriptor->Disk.Descriptor.SectorSize);
		size_t SectorsLeft      = MFS_GETSECTOR(Mfs, fInstance->DataBucketLength) - SectorIndex;
		size_t SectorCount      = 0;
        size_t ByteCount        = 0;
		
        // Debug counter values
        TRACE(">> Physical Pointer: 0x%x, File Position %u, BytesToRead %u",
            DataPointer, LODWORD(Position), BytesToRead);

		// Calculate the sector index into bucket
		Sector += SectorIndex;

		// Calculate how many sectors we should read in
		SectorCount = DIVUP(BytesToRead, Descriptor->Disk.Descriptor.SectorSize);

		// Do we cross a boundary?
		if (SectorOffset + BytesToRead > Descriptor->Disk.Descriptor.SectorSize) {
			SectorCount++;
		}

		// Adjust for bucket boundary
		SectorCount = MIN(SectorsLeft, SectorCount);

		// Adjust for number of bytes read
		ByteCount = (size_t)MIN(BytesToRead, (SectorCount * Descriptor->Disk.Descriptor.SectorSize) - SectorOffset);

		// Ex pos 490 - length 50
		// SectorIndex = 0, SectorOffset = 490, SectorCount = 2 - ByteCount = 50 (Capacity 4096)
		// Ex pos 1109 - length 450
		// SectorIndex = 2, SectorOffset = 85, SectorCount = 2 - ByteCount = 450 (Capacity 4096)
		// Ex pos 490 - length 4000
		// SectorIndex = 0, SectorOffset = 490, SectorCount = 8 - ByteCount = 3606 (Capacity 4096)
		TRACE(">> Read metrics - Sector %u + %u, Count %u, ByteOffset %u, ByteCount %u",
			LODWORD(Sector), SectorIndex, SectorCount, LODWORD(SectorOffset), ByteCount);

		// If there is less than one sector left - break
		if ((Descriptor->Disk.Descriptor.SectorSize + *BytesRead) > GetBufferSize(BufferObject)) {
			WARNING("Ran out of buffer space, BytesRead %u, BytesLeft %u, Capacity %u",
				*BytesRead, BytesToRead, GetBufferSize(BufferObject));
			break;
		}

		// Perform the read (Raw - as we need to pass the datapointer)
		if (StorageRead(Descriptor->Disk.Driver, Descriptor->Disk.Device, 
			Descriptor->SectorStart + Sector, DataPointer, SectorCount) != OsSuccess) {
			ERROR("Failed to read sector");
			Result = FsDiskError;
			break;
		}

		// Increase the pointers and decrease with bytes read
		DataPointer += Descriptor->Disk.Descriptor.SectorSize * SectorCount;
		*BytesRead  += ByteCount;
		Position    += ByteCount;
		BytesToRead -= ByteCount;

		// Do we need to switch bucket?
		// We do if the position we have read to equals end of bucket
		if (Position == (fInstance->BucketByteBoundary 
			+ (fInstance->DataBucketLength * BucketSizeBytes))) {
			MapRecord_t Link;

			// We have to lookup the link for current bucket
			if (MfsGetBucketLink(Descriptor, 
				fInstance->DataBucketPosition, &Link) != OsSuccess) {
				ERROR("Failed to get link for bucket %u", fInstance->DataBucketPosition);
				Result = FsDiskError;
				break;
			}

			// Check for EOL
			if (Link.Link == MFS_ENDOFCHAIN) {
				break;
			}

			// Store link
			fInstance->DataBucketPosition = Link.Link;

			// Lookup length of link
			if (MfsGetBucketLink(Descriptor,
				fInstance->DataBucketPosition, &Link) != OsSuccess) {
				ERROR("Failed to get length for bucket %u", fInstance->DataBucketPosition);
				Result = FsDiskError;
				break;
			}

			// Store length & Update bucket boundary
			fInstance->DataBucketLength = Link.Length;
			fInstance->BucketByteBoundary += (Link.Length * BucketSizeBytes);
		}
	}
	return Result;
}

/* FsWriteFile 
 * Writes the requested number of bytes to the given
 * file handle and outputs the number of bytes actually written */
FileSystemCode_t
FsWriteFile(
    _In_  FileSystemDescriptor_t*   Descriptor,
    _In_  FileSystemFileHandle_t*   Handle,
    _In_  DmaBuffer_t*              BufferObject,
    _In_  size_t                    Length,
    _Out_ size_t*                   BytesWritten)
{
	// Variables
	MfsFileInstance_t *fInstance    = NULL;
	MfsFile_t *fInformation         = NULL;
	MfsInstance_t *Mfs              = NULL;
	FileSystemCode_t Result         = FsOk;
	uint64_t Position               = 0;
	size_t BucketSizeBytes          = 0;
	size_t BytesToWrite             = 0;

	// Trace
	TRACE("FsWriteFile(Id 0x%x, Position %u, Length %u)",
		Handle->Id, LODWORD(Handle->Position), Length);

	// Instantiate the pointers
	Mfs             = (MfsInstance_t*)Descriptor->ExtensionData;
	fInstance       = (MfsFileInstance_t*)Handle->ExtensionData;
	fInformation    = (MfsFile_t*)Handle->File->ExtensionData;

	// Instantiate some of the variables
	BucketSizeBytes = Mfs->SectorsPerBucket * Descriptor->Disk.Descriptor.SectorSize;
	Position        = Handle->Position;
	BytesToWrite    = Length;
	*BytesWritten   = 0;

	// Make sure record has enough disk-space allocated for 
	// the write operations
	if ((Position + BytesToWrite) > fInformation->AllocatedSize) {
		// Calculate the number of sectors, then number of buckets
		size_t NumSectors = (size_t)(DIVUP(((Position + BytesToWrite) - fInformation->AllocatedSize),
			Descriptor->Disk.Descriptor.SectorSize));
		size_t NumBuckets = DIVUP(NumSectors, Mfs->SectorsPerBucket);
		uint32_t BucketPointer, PreviousBucketPointer;
		MapRecord_t Iterator, Link;

		// Perform the allocation of buckets
		if (MfsAllocateBuckets(Descriptor, NumBuckets, &Link) != OsSuccess) {
			ERROR("Failed to allocate %u buckets for file", NumBuckets);
			return FsDiskError;
		}

		// Now iterate to end
		BucketPointer = fInformation->StartBucket;
		PreviousBucketPointer = MFS_ENDOFCHAIN;
		while (BucketPointer != MFS_ENDOFCHAIN) {
			PreviousBucketPointer = BucketPointer;
			if (MfsGetBucketLink(Descriptor, BucketPointer, &Iterator) != OsSuccess) {
				ERROR("Failed to get link for bucket %u", BucketPointer);
				return FsDiskError;
			}
			BucketPointer = Iterator.Link;
		}

		// We have a special case if previous == MFS_ENDOFCHAIN
		if (PreviousBucketPointer == MFS_ENDOFCHAIN) {
			// This means file had nothing allocated
			fInformation->StartBucket = Link.Link;
			fInformation->StartLength = Link.Length;
			fInstance->DataBucketPosition = Link.Link;
			fInstance->DataBucketLength = Link.Length;
			fInstance->BucketByteBoundary = 0;
		}
		else {
			if (MfsSetBucketLink(Descriptor, PreviousBucketPointer, &Link, 1) != OsSuccess) {
				ERROR("Failed to set link for bucket %u", PreviousBucketPointer);
				return FsDiskError;
			}
		}

		// Adjust the allocated-size of record
		fInformation->AllocatedSize += (NumBuckets * BucketSizeBytes);

		// Now, update entry on disk 
		// thats important if next steps fail
		Result = MfsUpdateRecord(Descriptor, fInformation, MFS_ACTION_UPDATE);

		// Sanitize update operation
		if (Result != FsOk) {
			ERROR("Failed to update record");
			return Result;
		}
	}
	
	// Write in a loop to make sure we write all requested bytes
	while (BytesToWrite) {
		// Calculate which bucket, then the sector offset
		// Then calculate how many sectors of the bucket we need to read
		uint64_t Sector         = MFS_GETSECTOR(Mfs, fInstance->DataBucketPosition);
		uint64_t SectorOffset   = (Position - fInstance->BucketByteBoundary) % Descriptor->Disk.Descriptor.SectorSize;
		size_t SectorIndex      = (size_t)((Position - fInstance->BucketByteBoundary) / Descriptor->Disk.Descriptor.SectorSize);
		size_t SectorsLeft      = MFS_GETSECTOR(Mfs, fInstance->DataBucketLength) - SectorIndex;
		size_t SectorCount      = 0, ByteCount = 0;

		// Ok - so sectorindex contains the index in the bucket
		// and sector offset contains the byte-offset in that sector

		// Calculate the sector index into bucket
		Sector += SectorIndex;

		// Calculate how many sectors we should read in
		SectorCount = DIVUP(BytesToWrite, Descriptor->Disk.Descriptor.SectorSize);

		// Do we cross a boundary?
		if (SectorOffset + BytesToWrite > Descriptor->Disk.Descriptor.SectorSize) {
			SectorCount++;
		}

		// Adjust for bucket boundary
		SectorCount = MIN(SectorsLeft, SectorCount);

		// Adjust for number of bytes read
		ByteCount = (size_t)MIN(BytesToWrite, (SectorCount * Descriptor->Disk.Descriptor.SectorSize) - SectorOffset);

		// Ex pos 490 - length 50
		// SectorIndex = 0, SectorOffset = 490, SectorCount = 2 - ByteCount = 50 (Capacity 4096)
		// Ex pos 1109 - length 450
		// SectorIndex = 2, SectorOffset = 85, SectorCount = 2 - ByteCount = 450 (Capacity 4096)
		// Ex pos 490 - length 4000
		// SectorIndex = 0, SectorOffset = 490, SectorCount = 8 - ByteCount = 3606 (Capacity 4096)
		TRACE("Write metrics - Sector %u + %u, Count %u, ByteOffset %u, ByteCount %u",
			LODWORD(Sector), SectorIndex, SectorCount, LODWORD(SectorOffset), ByteCount);

		// First of all, calculate the bounds as we might need to read
		// in existing data - Start out by clearing our combination buffer
		ZeroBuffer(Mfs->TransferBuffer);

		// Case 1 - Handle padding
		if (SectorOffset != 0 || ByteCount != Descriptor->Disk.Descriptor.SectorSize) {
			// Start building the sector
			if (MfsReadSectors(Descriptor, Mfs->TransferBuffer, Sector, SectorCount) != OsSuccess) {
				ERROR("Failed to read sector %u for combination step", 
					LODWORD(Sector));
				return FsDiskError;
			}
		}

		// Now write the data to the sector
		SeekBuffer(Mfs->TransferBuffer, (size_t)SectorOffset);
		CombineBuffer(Mfs->TransferBuffer, BufferObject, ByteCount, NULL);

		// Perform the write (Raw - as we need to pass the datapointer)
		if (MfsWriteSectors(Descriptor, Mfs->TransferBuffer, Sector, SectorCount) != OsSuccess) {
			ERROR("Failed to write sector %u", LODWORD(Sector));
			Result = FsDiskError;
			break;
		}

		// Increase the pointers and decrease with bytes read
		*BytesWritten 	+= ByteCount;
		Position 		+= ByteCount;
		BytesToWrite 	-= ByteCount;

		// Do we need to switch bucket?
		// We do if the position we have read to equals end of bucket
		if (Position == (fInstance->BucketByteBoundary
			+ (fInstance->DataBucketLength * BucketSizeBytes))) {
			MapRecord_t Link;

			// We have to lookup the link for current bucket
			if (MfsGetBucketLink(Descriptor, fInstance->DataBucketPosition, &Link) != OsSuccess) {
				ERROR("Failed to get link for bucket %u", fInstance->DataBucketPosition);
				Result = FsDiskError;
				break;
			}

			// Check for EOL
			if (Link.Link == MFS_ENDOFCHAIN) {
				break;
			}

			// Store link
			fInstance->DataBucketPosition = Link.Link;

			// Lookup length of link
			if (MfsGetBucketLink(Descriptor,
				fInstance->DataBucketPosition, &Link) != OsSuccess) {
				ERROR("Failed to get length for bucket %u", fInstance->DataBucketPosition);
				Result = FsDiskError;
				break;
			}

			// Store length
			fInstance->DataBucketLength = Link.Length;

			// Update bucket boundary
			fInstance->BucketByteBoundary += (Link.Length * BucketSizeBytes);
		}
	}

	// Update file position
	Handle->Position = Position;

	// Do we need to update the on-disk record with the
	// new file-size? 
	if (Handle->Position > Handle->File->Size) {
		Handle->File->Size = Handle->Position;
		fInformation->Size = Handle->Position;
	}

	// Update time-modified

	// Update on-disk record
	return MfsUpdateRecord(Descriptor, fInformation, MFS_ACTION_UPDATE);
}

/* FsDeletePath 
 * Deletes the file connected to the file-handle, this
 * will disconnect all existing file-handles to the file
 * and make them fail on next access */
FileSystemCode_t
FsDeletePath(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ MString_t*              Path,
    _In_ int                     Recursive)
{
	// Variables
	MfsInstance_t *Mfs          = NULL;
	MfsFile_t *fInformation     = NULL;
	FileSystemCode_t Result;

	// Trace
	TRACE("FsDeletePath(Path %s, Recursive %i)", MStringRaw(Path), Recursive);
    if (Recursive != 0) {
        ERROR("No support for deleting directories. Exitting");
        return FsInvalidParameters;
    }

	// Instantiate the pointers
	Mfs     = (MfsInstance_t*)Descriptor->ExtensionData;

	// Try to locate the given file-record
	Result  = MfsLocateRecord(Descriptor, Mfs->MasterRecord.RootIndex, Path, &fInformation);
	if (Result != FsOk) {
		return Result;
	}

	// Free all buckets allocated
	if (MfsFreeBuckets(Descriptor, fInformation->StartBucket, 
		fInformation->StartLength) != OsSuccess) {
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
	_In_ FileSystemDescriptor_t*    Descriptor,
	_In_ FileSystemFileHandle_t*    Handle,
	_In_ uint64_t                   AbsolutePosition)
{
	// Variables
	MfsFileInstance_t *fInstance    = NULL;
	MfsFile_t *fInformation         = NULL;
	MfsInstance_t *Mfs              = NULL;
	size_t InitialBucketMax         = 0;
	int ConstantLoop                = 1;

	// Trace
	TRACE("FsSeekFile(Id 0x%x, Position 0x%x)", 
		Handle->Id, LODWORD(AbsolutePosition));

	// Instantiate the pointers
	Mfs             = (MfsInstance_t*)Descriptor->ExtensionData;
	fInstance       = (MfsFileInstance_t*)Handle->ExtensionData;
	fInformation    = (MfsFile_t*)Handle->File->ExtensionData;

	// Sanitize seeking bounds
	if (AbsolutePosition > fInformation->Size
		|| fInformation->Size == 0) {
		return FsInvalidParameters;
	}

	// Step 1, if the new position is in
	// initial bucket, we need to do no actual
	// seeking
	InitialBucketMax                    = (fInformation->StartLength * 
		(Mfs->SectorsPerBucket * Descriptor->Disk.Descriptor.SectorSize));
	if (AbsolutePosition < InitialBucketMax) {
		fInstance->DataBucketPosition   = fInformation->StartBucket;
		fInstance->DataBucketLength     = fInformation->StartLength;
		fInstance->BucketByteBoundary   = 0;
	}
	else {
		// Step 2. We might still get out easy
		// if we are setting a new position that's 
		// within the current bucket
		uint64_t OldBucketLow, OldBucketHigh;

		// Calculate bucket boundaries
		OldBucketLow    = fInstance->BucketByteBoundary;
		OldBucketHigh   = OldBucketLow + (fInstance->DataBucketLength 
			* (Mfs->SectorsPerBucket * Descriptor->Disk.Descriptor.SectorSize));

		// If we are seeking inside the same bucket no need
		// to do anything else
		if (AbsolutePosition >= OldBucketLow
			&& AbsolutePosition < OldBucketHigh) {
			// Same bucket
		}
		else {
			// We need to figure out which bucket the position is in
			uint64_t PositionBoundLow   = 0;
			uint64_t PositionBoundHigh  = InitialBucketMax;
			MapRecord_t Link;

			// Start at the file-bucket
			uint32_t BucketPtr          = fInformation->StartBucket;
			uint32_t BucketLength       = fInformation->StartLength;
			while (ConstantLoop) {
				// Check if we reached correct bucket
				if (AbsolutePosition >= PositionBoundLow
					&& AbsolutePosition < (PositionBoundLow + PositionBoundHigh)) {
					fInstance->BucketByteBoundary = PositionBoundLow;
					break;
				}

				// Get link
				if (MfsGetBucketLink(Descriptor, BucketPtr, &Link) != OsSuccess) {
					ERROR("Failed to get link for bucket %u", BucketPtr);
					return FsDiskError;
				}

				// If we do reach end of chain, something went terribly wrong
				if (Link.Link == MFS_ENDOFCHAIN) {
					ERROR("Reached end of chain during seek");
					return FsInvalidParameters;
				}
				BucketPtr   = Link.Link;

				// Get length of link
				if (MfsGetBucketLink(Descriptor, BucketPtr, &Link) != OsSuccess) {
					ERROR("Failed to get length for bucket %u", BucketPtr);
					return FsDiskError;
				}
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
	return FsOk;
}

/* FsChangeFileSize 
 * Either expands or shrinks the allocated space for the given
 * file-handle to the requested size. */
FileSystemCode_t
FsChangeFileSize(
	_In_ FileSystemDescriptor_t*    Descriptor,
	_In_ FileSystemFile_t*          Handle,
	_In_ uint64_t                   Size)
{
	// Variables
	MfsFile_t *fInformation = NULL;

	// Trace
	TRACE("FsChangeFileSize(Name %s, Size 0x%x)",
		MStringRaw(Handle->Name), LODWORD(Size));

	// Instantiate the pointers
	fInformation = (MfsFile_t*)Handle->ExtensionData;

	// Handle a special case of 0
	if (Size == 0) {
		// Free all buckets allocated
		if (MfsFreeBuckets(Descriptor, fInformation->StartBucket,
			fInformation->StartLength) != OsSuccess) {
			ERROR("Failed to free the buckets at start 0x%x, length 0x%x",
				fInformation->StartBucket, fInformation->StartLength);
			return FsDiskError;
		}

		// Set new allocated size
		fInformation->AllocatedSize = 0;
		fInformation->StartBucket   = MFS_ENDOFCHAIN;
		fInformation->StartLength   = 0;
	}

	// Set new size
	fInformation->Size = Size;

	// Update time

	// Update the record on disk
	return MfsUpdateRecord(Descriptor, fInformation, MFS_ACTION_UPDATE);
}

/* FsQueryFile 
 * Queries the given file handle for information, the kind of
 * information queried is determined by the function */
FileSystemCode_t
FsQueryFile(
	_In_  FileSystemDescriptor_t*   Descriptor,
	_In_  FileSystemFileHandle_t*   Handle,
	_In_  int                       Function,
	_Out_ void*                     Buffer,
	_In_  size_t                    MaxLength)
{
	// Variables
	MfsFileInstance_t *fInstance    = NULL;
	MfsFile_t *fInformation         = NULL;

	// Trace
	TRACE("FsQueryFile(Id 0x%x, Function %i, Length %u)",
		Handle->Id, Function, MaxLength);

	// Instantiate the pointers
	fInstance       = (MfsFileInstance_t*)Handle->ExtensionData;
	fInformation    = (MfsFile_t*)Handle->File->ExtensionData;
	
    // @todo
	_CRT_UNUSED(Function);
	_CRT_UNUSED(Buffer);
	_CRT_UNUSED(MaxLength);

	// Not implemented atm
	return FsOk;
}

/* FsDestroy 
 * Destroys the given filesystem descriptor and cleans
 * up any resources allocated by the filesystem instance */
OsStatus_t
FsDestroy(
	_InOut_ FileSystemDescriptor_t* Descriptor,
	_In_    Flags_t                 UnmountFlags)
{
	// Variables
	MfsInstance_t *Mfs = NULL;

	// Instantiate the mfs pointer
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;

	// Sanity
	if (Mfs == NULL) {
		return OsError;
	}

	// Which kind of unmount is it?
	if (!(UnmountFlags & __DISK_FORCED_REMOVE)) {
		// Flush everything
        // @todo
	}

	// Cleanup all allocated resources
	if (Mfs->TransferBuffer != NULL) {
		DestroyBuffer(Mfs->TransferBuffer);
	}

	// Free the bucket-map
	if (Mfs->BucketMap != NULL) {
		free(Mfs->BucketMap);
	}

	// Free structure and return
	free(Mfs);
	return OsSuccess;
}

/* FsInitialize 
 * Initializes a new instance of the file system
 * and allocates resources for the given descriptor */
OsStatus_t
FsInitialize(
	_InOut_ FileSystemDescriptor_t *Descriptor)
{
	// Variables
	MasterRecord_t *MasterRecord    = NULL;
	BootRecord_t *BootRecord        = NULL;
	DmaBuffer_t *Buffer          	= NULL;
	MfsInstance_t *Mfs              = NULL;
	uint8_t *bMap                   = NULL;
	uint64_t BytesRead              = 0;
	uint64_t BytesLeft              = 0;
	size_t i, imax;

	// Trace
	TRACE("FsInitialize()");

	// Create a generic transferbuffer for us to use
	Buffer = CreateBuffer(UUID_INVALID, Descriptor->Disk.Descriptor.SectorSize);

	// Read the boot-sector
	if (MfsReadSectors(Descriptor, Buffer, 0, 1) != OsSuccess) {
		ERROR("Failed to read mfs boot-sector record");
		goto Error;
	}

	// Allocate a new instance of mfs
	Mfs                         = (MfsInstance_t*)malloc(sizeof(MfsInstance_t));
	Descriptor->ExtensionData   = (uintptr_t*)Mfs;

	// Instantiate the boot-record pointer
	BootRecord                  = (BootRecord_t*)GetBufferDataPointer(Buffer);

	// Process the boot-record
	if (BootRecord->Magic != MFS_BOOTRECORD_MAGIC) {
		ERROR("Failed to validate boot-record signature (0x%x, expected 0x%x)",
			BootRecord->Magic, MFS_BOOTRECORD_MAGIC);
		goto Error;
	}

	// Trace
	TRACE("Fs-Version: %u", BootRecord->Version);

	// Store some data from the boot-record
	Mfs->Version                    = (int)BootRecord->Version;
	Mfs->Flags                      = (Flags_t)BootRecord->Flags;
	Mfs->MasterRecordSector         = BootRecord->MasterRecordSector;
	Mfs->MasterRecordMirrorSector   = BootRecord->MasterRecordMirror;
	Mfs->SectorsPerBucket           = BootRecord->SectorsPerBucket;

	// Calculate where our map sector is
	Mfs->BucketCount                = Descriptor->SectorCount / Mfs->SectorsPerBucket;
	
	// Bucket entries are 64 bit (8 bytes) in map
	Mfs->BucketsPerSectorInMap      = Descriptor->Disk.Descriptor.SectorSize / 8;

	// Read the master-record
	if (MfsReadSectors(Descriptor, Buffer, Mfs->MasterRecordSector, 1) != OsSuccess) {
		ERROR("Failed to read mfs master-sector record");
		goto Error;
	}

	// Instantiate the master-record pointer
	MasterRecord                    = (MasterRecord_t*)GetBufferDataPointer(Buffer);

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
	Buffer                          = CreateBuffer(UUID_INVALID, Mfs->SectorsPerBucket 
		* Descriptor->Disk.Descriptor.SectorSize * MFS_ROOTSIZE);
	Mfs->TransferBuffer             = Buffer;

	// Allocate a buffer for the map
	Mfs->BucketMap = (uint32_t*)malloc((size_t)Mfs->MasterRecord.MapSize);

	// Trace
	TRACE("Caching bucket-map (Sector %u - Size %u Bytes)",
		LODWORD(Mfs->MasterRecord.MapSector),
		LODWORD(Mfs->MasterRecord.MapSize));

	// Load map
	bMap        = (uint8_t*)Mfs->BucketMap;
	BytesLeft   = Mfs->MasterRecord.MapSize;
    BytesRead   = 0;
	i           = 0;
    imax        = DIVUP(BytesLeft, (Mfs->SectorsPerBucket * Descriptor->Disk.Descriptor.SectorSize)); //GetBufferSize(Buffer)
	while (BytesLeft) {
		// Variables
		uint64_t MapSector = Mfs->MasterRecord.MapSector + (i * Mfs->SectorsPerBucket);
		size_t TransferSize = MIN((Mfs->SectorsPerBucket * Descriptor->Disk.Descriptor.SectorSize), (size_t)BytesLeft);
		size_t SectorCount = DIVUP(TransferSize, Descriptor->Disk.Descriptor.SectorSize);

		// Read sectors
		if (MfsReadSectors(Descriptor, Buffer, MapSector, SectorCount) != OsSuccess) {
			ERROR("Failed to read sector 0x%x (map) into cache", LODWORD(MapSector));
			goto Error;
		}

		// Reset buffer position to 0 and read the data into the map
		SeekBuffer(Buffer, 0);
		ReadBuffer(Buffer, (const void*)bMap, TransferSize, NULL);
		BytesLeft   -= TransferSize;
        BytesRead   += TransferSize;
		bMap        += TransferSize;
		i++;
        if (i == (imax / 4) || i == (imax / 2) || i == ((imax / 4) * 3)) {
            WARNING("Cached %u/%u bytes of sector-map", LODWORD(BytesRead), LODWORD(Mfs->MasterRecord.MapSize));
        }
	}

	// Update the structure
	return OsSuccess;

Error:
	// Cleanup mfs
	if (Mfs != NULL) {
		free(Mfs);
	}

	// Clear extension data
	Descriptor->ExtensionData = NULL;

	// Cleanup the transfer buffer
	DestroyBuffer(Buffer);
	return OsError;
}
