/**
 * Copyright 2022, Philip Meulengracht
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

//#define __TRACE

#include <ddk/convert.h>
#include <ddk/utils.h>
#include <fs/common.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <internal/_storage.h>
#include <os/services/process.h>
#include <os/services/file.h>
#include <stdio.h>

#include <ctt_storage_service_client.h>
#include <sys_file_service_client.h>

static oserr_t
__StatDevice(
        _In_ uuid_t               driverID,
        _In_ uuid_t               deviceID,
        _In_ StorageDescriptor_t* stats)
{
    struct vali_link_message   msg  = VALI_MSG_INIT_HANDLE(driverID);
    oserr_t                    osStatus;
    struct sys_disk_descriptor gdescriptor;

    if (ctt_storage_stat(GetGrachtClient(), &msg.base, deviceID) != 0) {
        return OS_EUNKNOWN;
    }

    if (gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC) != 0 ||
        gracht_client_get_status(GetGrachtClient(), &msg.base) != GRACHT_MESSAGE_COMPLETED) {
        gracht_client_abandon(GetGrachtClient(), &msg.base);
        return OS_EUNKNOWN;
    }

    if (ctt_storage_stat_result(GetGrachtClient(), &msg.base, &osStatus, &gdescriptor) != 0) {
        return OS_EPROTOCOL;
    }
    if (osStatus == OS_EOK) {
        from_sys_disk_descriptor_dkk(&gdescriptor, stats);
    }
    return osStatus;
}

static oserr_t
__ReadFile(
    _In_  uuid_t        handleID,
    _In_  uuid_t        buffer,
    _In_  size_t        offset,
    _In_  UInteger64_t* sector,
    _In_  size_t        count,
    _Out_ size_t*       completed)
{
    UInteger64_t position;
    size_t       bytes = 0;
    oserr_t      status;

    // reset the result in any case
    *completed = 0;

    if (sector->QuadPart > UINT64_MAX / 512 || count > SIZE_MAX / 512) {
        return OS_EINVALPARAMS;
    }

    position.QuadPart = sector->QuadPart * 512;
    
    status = OSTransferFileAt(
        handleID,
        &position,
        buffer,
        offset,
        false,
        count * 512,
        &bytes
    );
    *completed = bytes / 512;
    return status;
}

static oserr_t
__WriteFile(
    _In_  uuid_t        handleID,
    _In_  uuid_t        buffer,
    _In_  size_t        offset,
    _In_  UInteger64_t* sector,
    _In_  size_t        count,
    _Out_ size_t*       completed)
{
    UInteger64_t position;
    size_t       bytes = 0;
    oserr_t      status;

    // reset the result in any case
    *completed = 0;

    if (sector->QuadPart > UINT64_MAX / 512 || count > SIZE_MAX / 512) {
        return OS_EINVALPARAMS;
    }

    position.QuadPart = sector->QuadPart * 512;

    status = OSTransferFileAt(
        handleID,
        &position,
        buffer,
        offset,
        true,
        count * 512,
        &bytes
    );
    *completed = bytes / 512;
    return status;
}

static oserr_t
__StatFile(
        _In_ uuid_t               handleID,
        _In_ StorageDescriptor_t* stats)
{
    struct vali_link_message   msg  = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                    osStatus;
    struct sys_file_descriptor fstats;

    if (sys_file_fstat(
            GetGrachtClient(),
            &msg.base,
            OSProcessCurrentID(),
            handleID
    ) != 0) {
        return OS_EUNKNOWN;
    }

    if (gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC) != 0 ||
        gracht_client_get_status(GetGrachtClient(), &msg.base) != GRACHT_MESSAGE_COMPLETED) {
        gracht_client_abandon(GetGrachtClient(), &msg.base);
        return OS_EUNKNOWN;
    }

    if (sys_file_fstat_result(GetGrachtClient(), &msg.base, &osStatus, &fstats) != 0) {
        return OS_EPROTOCOL;
    }
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    // Use the file size and handle to describe the storage.
    stats->DriverID = GetFileService();
    stats->DeviceID = handleID;
    stats->SectorCount = fstats.size / 512;
    stats->SectorSize  = 512; // TODO this can change in the future
    stats->Flags = 0;
    stats->LUNCount = 1;

    // generate model and serial
    snprintf(
            &stats->Serial[0], sizeof(stats->Serial),
            "file-%li", fstats.storageId
    );
    snprintf(
            &stats->Model[0], sizeof(stats->Model),
            "file-mount"
    );

    return osStatus;
}

oserr_t
FSStorageRead(
        _In_  struct VFSStorageParameters* storageParameters,
        _In_  uuid_t                       buffer,
        _In_  size_t                       offset,
        _In_  UInteger64_t*                sector,
        _In_  size_t                       count,
        _Out_ size_t*                      read)
{
    // reset the result in any case
    *read = 0;

    if (sector->QuadPart > UINT64_MAX - storageParameters->SectorStart.QuadPart) {
        return OS_EINVALPARAMS;
    }


    switch (storageParameters->StorageType) {
        case VFSSTORAGE_TYPE_DEVICE: {
            return __StorageTransfer(
                    storageParameters->Storage.Device.DriverID,
                    storageParameters->Storage.Device.DeviceID,
                    buffer,
                    offset,
                    &(UInteger64_t) { .QuadPart = storageParameters->SectorStart.QuadPart + sector->QuadPart },
                    storageParameters->SectorSize, count, false,
                    read
            );
        } break;
        case VFSSTORAGE_TYPE_FILE: {
            return __ReadFile(
                    storageParameters->Storage.File.HandleID,
                    buffer,
                    offset,
                    &(UInteger64_t) { .QuadPart = storageParameters->SectorStart.QuadPart + sector->QuadPart },
                    count,
                    read
            );
        } break;
    }
    return OS_ENOTSUPPORTED;
}

oserr_t
FSStorageWrite(
        _In_  struct VFSStorageParameters* storageParameters,
        _In_  uuid_t                       buffer,
        _In_  size_t                       offset,
        _In_  UInteger64_t*                sector,
        _In_  size_t                       count,
        _Out_ size_t*                      written)
{
    // reset the result in any case
    *written = 0;

    if (sector->QuadPart > UINT64_MAX - storageParameters->SectorStart.QuadPart) {
        return OS_EINVALPARAMS;
    }


    switch (storageParameters->StorageType) {
        case VFSSTORAGE_TYPE_DEVICE: {
            return __StorageTransfer(
                    storageParameters->Storage.Device.DriverID,
                    storageParameters->Storage.Device.DeviceID,
                    buffer,
                    offset,
                    &(UInteger64_t) { .QuadPart = storageParameters->SectorStart.QuadPart + sector->QuadPart },
                    storageParameters->SectorSize, count, true,
                    written
            );
        } break;
        case VFSSTORAGE_TYPE_FILE: {
            return __WriteFile(
                    storageParameters->Storage.File.HandleID,
                    buffer,
                    offset,
                    &(UInteger64_t) { .QuadPart = storageParameters->SectorStart.QuadPart + sector->QuadPart },
                    count,
                    written
            );
        } break;
    }
    return OS_ENOTSUPPORTED;
}

oserr_t
FSStorageStat(
    _InOut_ struct VFSStorageParameters* storageParameters,
    _Out_   StorageDescriptor_t*         stat)
{
    oserr_t status;

    storageParameters->SectorSize = 0;

    switch (storageParameters->StorageType) {
        case VFSSTORAGE_TYPE_DEVICE:
            status = __StatDevice(
                storageParameters->Storage.Device.DriverID,
                storageParameters->Storage.Device.DeviceID,
                stat
            );
            break;
        case VFSSTORAGE_TYPE_FILE:
            status = __StatFile(storageParameters->Storage.File.HandleID, stat);
            break;
        default:
            return OS_ENOTSUPPORTED;
    }
    
    if (status == OS_EOK) {
        if (stat->SectorSize == 0 || stat->SectorSize > UINT32_MAX) {
            return OS_EINVALPARAMS;
        }
        storageParameters->SectorSize = stat->SectorSize;
    }
    return status;
}

// The file client requires this event handler even though we do not use it.
void sys_file_event_storage_ready_invocation(gracht_client_t* client, const char* path) {
    _CRT_UNUSED(client);
    _CRT_UNUSED(path);
}
