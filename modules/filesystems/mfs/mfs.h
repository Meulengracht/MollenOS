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
 *  - Missing implementations:
 *    - Journaling
 *    - Encryptiong (AES)
 *      - Versioning
 *      - Inline-data
 *      - Switch to B+ trees for metadata
 */

#ifndef _MFS_H_
#define _MFS_H_

#include <ddk/filesystem.h>
#include <os/mollenos.h>
#include <os/dmabuf.h>
#include <ds/mstring.h>

/**
 * MFS Definitions and Utilities
 * Contains magic constant values and utility macros for conversion
 */
#define MFS_ENDOFCHAIN                0xFFFFFFFF
#define MFS_SECTORCOUNT(mfs, bucket) ((mfs)->SectorsPerBucket * (bucket))
#define MFS_GETSECTOR(mfs, bucket)   ((mfs)->ReservedSectorCount + MFS_SECTORCOUNT(mfs, bucket))
#define MFS_ROOTSIZE                  8
#define MFS_DIRECTORYEXPANSION        4

#define MFS_ACTION_NONE     0x0
#define MFS_ACTION_UPDATE   0x1
#define MFS_ACTION_CREATE   0x2
#define MFS_ACTION_DELETE   0x3

PACKED_TYPESTRUCT(BootRecord, {
    uint8_t  JumpCode[3];

    // Header
    uint32_t Magic;
    uint8_t  Version;
    uint8_t  Flags;

    // Disk Information
    uint8_t  MediaType;
    uint16_t SectorSize;
    uint16_t SectorsPerTrack;
    uint16_t HeadsPerCylinder;
    uint64_t SectorCount;
    
    // MFS Information
    uint16_t ReservedSectors;
    uint16_t SectorsPerBucket;
    uint64_t MasterRecordSector;
    uint64_t MasterRecordMirror;

    uint8_t  BootCode[468];    //512 - 44
});

/**
 * MFS Magic Value 
 * The signature value that must be present in BootRecord::Magic
 */
#define MFS_BOOTRECORD_MAGIC 0x3153464D // 1SFM

/**
 * MFS Boot-Record flags
 * The possible values that can be present in BootRecord::Flags
 */
#define MFS_BOOTRECORD_SYSDRIVE  0x1  // Boot Partition
#define MFS_BOOTRECORD_ENCRYPTED 0x2  // Partition is protected by encryption

/**
 * The master-record structure
 * Exists two places on disk to have a backup
 * and it contains extended information related to the mfs-partition
 */
PACKED_TYPESTRUCT(MasterRecord, {
    uint32_t Magic;
    uint32_t Flags;
    uint32_t Checksum;           // Checksum of the master-record
    uint8_t  PartitionName[64];

    uint32_t FreeBucket;         // Pointer to first free index
    uint32_t RootIndex;          // Pointer to root directory
    uint32_t BadBucketIndex;     // Pointer to list of bad buckets
    uint32_t JournalIndex;       // Pointer to journal file

    uint64_t MapSector;          // Start sector of bucket-map
    uint64_t MapSize;            // Size of bucket map
});

#define MFS_MASTERRECORD_SYSTEM_DRIVE 0x1  // Partition is system drive
#define MFS_MASTERRECORD_DATA_DRIVE   0x2  // Partition is system data drive
#define MFS_MASTERRECORD_USER_DRIVE   0x4  // Partition is system user drive
#define MFS_MASTERRECORD_HIDDEN       0x8  // Partition is marked as hidden
#define MFS_MASTERRECORD_DIRTY        0x10 // Journal must be checked on startup

/* The bucket-map record
 * A map entry consists of the length of the bucket, and it's link
 * To get the length of the link, you must lookup it's length by accessing Map[Link]
 * Length of bucket 0 is HIDWORD(Map[0]), Link of bucket 0 is LODWORD(Map[0])
 * If the link equals 0xFFFFFFFF there is no link */
PACKED_TYPESTRUCT(MapRecord, {
    uint32_t Link;
    uint32_t Length;
});

/* The file-time structure
 * Keeps track of the last time records were modified */
PACKED_TYPESTRUCT(DateTimeRecord, {
    // Date Information
    uint16_t Year;
    uint8_t  Month;
    uint8_t  Day;

    // Time Information
    uint8_t  Hour;
    uint8_t  Minute;
    uint8_t  Second;
    uint8_t  MilliSeconds; // In the interval of 4 (20 = 80 milliseconds, 249 = 996 milliseconds)
});

/* The file-versioning structure
 * Contains a copy of a file somewhere in time (36 Bytes) */
