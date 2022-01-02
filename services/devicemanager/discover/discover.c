/**
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
 * Device Manager
 * - Implementation of the device manager in the operating system.
 *   Keeps track of devices, their loaded drivers and bus management.
 */

//#define __TRACE

#include <discover.h>
#include <devices.h>
#include <ramdisk.h>
#include <requests.h>
#include <ddk/utils.h>
#include <ds/list.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/process.h>
#include <os/usched/mutex.h>
#include <os/usched/usched.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <ctt_driver_service_client.h>

enum DmDriverState {
    DmDriverState_NOTLOADED,
    DmDriverState_LOADING,
    DmDriverState_AVAILABLE
};

struct DmDriver {
    element_t                    list_header;
    UUId_t                       id;
    UUId_t                       handle;
    enum DmDriverState           state;
    MString_t*                   path;
    struct DriverIdentification* identifiers;
    int                          identifiers_count;
    list_t                       devices; // list<struct DmDevice>
    struct usched_mtx            devices_lock;
};

struct DmDevice {
    element_t list_header;
    UUId_t    id;
};

static struct usched_mtx g_driversLock;
static list_t            g_drivers  = LIST_INIT;
static UUId_t            g_driverId = 1;

void
DmDiscoverInitialize(void)
{
    // Initialize all our dependencies first before we start discovering drivers
    usched_mtx_init(&g_driversLock);

    // Start parsing the ramdisk as that is all we have initially, do it in usched
    // context, so we spawn a job to do this.
    usched_task_queue((usched_task_fn)DmRamdiskDiscover, NULL);
}

static void
__DestroyDriver(
        _In_ struct DmDriver* driver)
{
    // freeing NULLS behave as noops
    free(driver->identifiers);
    MStringDestroy(driver->path);
    free(driver);
}

OsStatus_t
DmDiscoverAddDriver(
        _In_ MString_t*                   driverPath,
        _In_ struct DriverIdentification* identifiers,
        _In_ int                          identifiersCount)
{
    struct DmDriver* driver;
    TRACE("DmDiscoverAddDriver(path=%s, identifiersCount=%i)",
          MStringRaw(driverPath), identifiersCount);

    driver = malloc(sizeof(struct DmDriver));
    if (!driver) {
        return OsOutOfMemory;
    }
    memset(driver, 0, sizeof(struct DmDriver));

    ELEMENT_INIT(&driver->list_header, 0, driver);
    driver->id                = g_driverId++;
    driver->handle            = UUID_INVALID;
    driver->state             = DmDriverState_NOTLOADED;
    driver->identifiers_count = identifiersCount;
    list_construct(&driver->devices);
    usched_mtx_init(&driver->devices_lock);

    driver->path = MStringClone(driverPath);
    if (!driver->path) {
        __DestroyDriver(driver);
        return OsOutOfMemory;
    }

    driver->identifiers = malloc(sizeof(struct DriverIdentification) * identifiersCount);
    if (!driver->identifiers) {
        __DestroyDriver(driver);
        return OsOutOfMemory;
    }
    memcpy(driver->identifiers, identifiers, sizeof(struct DriverIdentification) * identifiersCount);

    usched_mtx_lock(&g_driversLock);
    list_append(&g_drivers, &driver->list_header);
    usched_mtx_unlock(&g_driversLock);
    return OsSuccess;
}

OsStatus_t
DmDiscoverRemoveDriver(
        _In_ MString_t* driverPath)
{
    OsStatus_t osStatus = OsDoesNotExist;

    usched_mtx_lock(&g_driversLock);
    foreach (i, &g_drivers) {
        struct DmDriver* driver = i->value;
        if (MStringCompare(driver->path, driverPath, 0) == MSTRING_FULL_MATCH) {
            // we do not remove the driver if the driver is already loading/loaded
            if (driver->state == DmDriverState_NOTLOADED) {
                list_remove(&g_drivers, &driver->list_header);
                __DestroyDriver(driver);
            }

            osStatus =  OsSuccess;
            break;
        }
    }
    usched_mtx_unlock(&g_driversLock);
    return osStatus;
}

