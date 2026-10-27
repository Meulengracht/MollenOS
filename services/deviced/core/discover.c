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

//#define __TRACE

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

struct __DmDriver {
    element_t                   ListHeader;
    uuid_t                      Id;
    uuid_t                      Handle;
    enum DmDriverState          State;
    mstring_t*                  Path;
    struct DriverConfiguration* Configuration;
    list_t                      Devices; // list<struct __PendingDriverDevice>
    struct usched_mtx           DevicesLock;
};

struct __PendingDriverDevice {
    element_t ListHeader;
    uuid_t    Id;
};

static struct usched_mtx g_driversLock;
static list_t            g_drivers  = LIST_INIT;
static uuid_t            g_driverId = 1;

void
DmDiscoverInitialize(void)
{
    uuid_t jobId;
    TRACE("DmDiscoverInitialize()");

    // Initialize the driver list lock before scheduling discovery.
    usched_mtx_init(&g_driversLock, USCHED_MUTEX_PLAIN);

    // The ramdisk is the initial source of drivers. Schedule discovery and device
    // refresh in usched context.
    jobId = usched_job_queue((usched_task_fn)DmRamdiskDiscover, NULL);
    if (jobId == UUID_INVALID) {
        ERROR("DmDiscoverInitialize: failed to queue ramdisk discovery");
    }
    jobId = usched_job_queue((usched_task_fn)DmDeviceRefreshDrivers, NULL);
    if (jobId == UUID_INVALID) {
        ERROR("DmDiscoverInitialize: failed to queue driver refresh");
    }
}

static void
__DestroyDriver(
    _In_ struct __DmDriver* driver)
{
    // Configuration and path cleanup accept NULL during partial initialization.
    DmDriverConfigDestroy(driver->Configuration);
    mstr_delete(driver->Path);
    free(driver);
}

oserr_t
DmDiscoverAddDriver(
    _In_ mstring_t*                   driverPath,
    _In_ struct DriverConfiguration* driverConfig)
{
    struct __DmDriver* driver;
    TRACE("DmDiscoverAddDriver(path=%ms, class=%u, subclass=%u)",
          driverPath, driverConfig->Class, driverConfig->Subclass);

    driver = malloc(sizeof(struct __DmDriver));
    if (!driver) {
        ERROR("DmDiscoverAddDriver: failed to allocate driver %ms", driverPath);
        return OS_EOOM;
    }
    memset(driver, 0, sizeof(struct __DmDriver));

    ELEMENT_INIT(&driver->ListHeader, 0, driver);
    driver->Id            = g_driverId++;
    driver->Handle        = UUID_INVALID;
    driver->State         = DmDriverState_NOTLOADED;
    driver->Configuration = driverConfig;
    list_construct(&driver->Devices);
    usched_mtx_init(&driver->DevicesLock, USCHED_MUTEX_PLAIN);

    driver->Path = mstr_clone(driverPath);
    if (!driver->Path) {
        ERROR("DmDiscoverAddDriver: failed to copy path %ms", driverPath);
        __DestroyDriver(driver);
        return OS_EOOM;
    }

    usched_mtx_lock(&g_driversLock);
    list_append(&g_drivers, &driver->ListHeader);
    TRACE("DmDiscoverAddDriver: added driver %u at %ms", driver->Id, driver->Path);
    usched_mtx_unlock(&g_driversLock);
    return OS_EOK;
}

oserr_t
DmDiscoverRemoveDriver(
    _In_ mstring_t* driverPath)
{
    struct __DmDriver* driver;
    oserr_t            osStatus = OS_ENOENT;
    TRACE("DmDiscoverRemoveDriver(path=%ms)", driverPath);

    usched_mtx_lock(&g_driversLock);
    foreach (i, &g_drivers) {
        driver = i->value;
        if (!mstr_cmp(driver->Path, driverPath)) {
            // Removal is allowed only before the driver process starts.
            if (driver->State == DmDriverState_NOTLOADED) {
                TRACE("DmDiscoverRemoveDriver: removing unloaded driver %u", driver->Id);
                list_remove(&g_drivers, &driver->ListHeader);
                __DestroyDriver(driver);
            } else {
                TRACE("DmDiscoverRemoveDriver: keeping driver %u in state %i", driver->Id, driver->State);
            }

            osStatus = OS_EOK;
            break;
        }
    }
    usched_mtx_unlock(&g_driversLock);
    return osStatus;
}

