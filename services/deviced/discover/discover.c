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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Device Manager
 * - Implementation of the device manager in the operating system.
 *   Keeps track of devices, their loaded drivers and bus management.
 */

#define __TRACE

#include <discover.h>
#include <devices.h>
#include <configparser.h>
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
    uuid_t                       id;
    uuid_t                       handle;
    enum DmDriverState           state;
    mstring_t*                   path;
    struct DriverConfiguration*  configuration;
    list_t                       devices; // list<struct DmDevice>
    struct usched_mtx            devices_lock;
};

struct DmDevice {
    element_t list_header;
    uuid_t    id;
};

static struct usched_mtx g_driversLock;
static list_t            g_drivers  = LIST_INIT;
static uuid_t            g_driverId = 1;

void
DmDiscoverInitialize(void)
{
    // Initialize all our dependencies first before we start discovering drivers
    usched_mtx_init(&g_driversLock);

    // Start parsing the ramdisk as that is all we have initially, do it in usched
    // context, so we spawn a job to do this.
    usched_job_queue((usched_task_fn)DmRamdiskDiscover, NULL);
}

static void
__DestroyDriver(
        _In_ struct DmDriver* driver)
{
    // freeing NULLS behave as noops
    DmDriverConfigDestroy(driver->configuration);
    mstr_delete(driver->path);
    free(driver);
}

oserr_t
DmDiscoverAddDriver(
        _In_ mstring_t*                  driverPath,
        _In_ struct DriverConfiguration* driverConfig)
{
    struct DmDriver* driver;
    TRACE("DmDiscoverAddDriver(path=%ms, class=%u, subclass=%u)",
          driverPath, driverConfig->Class, driverConfig->Subclass);

    driver = malloc(sizeof(struct DmDriver));
    if (!driver) {
        return OsOutOfMemory;
    }
    memset(driver, 0, sizeof(struct DmDriver));

    ELEMENT_INIT(&driver->list_header, 0, driver);
    driver->id                = g_driverId++;
    driver->handle            = UUID_INVALID;
    driver->state             = DmDriverState_NOTLOADED;
    driver->configuration     = driverConfig;
    list_construct(&driver->devices);
    usched_mtx_init(&driver->devices_lock);

    driver->path = mstr_clone(driverPath);
    if (!driver->path) {
        __DestroyDriver(driver);
        return OsOutOfMemory;
    }

    usched_mtx_lock(&g_driversLock);
    list_append(&g_drivers, &driver->list_header);
    usched_mtx_unlock(&g_driversLock);
    return OsOK;
}

oserr_t
DmDiscoverRemoveDriver(
        _In_ mstring_t* driverPath)
{
    oserr_t osStatus = OsNotExists;

    usched_mtx_lock(&g_driversLock);
    foreach (i, &g_drivers) {
        struct DmDriver* driver = i->value;
        if (!mstr_cmp(driver->path, driverPath)) {
            // we do not remove the driver if the driver is already loading/loaded
            if (driver->state == DmDriverState_NOTLOADED) {
                list_remove(&g_drivers, &driver->list_header);
                __DestroyDriver(driver);
            }

            osStatus =  OsOK;
            break;
        }
    }
    usched_mtx_unlock(&g_driversLock);
    return osStatus;
}

static oserr_t
__SpawnDriver(
        _In_ struct DmDriver* driver)
{
    uuid_t  handle;
    oserr_t osStatus;
    char    args[32];
    char*   driverPath;

    driverPath = mstr_u8(driver->path);
    if (driverPath == NULL) {
        return OsOutOfMemory;
    }

    sprintf(&args[0], "--id %u", driver->id);

    osStatus = ProcessSpawn(driverPath, &args[0], &handle);
    free(driverPath);
    if (osStatus != OsOK) {
        return osStatus;
    }

    // update driver state
    driver->state = DmDriverState_LOADING;

    return OsOK;
}

static void
__RegisterDeviceForDriver(
        _In_ struct DmDriver* driver,
        _In_ uuid_t         deviceId)
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
__IsDriverMatch(
        _In_ struct DmDriver*             driver,
        _In_ struct DriverIdentification* deviceIdentification)
{
    if (deviceIdentification->VendorId != 0) {
        foreach (i, &driver->configuration->Vendors) {
            struct DriverVendor* vendor = i->value;
            if (vendor->Id == deviceIdentification->VendorId) {
                foreach (j, &vendor->Products) {
                    struct DriverProduct* product = i->value;
                    if (product->Id == deviceIdentification->ProductId) {
                        return 1;
                    }
                }
            }
        }
    }

    if (!(deviceIdentification->Class == 0 && deviceIdentification->Subclass == 0) &&
        deviceIdentification->Class == driver->configuration->Class &&
        deviceIdentification->Subclass == driver->configuration->Subclass) {
        return 1;
    }
    return 0;
}

oserr_t
DmDiscoverFindDriver(
        _In_ uuid_t                       deviceId,
        _In_ struct DriverIdentification* deviceIdentification)
{
    oserr_t osStatus = OsNotExists;
    TRACE("DmDiscoverFindDriver(deviceId=%u, class=%u, subclass=%u)",
          deviceId, deviceIdentification->Class, deviceIdentification->Subclass);

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
                osStatus = OsOK;
            }
            break;
        }
    }
    usched_mtx_unlock(&g_driversLock);
    return osStatus;
}

static struct DmDriver*
__GetDriver(
        _In_ uuid_t id)
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
        struct DmDevice* device = i->value;
        oserr_t          oserr  = DmDevicesRegister(driver->handle, device->id);
        if (oserr != OsOK) {
            WARNING("__NotifyDevices failed to notify driver of device %u", device->id);
        }
    }
    usched_mtx_unlock(&driver->devices_lock);
}

void DmHandleNotify(
        _In_ Request_t* request)
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
