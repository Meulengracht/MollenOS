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
 *  - Missing implementations:
 *    - Journaling
 *    - Encryptiong (AES)
 *	  - Versioning
 *	  - Inline-data
 *	  - Switch to B+ trees for metadata
 */

#ifndef _MFS_H_
#define _MFS_H_

/* Includes
 * - System */
#include <os/driver/contracts/filesystem.h>

/* Includes
 * - Library */
#include <os/osdefs.h>
#include <ds/mstring.h>

/* MFS Definitions and Utilities
 * Contains magic constant values and utility macros for conversion */
#define MFS_ENDOFCHAIN							0xFFFFFFFF
#define MFS_GETSECTOR(mInstance, Bucket)		((Mfs->SectorsPerBucket * Bucket))

/* MFS Update Entry Action Codes */
#define MFS_ACTION_UPDATE	0x0
#define MFS_ACTION_CREATE	0x1
#define MFS_ACTION_DELETE	0x2

/* The MFS-MBR */
PACKED_TYPESTRUCT(BootRecord, {
	uint8_t					JumpCode[3];

	// Header
	uint32_t				Magic;
	uint8_t					Version;
	uint8_t					Flags;

	// Disk Information
	uint8_t					MediaType;
	uint16_t				SectorSize;
	uint16_t				SectorsPerTrack;
	uint16_t				HeadsPerCylinder;
	uint64_t				SectorCount;
	
	// MFS Information
	uint16_t				ReservedSectors;
	uint16_t				SectorsPerBucket;
	uint64_t				MasterRecordSector;
	uint64_t				MasterRecordMirror;

	uint8_t					BootCode[460];	//512 - 48
});

/* MFS Magic Value 
 * The signature value that must be present in BootRecord::Magic */
#define MFS_BOOTRECORD_MAGIC			0x3153464D // 1SFM

/* MFS Boot-Record flags
 * The possible values that can be present in BootRecord::Flags */
#define MFS_BOOTRECORD_SYSDRIVE			0x1
#define MFS_BOOTRECORD_DIRTY			0x2
#define MFS_BOOTRECORD_ENCRYPTED		0x4

/* The master-record structure
 * Exists two places on disk to have a backup
 * and it contains extended information related
 * to the mfs-partition */
PACKED_TYPESTRUCT(MasterRecord, {
	uint32_t				Magic;
	uint32_t				Flags;
	uint32_t				Checksum;		// Checksum of the master-record
	uint8_t					PartitionName[64];

	uint32_t				FreeBucket;		// Pointer to first free index
	uint32_t				RootIndex;		// Pointer to root directory
	uint32_t				BadBucketIndex;	// Pointer to list of bad buckets
	uint32_t				JournalIndex;	// Pointer to journal file

	uint64_t				MapSector;		// Start sector of bucket-map
	uint64_t				MapSize;		// Size of bucket map
});

/* The bucket-map record
 * A map entry consists of the length of the bucket, and it's link
 * To get the length of the link, you must lookup it's length by accessing Map[Link]
 * Length of bucket 0 is HIDWORD(Map[0]), Link of bucket 0 is LODWORD(Map[0])
 * If the link equals 0xFFFFFFFF there is no link */
PACKED_TYPESTRUCT(MapRecord, {
	uint32_t				Link;
	uint32_t				Length;
});

/* The file-time structure
 * Keeps track of the last time records were modified */
PACKED_TYPESTRUCT(DateTimeRecord, {
	// Date Information
	uint16_t				Year;
	uint8_t					Month;
	uint8_t					Day;

	// Time Information
	uint8_t					Hour;
	uint8_t					Minute;
	uint8_t					Second;
	uint8_t					MilliSeconds; // In the interval of 4 (20 = 80 milliseconds, 249 = 996 milliseconds)
});

/* The file-versioning structure
 * Contains a copy of a file somewhere in time (36 Bytes) */
