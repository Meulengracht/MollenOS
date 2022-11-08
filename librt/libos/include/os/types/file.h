/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * File Type Definitions & Structures
 * - This header describes the base file-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __TYPES_FILE_H__
#define __TYPES_FILE_H__

#include <os/osdefs.h>
#include <time.h>

typedef struct OsDirectoryEntry {
    // ID is the file node ID. This is not consistent across boots, and is
    // assigned during when first enumerated on a fresh boot.
    uuid_t ID;
    // Name is guaranteed to be able to hold a name of NAME_MAX set in os/osdefs.h.
    const char* Name;
    // Index into the directory. This can be used in conjunction with
    // seekdir()
    long Index;
    // Flags match the field OsFileDescriptor_t::Flags and describe
    // the type of directory entry.
    unsigned int Flags;
} OsDirectoryEntry_t;

typedef struct OsFileSystemDescriptor {
    long          Id;
    unsigned int  Flags;
    size_t        MaxFilenameLength;
    char          SerialNumber[32];
    unsigned long BlockSize;
    unsigned long BlocksPerSegment;
    UInteger64_t  SegmentsTotal;
    UInteger64_t  SegmentsFree;
} OsFileSystemDescriptor_t;

typedef struct OsFileDescriptor {
    long            Id;
    long            StorageId;
    unsigned int    Flags;
    unsigned int    Permissions;
    UInteger64_t    Size;
    struct timespec CreatedAt;
    struct timespec ModifiedAt;
    struct timespec AccessedAt;
} OsFileDescriptor_t;

// OsFileDescriptor_t::Flags
#define FILE_FLAG_FILE          0x00000000
#define FILE_FLAG_DIRECTORY     0x00000001
#define FILE_FLAG_LINK          0x00000002
#define FILE_FLAG_TYPE(Flags)   ((Flags) & 0x00000003)

// OsFileDescriptor_t::Permissions
#define FILE_PERMISSION_OWNER_READ    0x0001
#define FILE_PERMISSION_OWNER_WRITE   0x0002
#define FILE_PERMISSION_OWNER_EXECUTE 0x0004
#define FILE_PERMISSION_GROUP_READ    0x0008
#define FILE_PERMISSION_GROUP_WRITE   0x0010
#define FILE_PERMISSION_GROUP_EXECUTE 0x0020
#define FILE_PERMISSION_OTHER_READ    0x0040
#define FILE_PERMISSION_OTHER_WRITE   0x0080
#define FILE_PERMISSION_OTHER_EXECUTE 0x0100

#define FILE_PERMISSION_READ    (FILE_PERMISSION_OWNER_READ    | FILE_PERMISSION_GROUP_READ    | FILE_PERMISSION_OTHER_READ)
#define FILE_PERMISSION_WRITE   (FILE_PERMISSION_OWNER_WRITE   | FILE_PERMISSION_GROUP_WRITE   | FILE_PERMISSION_OTHER_WRITE)
#define FILE_PERMISSION_EXECUTE (FILE_PERMISSION_OWNER_EXECUTE | FILE_PERMISSION_GROUP_EXECUTE | FILE_PERMISSION_OTHER_EXECUTE)

PACKED_TYPESTRUCT(FileMappingParameters, {
    uuid_t       MemoryHandle;
    unsigned int Flags;
    uint64_t     FileOffset;
    uintptr_t    VirtualAddress;
    size_t       Length;
});

// Options flag
#define __FILE_CREATE                           0x00000001
#define __FILE_TRUNCATE                         0x00000002
#define __FILE_FAILONEXIST                      0x00000004
#define __FILE_APPEND                           0x00000100
#define __FILE_BINARY                           0x00000200
#define __FILE_VOLATILE                         0x00000400
#define __FILE_TEMPORARY                        0x00000800
#define __FILE_DIRECTORY                        0x00001000
#define __FILE_LINK                             0x00002000

// Access flags
#define __FILE_READ_ACCESS                      0x00000010
#define __FILE_WRITE_ACCESS                     0x00000020
#define __FILE_READ_SHARE                       0x00000040
#define __FILE_WRITE_SHARE                      0x00000080

#endif //!__TYPES_FILE_H__