static OsStatus_t
__SpawnDriver(
        _In_ struct DmDriver* driver)
{
    UUId_t     handle;
    OsStatus_t osStatus;
    char       args[32];

    sprintf(&args[0], "--id %u", driver->id);

    osStatus = ProcessSpawn(MStringRaw(driver->path), &args[0], &handle);
    if (osStatus != OsSuccess) {
        return osStatus;
    }

    // update driver state
    driver->state = DmDriverState_LOADING;

    return OsSuccess;
}

static void
__RegisterDeviceForDriver(
        _In_ struct DmDriver* driver,
        _In_ UUId_t         deviceId)
{
    struct DmDevice* device;

    device = malloc(sizeof(struct DmDevice));
    if (!device) {
        return;
    }

    ELEMENT_INIT(&device->list_header, 0, device);
    device->id = deviceId;

    usched_mtx_lock(&driver->devices_lock);
    list_append(&driver->devices, &device->list_header);
    usched_mtx_unlock(&driver->devices_lock);
}

static int
__IsIdentificationsMatch(
        _In_ struct DriverIdentification* lh,
        _In_ struct DriverIdentification* rh)
{
    // We must have a vendor/product match, or a generic match
    if (lh->VendorId != 0 && lh->ProductId != 0 &&
        lh->VendorId == rh->VendorId &&
        lh->ProductId == rh->ProductId) {
        return 1;
    }

    if (lh->Class != 0 && lh->Subclass != 0 &&
        lh->Class == rh->Class &&
        lh->Subclass == rh->Subclass) {
        return 1;
    }

    return 0;
}

static int
__IsDriverMatch(
        _In_ struct DmDriver*             driver,
        _In_ struct DriverIdentification* deviceIdentification)
{
    for (int i = 0; i < driver->identifiers_count; i++) {
        if (__IsIdentificationsMatch(deviceIdentification, &driver->identifiers[i])) {
            return 1;
        }
    }
    return 0;
}

OsStatus_t
DmDiscoverFindDriver(
        _In_ UUId_t                       deviceId,
        _In_ struct DriverIdentification* deviceIdentification)
{
    OsStatus_t osStatus = OsDoesNotExist;

    usched_mtx_lock(&g_driversLock);
    foreach (i, &g_drivers) {
        struct DmDriver* driver = i->value;
        if (__IsDriverMatch(driver, deviceIdentification)) {
            __RegisterDeviceForDriver(driver, deviceId);
            if (driver->state == DmDriverState_NOTLOADED) {
                osStatus = __SpawnDriver(driver);
            }
            else if (driver->state == DmDriverState_AVAILABLE) {
                osStatus = DmDevicesRegister(driver->handle, deviceId);
            }
            else {
                osStatus = OsSuccess;
            }
            break;
        }
    }
    usched_mtx_unlock(&g_driversLock);
    return osStatus;
}

static struct DmDriver*
__GetDriver(
        _In_ UUId_t id)
{
    struct DmDriver* result = NULL;

    usched_mtx_lock(&g_driversLock);
    foreach (i, &g_drivers) {
        struct DmDriver* driver = i->value;
        if (driver->id == id) {
            result = driver;
            break;
        }
    }
    usched_mtx_unlock(&g_driversLock);
    return result;
}

static void
__SubscribeToDriver(
        _In_ struct DmDriver* driver)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(driver->handle);
    ctt_driver_subscribe(GetGrachtClient(), &msg.base);
}

static void
__NotifyDevices(
        _In_ struct DmDriver* driver)
{
    usched_mtx_lock(&driver->devices_lock);
    foreach (i, &driver->devices) {
        struct DmDevice* device   = i->value;
        OsStatus_t       osStatus = DmDevicesRegister(driver->handle, device->id);
        if (osStatus != OsSuccess) {
            WARNING("__NotifyDevices failed to notify driver of device %u", device->id);
        }
    }
    usched_mtx_unlock(&driver->devices_lock);
}

void DmHandleNotify(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    // driver is now booted, we can send all the devices that have
    // been registered
    struct DmDriver* driver;
    TRACE("DmHandleNotify(driverId=%u)",
          request->parameters.notify.driver_id);

    driver = __GetDriver(request->parameters.notify.driver_id);
    if (!driver) {
        ERROR("DmHandleNotify driver provided an invalid id");
        return;
    }

    // Update the driver with the provided handle
    driver->handle = request->parameters.notify.driver_handle;
    __SubscribeToDriver(driver);

    // iterate all devices attached and send them
    __NotifyDevices(driver);
    RequestDestroy(request);
}
