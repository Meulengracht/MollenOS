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
#include <internal/_utils.h>
#include <vfs/storage.h>
#include <ctt_storage_service_client.h>

struct __DeviceContext {
    uuid_t              DeviceID;
    uuid_t              DriverID;
    unsigned int        Flags;
};

static void    __DestroyDevice(void*);
static oserr_t __ReadDevice(void*, uuid_t, size_t, UInteger64_t*, size_t, size_t*);
static oserr_t __WriteDevice(void*, uuid_t, size_t, UInteger64_t*, size_t, size_t*);

static struct VFSStorageOperations g_operations = {
        .Destroy = __DestroyDevice,
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

    ctt_storage_stat(GetGrachtClient(), &msg.base, deviceID);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_storage_stat_result(GetGrachtClient(), &msg.base, &osStatus, &gdescriptor);
    if (osStatus == OsOK) {
        from_sys_disk_descriptor_dkk(&gdescriptor, stats);
    }
    return osStatus;
}

static struct __DeviceContext* __DeviceContextNew(
        _In_ uuid_t       deviceID,
        _In_ uuid_t       driverID,
        _In_ unsigned int flags)
{
    struct __DeviceContext* context = malloc(sizeof(struct __DeviceContext));
    if (context == NULL) {
        return NULL;
    }
    context->DeviceID = deviceID;
    context->DriverID = driverID;
    context->Flags = flags;
    return context;
}

struct VFSStorage*
VFSStorageCreateDeviceBacked(
        _In_ uuid_t       deviceID,
        _In_ uuid_t       driverID,
        _In_ unsigned int flags)
{
    struct VFSStorage*      storage;
    struct __DeviceContext* context;
    oserr_t                 oserr;

    storage = VFSStorageNew(&g_operations);
    if (storage == NULL) {
        return NULL;
    }

    context = __DeviceContextNew(deviceID, driverID, flags);
    if (context == NULL) {
        free(storage);
        return NULL;
    }
    storage->Data = context;

    oserr = __DeviceQueryStats(deviceID, driverID, &storage->Stats);
    if (oserr != OsOK) {
        VFSStorageDelete(storage);
        return NULL;
    }
    return storage;
}

static void __DestroyDevice(
        _In_ void* context)
{
    struct __DeviceContext* device = context;
    free(device);
}

static oserr_t __ReadDevice(
        _In_ void*         context,
        _In_ uuid_t        buffer,
        _In_ size_t        offset,
        _In_ UInteger64_t* sector,
        _In_ size_t        count,
        _In_ size_t*       read)
{
    struct __DeviceContext*  device = context;
    struct vali_link_message msg  = VALI_MSG_INIT_HANDLE(device->DriverID);
    oserr_t                  status;

    ctt_storage_transfer(
            GetGrachtClient(), &msg.base,
            device->DeviceID, __STORAGE_OPERATION_READ,
            sector->u.LowPart, sector->u.HighPart,
            buffer, offset, count
    );
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_storage_transfer_result(GetGrachtClient(), &msg.base, &status, read);
    return status;
}

static oserr_t __WriteDevice(
        _In_ void*         context,
        _In_ uuid_t        buffer,
        _In_ size_t        offset,
        _In_ UInteger64_t* sector,
        _In_ size_t        count,
        _In_ size_t*       written)
{
    struct __DeviceContext* device = context;
    struct vali_link_message msg  = VALI_MSG_INIT_HANDLE(device->DriverID);
    oserr_t                  status;

    ctt_storage_transfer(
            GetGrachtClient(), &msg.base,
            device->DeviceID, __STORAGE_OPERATION_WRITE,
            sector->u.LowPart, sector->u.HighPart,
            buffer, offset, count
    );
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_storage_transfer_result(GetGrachtClient(), &msg.base, &status, written);
    return status;
}
