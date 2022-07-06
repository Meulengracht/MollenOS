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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Gracht types conversion methods
 * Implements conversion methods to convert between formats used in gracht services
 * and OS types.
 */

#ifndef __DDK_CONVERT_H__
#define __DDK_CONVERT_H__

#include <ddk/storage.h>

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

static void to_sys_timestamp(struct timespec* in, struct sys_timestamp* out)
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

static void from_sys_disk_descriptor_dkk(struct sys_disk_descriptor* in, StorageDescriptor_t* out)
{
    size_t serialLen = strnlen(in->serial, sizeof(out->Serial) - 1);
    size_t modelLen  = strnlen(in->model, sizeof(out->Model) - 1);

    out->DeviceID = in->device_id;
    out->DriverID = in->driver_id;
    out->Flags = (unsigned int)in->flags;
    out->SectorSize = in->geometry.sector_size;
    out->SectorCount = in->geometry.sectors_total;
    out->LUNCount = in->geometry.lun_count;
    out->SectorsPerCylinder = in->geometry.sectors_per_cylinder;

    memcpy(&out->Serial[0], in->serial, serialLen);
    out->Serial[serialLen] = 0;
    memcpy(&out->Model[0], in->model, modelLen);
    out->Model[modelLen] = 0;
}

static void to_sys_disk_descriptor_dkk(StorageDescriptor_t* in, struct sys_disk_descriptor* out)
{
    out->device_id = in->DeviceID;
    out->driver_id = in->DriverID;
    out->flags = (enum sys_storage_flags)in->Flags;
    out->geometry.sector_size = in->SectorSize;
    out->geometry.sectors_total = in->SectorCount;
    out->geometry.lun_count = in->LUNCount;
    out->geometry.sectors_per_cylinder = in->SectorsPerCylinder;
    out->serial = &in->Serial[0];
    out->model = &in->Model[0];
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

static void to_sys_file_descriptor(OsFileDescriptor_t* in, struct sys_file_descriptor* out)
{
    out->id = in->Id;
    out->storageId = in->StorageId;
    out->flags = (enum sys_file_flags)in->Flags;
    out->permissions = (enum sys_file_permissions)in->Permissions;
    out->size = in->Size.QuadPart;

    to_sys_timestamp(&in->CreatedAt, &out->created);
    to_sys_timestamp(&in->AccessedAt, &out->accessed);
    to_sys_timestamp(&in->ModifiedAt, &out->modified);
}

static void from_sys_process_configuration(const struct sys_process_configuration* in, ProcessConfiguration_t* out)
{
    out->InheritFlags = in->inherit_flags;
    out->MemoryLimit = in->memory_limit;
    out->StdOutHandle = in->stdout_handle;
    out->StdErrHandle = in->stderr_handle;
    out->StdInHandle = in->stdin_handle;
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
