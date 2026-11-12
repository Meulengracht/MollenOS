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
 */

#define __TRACE

#include <ddk/convert.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <internal/_gracht.h>
#include <internal/_utils.h>
#include <internal/_storage.h>
#include <vfs/storage.h>
#include <ctt_storage_service_client.h>

static oserr_t __ReadDevice(struct VFSStorage*, uuid_t, size_t, UInteger64_t*, size_t, size_t*);
static oserr_t __WriteDevice(struct VFSStorage*, uuid_t, size_t, UInteger64_t*, size_t, size_t*);

static struct VFSStorageOperations g_operations = {
        .Read = __ReadDevice,
        .Write = __WriteDevice,
};

static oserr_t __DeviceQueryStats(
        _In_ uuid_t               deviceID,
        _In_ uuid_t               driverID,
        _In_ StorageDescriptor_t* stats)
{
    struct vali_link_message   msg  = VALI_MSG_INIT_HANDLE(driverID);
    oserr_t                    osStatus;
    struct sys_disk_descriptor gdescriptor;
    TRACE("__DeviceQueryStats()");

    if (ctt_storage_stat(GetGrachtClient(), &msg.base, deviceID) != 0) {
        return OS_EUNKNOWN;
    }

    osStatus = __GrachtAwaitCompleted(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    if (ctt_storage_stat_result(GetGrachtClient(), &msg.base, &osStatus, &gdescriptor) != 0) {
        return OS_EPROTOCOL;
    }

    if (osStatus == OS_EOK) {
        from_sys_disk_descriptor_dkk(&gdescriptor, stats);
        if (stats->SectorSize == 0) {
            return OS_EINVALPARAMS;
        }
    }
    return osStatus;
}

struct VFSStorage*
VFSStorageCreateDeviceBacked(
        _In_ uuid_t       deviceID,
        _In_ uuid_t       driverID,
        _In_ unsigned int flags)
{
    struct VFSStorage*      storage;
    oserr_t                 oserr;

    storage = VFSStorageNew(&g_operations, flags);
    if (storage == NULL) {
        return NULL;
    }

    // setup protocol settings
    storage->Protocol.StorageType = VFSSTORAGE_TYPE_DEVICE;
    storage->Protocol.Storage.Device.DriverID = driverID;
    storage->Protocol.Storage.Device.DeviceID = deviceID;

    oserr = __DeviceQueryStats(deviceID, driverID, &storage->Stats);
    if (oserr != OS_EOK) {
        VFSStorageDelete(storage);
        return NULL;
    }
    return storage;
}

static oserr_t __ReadDevice(
        _In_ struct VFSStorage* storage,
        _In_ uuid_t             buffer,
        _In_ size_t             offset,
        _In_ UInteger64_t*      sector,
        _In_ size_t             count,
        _In_ size_t*            read)
{
    return __StorageTransfer(
        storage->Protocol.Storage.Device.DriverID,
        storage->Protocol.Storage.Device.DeviceID,
        buffer,
        offset,
        sector,
        storage->Stats.SectorSize,
        count,
        false,
        read);
}

static oserr_t __WriteDevice(
        _In_ struct VFSStorage* storage,
        _In_ uuid_t             buffer,
        _In_ size_t             offset,
        _In_ UInteger64_t*      sector,
        _In_ size_t             count,
        _In_ size_t*            written)
{
    return __StorageTransfer(
        storage->Protocol.Storage.Device.DriverID,
        storage->Protocol.Storage.Device.DeviceID,
        buffer,
        offset,
        sector,
        storage->Stats.SectorSize,
        count,
        true,
        written);
}