PACKED_TYPESTRUCT(VersionRecord, {
	DateTimeRecord_t		Timestamp;		// 0x00 - Timestamp of this version
	uint32_t				StartBucket;	// 0x08 - First data bucket
	uint32_t				StartLength;	// 0x0C - Length of first data bucket
	uint64_t				Size;			// 0x10 - Size of data (Set size if sparse)
	uint64_t				AllocatedSize;	// 0x18 - Actual size allocated
	uint32_t				SparseMap;		// 0x20 - Bucket of sparse-map
});

/* The file-record structure
 * Describes a record contained in a directory 
 * which can consist of multiple types, with the common types being
 * both directories and files, and file-links */
PACKED_TYPESTRUCT(FileRecord, {
	uint32_t				Flags;				// 0x00 - Record Flags
	uint32_t				StartBucket;		// 0x04 - First data bucket
	uint32_t				StartLength;		// 0x08 - Length of first data bucket
	uint32_t				RecordChecksum;		// 0x0C - Checksum of record excluding this entry + inline data
	uint64_t				DataChecksum;		// 0x10 - Checksum of data

	// DateTime Records (8 bytes each, 64 bit)
	DateTimeRecord_t		CreatedAt;			// 0x18 - Created timestamp
	DateTimeRecord_t		ModifiedAt;			// 0x20 - Last modified timestamp
	DateTimeRecord_t		AccessedAt;			// 0x28 - Last accessed timestamp
	
	uint64_t				Size;				// 0x30 - Size of data (Set size if sparse)
	uint64_t				AllocatedSize;		// 0x38 - Actual size allocated
	uint32_t				SparseMap;			// 0x40 - Bucket of sparse-map

	uint8_t					Name[300];			// 0x44 - Record name (150 UTF16)
	
	// Versioning Support
	VersionRecord_t			Versions[4];		// 0x170 - Record Versions

	// Inline Data Support
	uint8_t					Integrated[512];	// 0x200
});

/* MFS FileRecord-Flags Definitions
 * Contains constants and bitfield definitions for FileRecord::Flags */
#define MFS_FILERECORD_FILE				0x0		
#define MFS_FILERECORD_DIRECTORY		0x1
#define MFS_FILERECORD_LINK				0x2
#define MFS_FILERECORD_RESERVED			0x3
#define MFS_FILERECORD_TYPE(Flags)		(Flags & 0x3)

#define MFS_FILERECORD_SECURITY			0x4			// User must possess the right key to unlock
#define MFS_FILERECORD_SYSTEM			0x8			// Readable, nothing else
#define MFS_FILERECORD_HIDDEN			0x10		// Don't show
#define MFS_FILERECORD_CHAINED			0x20		// Means all buckets are adjacent
#define MFS_FILERECORD_LOCKED			0x40		// File is deep-locked

#define MFS_FILERECORD_VERSIONED		0x10000000	// Record is versioned
#define MFS_FILERECORD_INLINE			0x20000000	// Inline data is present
#define MFS_FILERECORD_SPARSE			0x40000000  // Record-sparse map is in use
#define MFS_FILERECORD_INUSE			0x80000000	// Record is in use

/* The in-memory version of the file-record
 * Describes which data we cache of files and the position
 * the record is in it's parent directory. */
PACKED_TYPESTRUCT(MfsFile, {
	MString_t				*Name;
	uint32_t				 Flags;
							 
	uint32_t				 StartBucket;
	uint32_t				 StartLength;
							 
	uint64_t				 Size;
	uint64_t				 AllocatedSize;
							 
	uint32_t				 DirectoryBucket;
	size_t					 DirectoryIndex;
});

/* Mfs File Instance 
 * */
typedef struct _MfsFileInstance {
	uint32_t DataBucketPosition;
	uint32_t DataBucketLength;

	// Variable bucket sizes
	// is a pain in the butt
	uint64_t BucketByteBoundary;
} MfsFileInstance_t;

/* Mfs Instance data
 * Keeps track of the current state of an instance of
 * the mollenos-filesystem and keeps cached data as well */