static oserr_t
__SpawnDriver(
    _In_ struct __DmDriver* driver)
{
    OSProcessOptions_t* options;
    uuid_t              handle;
    oserr_t             osStatus;
    char                args[32];
    char*               driverPath;
    TRACE("__SpawnDriver(%ms)", driver->Path);

    driverPath = mstr_u8(driver->Path);
    if (driverPath == NULL) {
        ERROR("__SpawnDriver: failed to encode path for driver %u", driver->Id);
        return OS_EOOM;
    }

    sprintf(&args[0], "--id %u", driver->Id);

    options = OSProcessOptionsNew();
    if (options == NULL) {
        ERROR("__SpawnDriver: failed to allocate options for driver %u", driver->Id);
        free(driverPath);
        return OS_EOOM;
    }

    OSProcessOptionsSetArguments(options, args);
    OSProcessOptionsSetCapabilities(options, driver->Configuration->Capabilities);

    TRACE("__SpawnDriver: requested capabilities=0x%x", driver->Configuration->Capabilities);
    osStatus = OSProcessSpawnOpts(driverPath, options, &handle);
    if (osStatus != OS_EOK) {
        ERROR("__SpawnDriver: driver %u (%s) process creation failed (%i), capabilities=0x%x",
              driver->Id, driverPath, osStatus, driver->Configuration->Capabilities);
        OSProcessOptionsDelete(options);
        free(driverPath);
        return osStatus;
    }
    OSProcessOptionsDelete(options);
    free(driverPath);

    // Wait for DmHandleNotify before registering devices with this process.
    driver->State = DmDriverState_LOADING;
    TRACE("__SpawnDriver: driver %u is loading with handle %u", driver->Id, handle);

    return OS_EOK;
}

static oserr_t
__RegisterDeviceForDriver(
    _In_ struct __DmDriver* driver,
    _In_ uuid_t             deviceId)
{
    struct __PendingDriverDevice* device;
    TRACE("__RegisterDeviceForDriver(driver=%u, device=%u)", driver->Id, deviceId);

    device = malloc(sizeof(struct __PendingDriverDevice));
    if (!device) {
        ERROR("__RegisterDeviceForDriver: failed to allocate binding for device %u, driver %u",
              deviceId, driver->Id);
        return OS_EOOM;
    }

    ELEMENT_INIT(&device->ListHeader, 0, device);
    device->Id = deviceId;

    usched_mtx_lock(&driver->DevicesLock);
    foreach (i, &driver->Devices) {
        if (((struct __PendingDriverDevice*)i->value)->Id == deviceId) {
            TRACE("__RegisterDeviceForDriver: device %u is already queued for driver %u", deviceId, driver->Id);
            usched_mtx_unlock(&driver->DevicesLock);
            free(device);
            return OS_EEXISTS;
        }
    }
    list_append(&driver->Devices, &device->ListHeader);
    usched_mtx_unlock(&driver->DevicesLock);
    return OS_EOK;
}

static void
__ForgetDriverDevice(
    _In_ struct __DmDriver* driver,
    _In_ uuid_t             deviceId)
{
    struct __PendingDriverDevice* device;

    usched_mtx_lock(&driver->DevicesLock);
    foreach (i, &driver->Devices) {
        device = i->value;
        if (device->Id == deviceId) {
            list_remove(&driver->Devices, &device->ListHeader);
            free(device);
            break;
        }
    }
    usched_mtx_unlock(&driver->DevicesLock);
}

void
DmDiscoverForgetDevice(
    _In_ uuid_t deviceId)
{
    TRACE("DmDiscoverForgetDevice(device=%u)", deviceId);
    usched_mtx_lock(&g_driversLock);
    foreach (i, &g_drivers) {
        __ForgetDriverDevice(i->value, deviceId);
    }
    usched_mtx_unlock(&g_driversLock);
}

