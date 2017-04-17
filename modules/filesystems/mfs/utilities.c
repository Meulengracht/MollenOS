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
		GetBufferAddress(Buffer), Count);
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
		GetBufferAddress(Buffer), Count);
}

/* MfsUpdateMasterRecord
 * master-bucket and it's mirror 
 * by writing the updated stats in our stored data */
OsStatus_t
MfsUpdateMasterRecord(
	_In_ FileSystemDescriptor_t *Descriptor)
{
	// Variables
	MfsInstance_t *Mfs = NULL;

	// Trace
	TRACE("MfsUpdateMasterRecord()");

	// Instantiate the pointers
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;

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
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;

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
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;

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
	MapRecord_t Record;
	uint32_t Bucket, PreviousBucket;
	size_t Counter;

	// Trace
	TRACE("MfsAllocateBuckets(Bucket %u, Link %u)",
		Bucket, Link->Link);

	// Instantiate the pointers
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;

	// Instantiate out
	RecordResult->Link = Mfs->MasterRecord.FreeBucket;
	RecordResult->Length = 0;
	PreviousBucket = 0;

	// Instantiate our varibles
	Bucket = Mfs->MasterRecord.FreeBucket;
	Counter = BucketCount;

	// Do allocation in a for-loop as bucket-sizes
	// are variable and thus we might need multiple
	// allocations to satisfy the demand
	while (Counter > 0) {
		// Get next free bucket
		if (MfsGetBucketLink(Descriptor, Bucket, &Record) != OsNoError) {
			ERROR("Failed to retrieve link for bucket %u", Bucket);
			return OsError;
		}

		// Bucket points to the free bucket index
		// Record.Link holds the link of <Bucket>
		// Record.Length holds the length of <Bucket>

		// We now have two cases, either the next block is
		// larger than the number of buckets we are asking for
		// or it's smaller
		if (Record.Length > Counter) {
			// Ok, this block is larger than what we need
			// We now need to first, update the free index to these values
			// Map[Bucket] = (Counter) | (MFS_ENDOFCHAIN)
			// Map[Bucket + Counter] = (Length - Counter) | Link
			MapRecord_t Update, Next;

			// Set update
			Update.Link = MFS_ENDOFCHAIN;
			Update.Length = Counter;

			// Set next
			Next.Link = Record.Link;
			Next.Length = Record.Length - Counter;

			// Make sure only to update out once, we just need
			// the initial size, not for each new allocation
			if (RecordResult->Length == 0) {
				RecordResult->Length = Update.Length;
			}

			// We have to adjust now, since we are taking 
			// only a chunk of the available length
			// Map[Bucket] = (Counter) | (MFS_ENDOFCHAIN)
			// Map[Bucket + Counter] = (Length - Counter) | PreviousLink
			if (MfsSetBucketLink(Descriptor, Bucket, &Update, 1) != OsNoError
				&& MfsSetBucketLink(Descriptor, Bucket + Counter, &Next, 1) != OsNoError) {
				ERROR("Failed to update link for bucket %u and %u", 
					Bucket, Bucket + Counter);
				return OsError;
			}

			// Update the master-record and we are done
			Mfs->MasterRecord.FreeBucket = Bucket + Counter;
			return MfsUpdateMasterRecord(Descriptor);
		}
		else {
			// Ok, block is either exactly the size we need or less
			// than what we need

			// Make sure only to update out once, we just need
			// the initial size, not for each new allocation
			if (RecordResult->Length == 0) {
				RecordResult->Length = Record.Length;
			}

			// Decrease allocation amount
			Counter -= Record.Length;

			// Set bucket to next
			PreviousBucket = Bucket;
			Bucket = Record.Link;
		}
	}

	// If we reach here it was because we encountered a block
	// that was exactly the fit we needed
	// So we set FreeIndex to Bucket
	// We set Record.Link to ENDOFCHAIN
	// We leave size unchanged

	// We want to update the last bucket of the chain
	// but not update the length
	Record.Link = MFS_ENDOFCHAIN;

	// Update the previous bucket to MFS_ENDOFCHAIN
	if (MfsSetBucketLink(Descriptor, PreviousBucket, &Record, 0) != OsNoError) {
		ERROR("Failed to update link for bucket %u", PreviousBucket);
		return OsError;
	}
	
	// Update the master-record and we are done
	Mfs->MasterRecord.FreeBucket = Bucket;
	return MfsUpdateMasterRecord(Descriptor);
}

