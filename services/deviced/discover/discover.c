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
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/services/process.h>
#include <os/usched/mutex.h>
#include <os/usched/job.h>
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

struct DMDevice {
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
    usched_mtx_init(&g_driversLock, USCHED_MUTEX_PLAIN);

    // Start parsing the ramdisk as that is all we have initially, do it in usched
    // context, so we spawn a job to do this. After discovering drivers, we want to do
    // a refresh of drivers for devices. This ensures that we don't have to enumerate
    // devices and drivers in a certain order
    usched_job_queue((usched_task_fn)DmRamdiskDiscover, NULL);
    usched_job_queue((usched_task_fn)DmDeviceRefreshDrivers, NULL);
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
        return OS_EOOM;
    }
    memset(driver, 0, sizeof(struct DmDriver));

    ELEMENT_INIT(&driver->list_header, 0, driver);
    driver->id                = g_driverId++;
    driver->handle            = UUID_INVALID;
    driver->state             = DmDriverState_NOTLOADED;
    driver->configuration     = driverConfig;
    list_construct(&driver->devices);
    usched_mtx_init(&driver->devices_lock, USCHED_MUTEX_PLAIN);

    driver->path = mstr_clone(driverPath);
    if (!driver->path) {
        __DestroyDriver(driver);
        return OS_EOOM;
    }

    usched_mtx_lock(&g_driversLock);
    list_append(&g_drivers, &driver->list_header);
    usched_mtx_unlock(&g_driversLock);
    return OS_EOK;
}

oserr_t
DmDiscoverRemoveDriver(
    _In_ mstring_t* driverPath)
{
    oserr_t osStatus = OS_ENOENT;

    usched_mtx_lock(&g_driversLock);
    foreach (i, &g_drivers) {
        struct DmDriver* driver = i->value;
        if (!mstr_cmp(driver->path, driverPath)) {
            // we do not remove the driver if the driver is already loading/loaded
            if (driver->state == DmDriverState_NOTLOADED) {
                list_remove(&g_drivers, &driver->list_header);
                __DestroyDriver(driver);
            }

            osStatus =  OS_EOK;
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
    TRACE("__SpawnDriver(%ms)", driver->path);

    driverPath = mstr_u8(driver->path);
    if (driverPath == NULL) {
        return OS_EOOM;
    }

    sprintf(&args[0], "--id %u", driver->id);

    osStatus = OSProcessSpawn(driverPath, &args[0], &handle);
    free(driverPath);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    // update driver state
    driver->state = DmDriverState_LOADING;

    return OS_EOK;
}

static oserr_t
__RegisterDeviceForDriver(
    _In_ struct DmDriver* driver,
    _In_ uuid_t           deviceId)
{
    struct DMDevice* device;
    TRACE("__RegisterDeviceForDriver()");

    device = malloc(sizeof(struct DMDevice));
    if (!device) {
        return OS_EOOM;
    }

    ELEMENT_INIT(&device->list_header, 0, device);
    device->id = deviceId;

    usched_mtx_lock(&driver->devices_lock);
    foreach (i, &driver->devices) {
        if (((struct DMDevice*)i->value)->id == deviceId) {
            usched_mtx_unlock(&driver->devices_lock);
            free(device);
            return OS_EEXISTS;
        }
    }
    list_append(&driver->devices, &device->list_header);
    usched_mtx_unlock(&driver->devices_lock);
    return OS_EOK;
}

static void
__ForgetDriverDevice(
    _In_ struct DmDriver* driver,
    _In_ uuid_t           deviceId)
{
    struct DMDevice* device;

    usched_mtx_lock(&driver->devices_lock);
    foreach (i, &driver->devices) {
        device = i->value;
        if (device->id == deviceId) {
            list_remove(&driver->devices, &device->list_header);
            free(device);
            break;
        }
    }
    usched_mtx_unlock(&driver->devices_lock);
}

void
DmDiscoverForgetDevice(
    _In_ uuid_t deviceId)
{
    usched_mtx_lock(&g_driversLock);
    foreach (i, &g_drivers) {
        __ForgetDriverDevice(i->value, deviceId);
    }
    usched_mtx_unlock(&g_driversLock);
}

oserr_t
DmDiscoverFindDriver(
    _In_ uuid_t deviceId,
    _In_ struct DriverIdentification* identification)
{
    struct DmDriver* selected = NULL;
    struct DmDriver* driver;
    unsigned int     best = 0;
    unsigned int     score;
    oserr_t          status;

    usched_mtx_lock(&g_driversLock);
    
    // Pair this check with ForgetDevice's discovery lock. Removal cannot leave
    // a queued binding behind even if it races a driver search.
    if (!DmDeviceIsBindable(deviceId)) {
        usched_mtx_unlock(&g_driversLock);
        return OS_ENOENT;
    }
    
    foreach (i, &g_drivers) {
        driver = i->value;
        score = DmDriverMatchScore(driver->configuration, identification);
        if (score && (!best || score < best)) {
            selected = driver;
            best = score;
        }
    }
    if (selected == NULL) {
        usched_mtx_unlock(&g_driversLock);
        return OS_ENOENT;
    }
    
    status = __RegisterDeviceForDriver(selected, deviceId);
    if (status == OS_EEXISTS) {
        status = OS_EOK;
    }
    if (status == OS_EOK) {
        if (selected->state == DmDriverState_NOTLOADED) {
            status = __SpawnDriver(selected);
        } else if (selected->state == DmDriverState_AVAILABLE) {
            status = DmDevicesRegister(selected->handle, deviceId);
            if (status == OS_EEXISTS) {
                status = OS_EOK;
            }
        }
        if (status != OS_EOK) {
            __ForgetDriverDevice(selected, deviceId);
        }
    }
    
    usched_mtx_unlock(&g_driversLock);
    return status;
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
        struct DMDevice* device = i->value;
        oserr_t          oserr  = DmDevicesRegister(driver->handle, device->id);
        if (oserr != OS_EOK) {
            WARNING("__NotifyDevices failed to notify driver of device %u", device->id);
        }
    }
    usched_mtx_unlock(&driver->devices_lock);
}

void DmHandleNotify(
    _In_ uuid_t driverId,
    _In_ uuid_t driverHandle)
{
    // driver is now booted, we can send all the devices that have
    // been registered
    struct DmDriver* driver;
    TRACE("DmHandleNotify(driverId=%u)", driverId);

    driver = __GetDriver(driverId);
    if (!driver) {
        ERROR("DmHandleNotify driver provided an invalid id");
        return;
    }

    // Update the driver with the provided handle
    usched_mtx_lock(&g_driversLock);
    driver->handle = driverHandle;
    driver->state = DmDriverState_AVAILABLE;
    __SubscribeToDriver(driver);

    // iterate all devices attached and send them
    __NotifyDevices(driver);
    usched_mtx_unlock(&g_driversLock);
}