oserr_t
DmDiscoverFindDriver(
    _In_ uuid_t                       deviceId,
    _In_ struct DriverIdentification* identification)
{
    struct __DmDriver* selected = NULL;
    struct __DmDriver* driver;
    unsigned int       best = 0;
    unsigned int       score;
    oserr_t            status;
    TRACE("DmDiscoverFindDriver(device=%u)", deviceId);

    usched_mtx_lock(&g_driversLock);

    // Pair this check with ForgetDevice's discovery lock. Removal cannot leave
    // a queued binding behind even if it races a driver search.
    if (!DmDeviceIsBindable(deviceId)) {
        TRACE("DmDiscoverFindDriver: device %u is not bindable", deviceId);
        usched_mtx_unlock(&g_driversLock);
        return OS_ENOENT;
    }

    foreach (i, &g_drivers) {
        driver = i->value;
        score = DmDriverMatchScore(driver->Configuration, identification);
        if (score && (!best || score < best)) {
            selected = driver;
            best = score;
        }
    }
    if (selected == NULL) {
        TRACE("DmDiscoverFindDriver: no matching driver for device %u", deviceId);
        usched_mtx_unlock(&g_driversLock);
        return OS_ENOENT;
    }

    TRACE("DmDiscoverFindDriver: selected driver %u for device %u with score %u",
          selected->Id, deviceId, best);
    status = __RegisterDeviceForDriver(selected, deviceId);
    if (status != OS_EOK && status != OS_EEXISTS) {
        WARNING("DmDiscoverFindDriver: queueing device %u for driver %u failed: %u",
                deviceId, selected->Id, status);
        usched_mtx_unlock(&g_driversLock);
        return status;
    }

    if (selected->State == DmDriverState_NOTLOADED) {
        status = __SpawnDriver(selected);
        if (status != OS_EOK) {
            WARNING("DmDiscoverFindDriver: starting driver %u for device %u failed: %u",
                    selected->Id, deviceId, status);
            goto error;
        }
    } else if (selected->State == DmDriverState_AVAILABLE) {
        status = DmDevicesRegister(selected->Handle, deviceId);
        if (status != OS_EOK && status != OS_EEXISTS) {
            WARNING("DmDiscoverFindDriver: registering device %u with driver %u failed: %u",
                    deviceId, selected->Id, status);
            goto error;
        }
    }

    usched_mtx_unlock(&g_driversLock);
    return OS_EOK;

error:
    __ForgetDriverDevice(selected, deviceId);
    usched_mtx_unlock(&g_driversLock);
    return status;
}

static struct __DmDriver*
__GetDriver(
    _In_ uuid_t id)
{
    struct __DmDriver* result = NULL;
    struct __DmDriver* driver;

    usched_mtx_lock(&g_driversLock);
    foreach (i, &g_drivers) {
        driver = i->value;
        if (driver->Id == id) {
            result = driver;
            break;
        }
    }
    usched_mtx_unlock(&g_driversLock);
    return result;
}

static void
__SubscribeToDriver(
    _In_ struct __DmDriver* driver)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(driver->Handle);
    int                      status;
    TRACE("__SubscribeToDriver(driver=%u, handle=%u)", driver->Id, driver->Handle);

    status = ctt_driver_subscribe(GetGrachtClient(), &msg.base);
    if (status != 0) {
        WARNING("__SubscribeToDriver: subscribing to driver %u failed: %i", driver->Id, status);
    }
}

static void
__NotifyDevices(
    _In_ struct __DmDriver* driver)
{
    struct __PendingDriverDevice* device;
    oserr_t                       oserr;
    TRACE("__NotifyDevices(driver=%u, handle=%u)", driver->Id, driver->Handle);

    usched_mtx_lock(&driver->DevicesLock);
    foreach (i, &driver->Devices) {
        device = i->value;
        oserr = DmDevicesRegister(driver->Handle, device->Id);
        if (oserr != OS_EOK) {
            WARNING("__NotifyDevices: notifying driver %u of device %u failed: %u",
                    driver->Id, device->Id, oserr);
        }
    }
    usched_mtx_unlock(&driver->DevicesLock);
}

void
DmHandleNotify(
    _In_ uuid_t driverId,
    _In_ uuid_t driverHandle)
{
    struct __DmDriver* driver;
    TRACE("DmHandleNotify(driver=%u, handle=%u)", driverId, driverHandle);

    driver = __GetDriver(driverId);
    if (!driver) {
        WARNING("DmHandleNotify: driver %u with handle %u was not found", driverId, driverHandle);
        return;
    }

    // The ready notification supplies the handle used for driver service calls.
    usched_mtx_lock(&g_driversLock);
    driver->Handle = driverHandle;
    driver->State = DmDriverState_AVAILABLE;
    TRACE("DmHandleNotify: driver %u is available", driverId);
    __SubscribeToDriver(driver);

    // Register devices that were queued while the driver was starting.
    __NotifyDevices(driver);
    usched_mtx_unlock(&g_driversLock);
}