typedef struct _MfsInstance {
	Flags_t					 Flags;
	int						 Version;
	size_t					 SectorsPerBucket;
	BufferObject_t			*TransferBuffer;
	
	uint64_t				 MasterRecordSector;
	uint64_t				 MasterRecordMirrorSector;
							 
	uint64_t				 BucketCount;
	size_t					 BucketsPerSectorInMap;

	// Cached map
	uint32_t				*BucketMap;

	// Keep a cached copy of master-record
	MasterRecord_t			 MasterRecord;
} MfsInstance_t;

/* MfsReadSectors 
 * A wrapper for reading sectors from the disk associated
 * with the file-system descriptor */
__EXTERN
OsStatus_t
MfsReadSectors(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ BufferObject_t *Buffer,
	_In_ uint64_t Sector,
	_In_ size_t Count);

/* MfsWriteSectors 
 * A wrapper for writing sectors to the disk associated
 * with the file-system descriptor */
__EXTERN
OsStatus_t
MfsWriteSectors(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ BufferObject_t *Buffer,
	_In_ uint64_t Sector,
	_In_ size_t Count);

/* MfsGetBucketLink
 * Looks up the next bucket link by utilizing the cached
 * in-memory version of the bucketmap */
__EXTERN
OsStatus_t
MfsGetBucketLink(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ uint32_t Bucket,
	_Out_ MapRecord_t *Link);

/* MfsSetBucketLink
 * Updates the next link for the given bucket and flushes
 * the changes to disk */
__EXTERN
OsStatus_t
MfsSetBucketLink(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ uint32_t Bucket,
	_In_ MapRecord_t *Link,
	_In_ int UpdateLength);

/* MfsZeroBucket
 * Wipes the given bucket and count with zero values
 * useful for clearing clusters of sectors */
__EXTERN
OsStatus_t
MfsZeroBucket(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ uint32_t Bucket,
	_In_ size_t Count);

/* MfsAllocateBuckets
 * Allocates the number of requested buckets in the bucket-map
 * if the allocation could not be done, it'll return OsError */
__EXTERN
OsStatus_t
MfsAllocateBuckets(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ size_t BucketCount,
	_Out_ MapRecord_t *RecordResult);

/* MfsFreeBuckets
 * Frees an entire chain of buckets that has been allocated for 
 * a file-record */
__EXTERN
OsStatus_t
MfsFreeBuckets(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ uint32_t StartBucket,
	_In_ uint32_t StartLength);

/* MfsUpdateRecord
 * Conveniance function for updating a given file on
 * the disk, not data related to file, but the metadata */
__EXTERN
FileSystemCode_t
MfsUpdateRecord(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ MfsFile_t *Handle,
	_In_ int Action);

/* MfsLocateRecord
 * Locates a given file-record by the path given, all sub
 * entries must be directories. File is only allocated and set
 * if the function returns FsOk */
__EXTERN
FileSystemCode_t
MfsLocateRecord(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ uint32_t BucketOfDirectory,
	_In_ MString_t *Path,
	_Out_ MfsFile_t **File);

/* MfsLocateFreeRecord
 * Very alike to the MfsLocateRecord
 * except instead of locating a file entry
 * it locates a free entry in the last token of
 * the path, and validates the path as it goes */
__EXTERN
FileSystemCode_t
MfsLocateFreeRecord(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ uint32_t BucketOfDirectory,
	_In_ MString_t *Path,
	_Out_ MfsFile_t **File);

/* MfsCreateRecord
 * Creates a new file-record in a directory
 * It internally calls MfsLocateFreeRecord to
 * find a viable entry and validate the path */
__EXTERN
FileSystemCode_t
MfsCreateRecord(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ uint32_t BucketOfDirectory,
	_In_ MString_t *Path,
	_In_ Flags_t Flags,
	_Out_ MfsFile_t **File);

#endif //!_MFS_H_
