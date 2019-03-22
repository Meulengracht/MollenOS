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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
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

typedef enum {
    FsOk,
    FsDeleted,
    FsInvalidParameters,
    FsPathNotFound,
    FsAccessDenied,
    FsPathIsNotDirectory,
    FsPathExists,
    FsDiskError
} FileSystemCode_t;

typedef struct {
    long           Id;
    Flags_t        Flags;
    size_t         MaxFilenameLength;
    char           SerialNumber[32];
    unsigned long  BlockSize;
    unsigned long  BlocksPerSegment;
    LargeInteger_t SegmentsTotal;
    LargeInteger_t SegmentsFree;
} OsFileSystemDescriptor_t;

typedef struct {
    long            Id;
    long            StorageId;
    Flags_t         Flags;
    Flags_t         Permissions;
    LargeUInteger_t Size;
    struct timespec CreatedAt;
    struct timespec ModifiedAt;
    struct timespec AccessedAt;
} OsFileDescriptor_t;

// OsFileDescriptor_t::Flags
#define FILE_FLAG_FILE          0x00000000
#define FILE_FLAG_DIRECTORY     0x00000001
#define FILE_FLAG_LINK          0x00000002

// OsFileDescriptor_t::Permissions
#define FILE_PERMISSION_READ    0x00000001
#define FILE_PERMISSION_WRITE   0x00000002
#define FILE_PERMISSION_EXECUTE 0x00000004

#endif //!__TYPES_FILE_H__