PACKED_TYPESTRUCT(VersionRecord, {
    DateTimeRecord_t Timestamp;      // 0x00 - Timestamp of this version
    uint32_t         StartBucket;    // 0x08 - First data bucket
    uint32_t         StartLength;    // 0x0C - Length of first data bucket
    uint64_t         Size;           // 0x10 - Size of data (Set size if sparse)
    uint64_t         AllocatedSize;  // 0x18 - Actual size allocated
    uint32_t         SparseMap;      // 0x20 - Bucket of sparse-map
});

/* The file-record structure
 * Describes a record contained in a directory which can consist of multiple types, 
 * with the common types being both directories and files, and file-links */
PACKED_TYPESTRUCT(FileRecord, {
    uint32_t         Flags;                // 0x00 - Record Flags
    uint32_t         StartBucket;        // 0x04 - First data bucket
    uint32_t         StartLength;        // 0x08 - Length of first data bucket
    uint32_t         RecordChecksum;        // 0x0C - Checksum of record excluding this entry + inline data
    uint64_t         DataChecksum;        // 0x10 - Checksum of data

    // DateTime Records (8 bytes each, 64 bit)
    DateTimeRecord_t CreatedAt;            // 0x18 - Created timestamp
    DateTimeRecord_t ModifiedAt;            // 0x20 - Last modified timestamp
    DateTimeRecord_t AccessedAt;            // 0x28 - Last accessed timestamp
    
    uint64_t         Size;                // 0x30 - Size of data (Set size if sparse)
    uint64_t         AllocatedSize;        // 0x38 - Actual size allocated
    uint32_t         SparseMap;            // 0x40 - Bucket of sparse-map

    uint8_t          Name[300];            // 0x44 - Record name (150 UTF16)
    
    // Versioning Support
    VersionRecord_t  Versions[4];        // 0x170 - Record Versions

    // Inline Data Support
    uint8_t          Integrated[512];    // 0x200
});

#define MFS_FILERECORD_MAX_NAME 300

/* MFS FileRecord-Flags Definitions
 * Contains constants and bitfield definitions for FileRecord::Flags */
#define MFS_FILERECORD_FILE             0x0        
#define MFS_FILERECORD_DIRECTORY        0x1
#define MFS_FILERECORD_LINK             0x2
#define MFS_FILERECORD_RESERVED         0x3
#define MFS_FILERECORD_TYPE(Flags)      ((Flags) & 0x3)

#define MFS_FILERECORD_SECURITY         0x4         // User must possess the right key to unlock
#define MFS_FILERECORD_SYSTEM           0x8         // Readable, nothing else
#define MFS_FILERECORD_HIDDEN           0x10        // Don't show
#define MFS_FILERECORD_CHAINED          0x20        // Means all buckets are adjacent
#define MFS_FILERECORD_LOCKED           0x40        // File is deep-locked

#define MFS_FILERECORD_VERSIONED        0x10000000  // Record is versioned
#define MFS_FILERECORD_INLINE           0x20000000  // Inline data is present
#define MFS_FILERECORD_SPARSE           0x40000000  // Record-sparse map is in use
#define MFS_FILERECORD_INUSE            0x80000000  // Record is in use

typedef struct MFSEntry {
    mstring_t* Name;
    uint32_t   Owner;
    uint32_t   Permissions;
    uint32_t   Flags;

    uint32_t   NativeFlags;
    int        ActionOnClose;

    // How many bytes are actually allocated, not the number of
    // valid bytes for the file
    uint64_t AllocatedSize;
    uint64_t ActualSize;

    // The initial bucket and length of that bucket
    uint32_t StartBucket;
    uint32_t StartLength;

    // The bucket of the directory where this file resides,
    // and the length of the bucket. We also store the file
    // descriptor index into this bucket.
    uint32_t DirectoryBucket;
    uint32_t DirectoryLength;
    size_t   DirectoryIndex;

    // Current position for this file handle. We store the bucket
    // and the length of that bucket.
    uint64_t Position;
    uint32_t DataBucketPosition;
    uint32_t DataBucketLength;
    uint64_t BucketByteBoundary;  // Support variadic bucket sizes
} MFSEntry_t;

typedef struct FileSystemMFS {
    mstring_t*                  Label;
    unsigned int                Flags;
    int                         Version;
    size_t                      SectorsPerBucket;
    DMAAttachment_t             TransferBuffer;
    struct VFSStorageParameters Storage;
    size_t                      SectorSize;
    uint16_t                    ReservedSectorCount;
    uint64_t                    MasterRecordSector;
    uint64_t                    MasterRecordMirrorSector;
    size_t                      BucketsPerSectorInMap;
    size_t                      BucketsInMap;

    // Cached resources
    uint32_t*      BucketMap;
    MasterRecord_t MasterRecord;
    MFSEntry_t     RootEntry;
} FileSystemMFS_t;

/* MfsGetBucketLink
 * Looks up the next bucket link by utilizing the cached
 * in-memory version of the bucketmap */
