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

#include <ddk/convert.h>
#include <fs/common.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/process.h>
#include <stdio.h>

#include <ctt_storage_service_client.h>
#include <sys_file_service_client.h>

static oserr_t
__ReadDevice(
        _In_  uuid_t        driverID,
        _In_  uuid_t        deviceID,
        _In_  uuid_t        buffer,
        _In_  size_t        offset,
        _In_  UInteger64_t* sector,
        _In_  size_t        count,
        _Out_ size_t*       read)
{
    struct vali_link_message msg  = VALI_MSG_INIT_HANDLE(driverID);
    oserr_t                  status;

    ctt_storage_transfer(
            GetGrachtClient(), &msg.base,
            deviceID, __STORAGE_OPERATION_READ,
            sector->u.LowPart, sector->u.HighPart,
            buffer, offset, count
    );
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_storage_transfer_result(GetGrachtClient(), &msg.base, &status, read);
    return status;
}

static oserr_t
__WriteDevice(
        _In_  uuid_t        driverID,
        _In_  uuid_t        deviceID,
        _In_  uuid_t        buffer,
        _In_  size_t        offset,
        _In_  UInteger64_t* sector,
        _In_  size_t        count,
        _Out_ size_t*       written)
{
    struct vali_link_message msg  = VALI_MSG_INIT_HANDLE(driverID);
    oserr_t                  status;

    ctt_storage_transfer(
            GetGrachtClient(), &msg.base,
            deviceID, __STORAGE_OPERATION_WRITE,
            sector->u.LowPart, sector->u.HighPart,
            buffer, offset, count
    );
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_storage_transfer_result(GetGrachtClient(), &msg.base, &status, written);
    return status;
}

static oserr_t
__StatDevice(
        _In_ uuid_t               driverID,
        _In_ uuid_t               deviceID,
        _In_ StorageDescriptor_t* stats)
{
    struct vali_link_message   msg  = VALI_MSG_INIT_HANDLE(driverID);
    oserr_t                    osStatus;
    struct sys_disk_descriptor gdescriptor;

    ctt_storage_stat(GetGrachtClient(), &msg.base, deviceID);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_storage_stat_result(GetGrachtClient(), &msg.base, &osStatus, &gdescriptor);
    if (osStatus == OsOK) {
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
        _Out_ size_t*       read)
{
    struct vali_link_message msg  = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                  status;
    UInteger64_t             seek = { .QuadPart = sector->QuadPart };
    size_t                   transferred;

    // With files so far we assume 512 byte sectors
    seek.QuadPart *= 512;
    sys_file_transfer_absolute(
            GetGrachtClient(),
            &msg.base,
            ProcessGetCurrentId(),
            handleID,
            SYS_TRANSFER_DIRECTION_READ,
            seek.u.LowPart,
            seek.u.HighPart,
            buffer,
            offset,
            count*512
    );
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_file_transfer_absolute_result(GetGrachtClient(), &msg.base, &status, &transferred);

    // convert result from bytes to sectors
    transferred /= 512;
    *read = transferred;
    return status;
}

static oserr_t
__WriteFile(
        _In_  uuid_t        handleID,
        _In_  uuid_t        buffer,
        _In_  size_t        offset,
        _In_  UInteger64_t* sector,
        _In_  size_t        count,
        _Out_ size_t*       written)
{
    struct vali_link_message msg  = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t                  status;
    UInteger64_t             seek = { .QuadPart = sector->QuadPart };
    size_t                   transferred;

    // With files so far we assume 512 byte sectors
    seek.QuadPart *= 512;
    sys_file_transfer_absolute(
            GetGrachtClient(),
            &msg.base,
            ProcessGetCurrentId(),
            handleID,
            SYS_TRANSFER_DIRECTION_WRITE,
            seek.u.LowPart,
            seek.u.HighPart,
            buffer,
            offset,
            count*512
    );
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_file_transfer_absolute_result(GetGrachtClient(), &msg.base, &status, &transferred);

    // convert result from bytes to sectors
    transferred /= 512;
    *written = transferred;
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

    sys_file_fstat(
            GetGrachtClient(),
            &msg.base,
            ProcessGetCurrentId(),
            handleID
    );
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_file_fstat_result(GetGrachtClient(), &msg.base, &osStatus, &fstats);
    if (osStatus != OsOK) {
        return osStatus;
    }

    // fill in the most details we can about the storage medium
    stats->DriverID = GetFileService();
    stats->DeviceID = handleID;
    stats->SectorCount = fstats.size / 512;
    stats->SectorSize  = 512; // TODO this can change in the future
    stats->Flags = 0;
    stats->LUNCount = 1;

    // generate model and serial
    snprintf(
            &stats->Serial[0], sizeof(stats->Serial),
            "%u-%u", fstats.storageId, fstats.id
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
    switch (storageParameters->StorageType) {
        case VFSSTORAGE_TYPE_DEVICE: {
            return __ReadDevice(
                    storageParameters->Storage.Device.DriverID,
                    storageParameters->Storage.Device.DeviceID,
                    buffer,
                    offset,
                    &(UInteger64_t) { .QuadPart = storageParameters->SectorStart.QuadPart + sector->QuadPart },
                    count,
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
    return OsNotSupported;
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
    switch (storageParameters->StorageType) {
        case VFSSTORAGE_TYPE_DEVICE: {
            return __WriteDevice(
                    storageParameters->Storage.Device.DriverID,
                    storageParameters->Storage.Device.DeviceID,
                    buffer,
                    offset,
                    &(UInteger64_t) { .QuadPart = storageParameters->SectorStart.QuadPart + sector->QuadPart },
                    count,
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
    return OsNotSupported;
}

oserr_t
FSStorageStat(
        _In_ struct VFSStorageParameters* storageParameters,
        _In_ StorageDescriptor_t*         stat)
{
    switch (storageParameters->StorageType) {
        case VFSSTORAGE_TYPE_DEVICE: {
            return __StatDevice(
                    storageParameters->Storage.Device.DriverID,
                    storageParameters->Storage.Device.DeviceID,
                    stat
            );
        } break;
        case VFSSTORAGE_TYPE_FILE: {
            return __StatFile(
                    storageParameters->Storage.File.HandleID,
                    stat
            );
        } break;
    }
    return OsNotSupported;
}

// Because we had to include a client file which defines events
void sys_file_event_storage_ready_invocation(gracht_client_t* client, const char* path) {
    _CRT_UNUSED(client);
    _CRT_UNUSED(path);
}
