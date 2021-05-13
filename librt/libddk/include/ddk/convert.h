/**
 * MollenOS
 *
 * Copyright 2021, Philip Meulengracht
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
 * Gracht types conversion methods
 * Implements conversion methods to convert between formats used in gracht services
 * and OS types.
 */

#ifndef __DDK_CONVERT_H__
#define __DDK_CONVERT_H__

#include <os/mollenos.h>
#include <os/process.h>

#include <sys_device_service.h>
#include <sys_file_service.h>
#include <sys_process_service.h>

static void from_sys_timestamp(struct sys_timestamp* in, struct timespec* out)
{
    out->tv_sec = in->tv_sec;
    out->tv_nsec = in->tv_nsec;
}

static void from_sys_disk_descriptor(struct sys_disk_descriptor* in, OsStorageDescriptor_t* out)
{
    size_t len = strnlen(in->serial, sizeof(out->SerialNumber) - 1);

    out->Id = 0;
    out->Flags = (unsigned int)in->flags;
    out->SectorSize = in->geometry.sector_size;
    out->SectorsTotal.QuadPart = in->geometry.sectors_total;

    memcpy(&out->SerialNumber[0], in->serial, len);
    out->SerialNumber[len] = 0;
}

static void from_sys_filesystem_descriptor(struct sys_filesystem_descriptor* in, OsFileSystemDescriptor_t* out)
{
    size_t len = strnlen(in->serial, sizeof(out->SerialNumber) - 1);

    out->Id = in->id;
    out->Flags = in->flags;
    out->BlockSize = in->block_size;
    out->BlocksPerSegment = in->blocks_per_segment;
    out->MaxFilenameLength = in->max_filename_length;
    out->SegmentsFree.QuadPart = in->segments_free;
    out->SegmentsTotal.QuadPart = in->segments_total;

    memcpy(&out->SerialNumber[0], in->serial, len);
    out->SerialNumber[len] = 0;
}

static void from_sys_file_descriptor(struct sys_file_descriptor* in, OsFileDescriptor_t* out)
{
    out->Id = in->id;
    out->StorageId = in->storageId;
    out->Flags = (unsigned int)in->flags;
    out->Permissions = (unsigned int)in->permissions;
    out->Size.QuadPart = in->size;

    from_sys_timestamp(&in->created, &out->CreatedAt);
    from_sys_timestamp(&in->accessed, &out->AccessedAt);
    from_sys_timestamp(&in->modified, &out->ModifiedAt);
}

static void to_sys_process_configuration(ProcessConfiguration_t* in, struct sys_process_configuration* out)
{
    out->inherit_flags = in->InheritFlags;
    out->memory_limit  = in->MemoryLimit;
    out->stdout_handle = in->StdOutHandle;
    out->stderr_handle = in->StdErrHandle;
    out->stdin_handle  = in->StdInHandle;
}

#endif //!__DDK_CONVERT_H__