/* MfsFreeBuckets
 * Frees an entire chain of buckets that has been allocated for 
 * a file-record */
OsStatus_t
MfsFreeBuckets(
	_In_ FileSystemDescriptor_t *Descriptor, 
	_In_ uint32_t StartBucket,
	_In_ uint32_t StartLength)
{
	// Variables
	MfsInstance_t *Mfs = NULL;
	uint32_t PreviousBucket;
	MapRecord_t Record;

	// Trace
	TRACE("MfsFreeBuckets(Bucket %u, Length %u)",
		StartBucket, StartLength);

	// Instantiate the variables
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;
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
	PreviousBucket = MFS_ENDOFCHAIN;
	while (Record.Link != MFS_ENDOFCHAIN) {
		PreviousBucket = Record.Link;
		if (MfsGetBucketLink(Descriptor, Record.Link, &Record) != OsNoError) {
			ERROR("Failed to retrieve the next bucket-link");
			return OsError;
		}
	}

	// If there was no allocated buckets to start with
	// then do nothing
	if (PreviousBucket == MFS_ENDOFCHAIN) {
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
	else {
		return FsOk;
	}
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
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;

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

/* MfsUpdateRecord
 * Conveniance function for updating a given file on
 * the disk, not data related to file, but the metadata */
FileSystemCode_t
MfsUpdateRecord(
	_In_ FileSystemDescriptor_t *Descriptor, 
	_In_ MfsFile_t *Handle,
	_In_ int Action)
{
	// Variables
	FileSystemCode_t Result = FsOk;
	MfsInstance_t *Mfs = NULL;
	FileRecord_t *Record = NULL;
	size_t i;

	// Trace
	TRACE("MfsUpdateEntry(File %s)", MStringRaw(Handle->Name));

	// Instantiate the mfs pointer
	Mfs = (MfsInstance_t*)Descriptor->ExtensionData;

	// Read the stored data bucket where the record is
	if (MfsReadSectors(Descriptor, Mfs->TransferBuffer, 
		MFS_GETSECTOR(Mfs, Handle->DirectoryBucket), Mfs->SectorsPerBucket) != OsNoError) {
		ERROR("Failed to read bucket %u", Handle->DirectoryBucket);
		Result = FsDiskError;
		goto Cleanup;
	}
	
	// Fast-forward to the correct entry
	Record = (FileRecord_t*)GetBufferData(Mfs->TransferBuffer);
	for (i = 0; i < Handle->DirectoryIndex; i++) {
		Record++;
	}

	// We have two over-all cases here, as create/modify share
	// some code, and that is delete as the second. If we delete
	// we zero out the entry and set the status to deleted
	if (Action == MFS_ACTION_DELETE) {
		memset((void*)Record, 0, sizeof(FileRecord_t));
		Record->Status = MFS_FILERECORD_DELETED;
	}
	else {
		// Now we have two sub cases, but create just needs some
		// extra updates otherwise they share
		if (Action == MFS_ACTION_CREATE) {
			Record->Status = MFS_FILERECORD_INUSE;
			memcpy(&Record->Name[0],
				MStringRaw(Handle->Name), MStringSize(Handle->Name));
			memset(&Record->Integrated[0], 0, 512);
		}

		// Update stats that are modifiable
		Record->Flags = Handle->Flags;
		Record->StartBucket = Handle->StartBucket;
		Record->StartLength = Handle->StartLength;

		// Update modified / accessed dates

		// Update sizes
		Record->Size = Handle->Size;
		Record->AllocatedSize = Handle->AllocatedSize;
	}
	
	// Write the bucket back to the disk
	if (MfsWriteSectors(Descriptor, Mfs->TransferBuffer,
		MFS_GETSECTOR(Mfs, Handle->DirectoryBucket), Mfs->SectorsPerBucket) != OsNoError) {
		ERROR("Failed to update bucket %u", Handle->DirectoryBucket);
		Result = FsDiskError;
	}

	// Cleanup and exit
Cleanup:
	return Result;
}
