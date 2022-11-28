/**
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
 */

#ifndef __DDK_FILESYSTEM_H__
#define __DDK_FILESYSTEM_H__

#include <ddk/storage.h>
#include <ds/list.h>
#include <ds/mstring.h>
#include <os/mollenos.h>
#include <os/types/time.h>

enum VFSStorageType {
    VFSSTORAGE_TYPE_DEVICE,
    VFSSTORAGE_TYPE_FILE,
    VFSSTORAGE_TYPE_EPHEMERAL,
};

/**
 * @brief The data in VFSStorageParameters will be supplied once upon initialization
 * of the filesystem. It is expected that the FS itself stores these parameters to
 * use for storage operations.
 */
struct VFSStorageParameters {
    unsigned int StorageType;
    union {
        struct {
            uuid_t HandleID;
            // size_t SectorSize;
        } File;
        struct {
            uuid_t DriverID;
            uuid_t DeviceID;
        } Device;
    } Storage;
    unsigned int Flags;
    UInteger64_t SectorStart;
};

struct VFSDirectoryEntry {
    // NameLength describes the data length of the entry's name. The name
    // is encoded as UTF-8 bytes.
    uint32_t NameLength;
    // LinkLength describes the data length of the entry's link target. The
    // string is encoded as UTF-8 bytes.
    uint32_t LinkLength;
    uint32_t UserID;
    uint32_t GroupID;
    uint64_t Size;
    uint64_t SizeOnDisk;
    uint32_t Permissions; // Permissions come from os/file/types.h
    uint32_t Flags;       // Flags come from os/file/types.h
    OSTimestamp_t Accessed;
    OSTimestamp_t Modified;
    OSTimestamp_t Created;
};

struct VFSStat {
    // These are filled in by the VFS
    uuid_t ID;
    uuid_t StorageID;

    mstring_t* Name;
    mstring_t* LinkTarget;
    uint32_t   Owner;
    uint32_t   Permissions; // Permissions come from os/file/types.h
    uint32_t   Flags;       // Flags come from os/file/types.h
    uint64_t   Size;
    uint32_t   Links;

    OSTimestamp_t Accessed;
    OSTimestamp_t Modified;
    OSTimestamp_t Created;
};

struct VFSStatFS {
    // These are filled in by the VFS
    uuid_t     ID;
    mstring_t* Serial;

    // These should be filled in by the underlying FS.
    mstring_t* Label;
    uint32_t   MaxFilenameLength;
    uint32_t   BlockSize;
    uint32_t   BlocksPerSegment;
    uint64_t   SegmentsTotal;
    uint64_t   SegmentsFree;
};

#endif //!__DDK_FILESYSTEM_H__