extern oserr_t
MfsGetBucketLink(
        _In_ FileSystemMFS_t* mfs,
        _In_ uint32_t         bucket,
        _In_ MapRecord_t*     link);

/* MfsSetBucketLink
 * Updates the next link for the given bucket and flushes
 * the changes to disk */
extern oserr_t
MfsSetBucketLink(
        _In_ FileSystemMFS_t* mfs,
        _In_ uint32_t         bucket,
        _In_ MapRecord_t*     link,
        _In_ int              updateLength);

/* MFSAdvanceToNextBucket
 * Retrieves the next bucket link, marks it active and updates the file-instance. Returns OsNotExists
 * when end-of-chain. */
extern oserr_t
MFSAdvanceToNextBucket(
        _In_ FileSystemMFS_t* mfs,
        _In_ MFSEntry_t*      entry,
        _In_ size_t           bucketSizeBytes);

/* MfsZeroBucket
 * Wipes the given bucket and count with zero values
 * useful for clearing clusters of sectors */
extern oserr_t
MfsZeroBucket(
        _In_ FileSystemMFS_t* mfs,
        _In_ uint32_t         bucket,
        _In_ size_t           count);

/* MfsAllocateBuckets
 * Allocates the number of requested buckets in the bucket-map
 * if the allocation could not be done, it'll return OsError */
extern oserr_t
MfsAllocateBuckets(
        _In_  FileSystemMFS_t* mfs,
        _In_  size_t           bucketCount,
        _Out_ MapRecord_t*     mapRecord);

/* MfsFreeBucketsMfsFreeBuckets
 * Frees an entire chain of buckets that has been allocated for 
 * a file-record */
extern oserr_t
MfsFreeBuckets(
        _In_ FileSystemMFS_t* mfs,
        _In_ uint32_t         startBucket,
        _In_ uint32_t         startLength);

/**
 * @brief
 * @param mfs
 * @param sourceBucket
 * @param sourceLength
 * @param destinationBucket
 * @param destinationLength
 * @return
 */
extern oserr_t
MFSCloneBucketData(
        _In_ FileSystemMFS_t* mfs,
        _In_ uint32_t         sourceBucket,
        _In_ uint32_t         sourceLength,
        _In_ uint32_t         destinationBucket,
        _In_ uint32_t         destinationLength);

/* MfsEnsureRecordSpace
 * Ensures that the given record has the space neccessary for the required data. */
extern oserr_t
MfsEnsureRecordSpace(
        _In_ FileSystemMFS_t* mfs,
        _In_ MFSEntry_t*      entry,
        _In_ uint64_t         spaceRequired);

/* MfsUpdateRecord
 * Conveniance function for updating a given file on
 * the disk, not data related to file, but the metadata */
extern oserr_t
MfsUpdateRecord(
        _In_ FileSystemMFS_t* mfs,
        _In_ MFSEntry_t*      entry,
        _In_ int              action);

/**
 * @brief Locates a path, starting from the given directory.
 * @param mfs
 * @param directory
 * @param path
 * @param entryOut
 * @return
 */
extern oserr_t
MfsLocateRecord(
        _In_  FileSystemMFS_t* mfs,
        _In_  MFSEntry_t*      directory,
        _In_  mstring_t*       path,
        _Out_ MFSEntry_t**     entryOut);

/**
 * @brief
 * @param vfsCommonData
 * @param entry
 * @param name
 * @param owner
 * @param flags
 * @param permissions
 * @param entryOut
 * @return
 */
extern oserr_t
MfsCreateRecord(
        _In_  FileSystemMFS_t* mfs,
        _In_  MFSEntry_t*      entry,
        _In_  mstring_t*       name,
        _In_  uint32_t         owner,
        _In_  uint32_t         flags,
        _In_  uint32_t         permissions,
        _In_  uint64_t         allocatedSize,
        _In_  uint64_t         size,
        _In_  uint32_t         startBucket,
        _In_  uint32_t         startLength,
        _Out_ MFSEntry_t**     entryOut);

/**
 * @brief Converts the generic vfs options/permissions to the native mfs representation.
 * @param flags
 * @return
 */
extern unsigned int
MFSToNativeFlags(
    _In_ unsigned int flags);

/**
 * @brief Converts the native MFS file flags into the generic vfs options/permissions.
 * @param fileRecord
 * @param flags
 * @param permissions
 */
extern void
MFSFromNativeFlags(
    _In_  FileRecord_t* fileRecord,
    _Out_ unsigned int* flags,
    _Out_ unsigned int* permissions);

/* MfsFileRecordToVfsFile
 * Converts a native MFS file record into the generic vfs representation. */
extern void
MfsFileRecordToVfsFile(
        _In_ FileSystemMFS_t* mfs,
        _In_ FileRecord_t*    nativeEntry,
        _In_ MFSEntry_t*      mfsEntry);

#endif //!_MFS_H_
