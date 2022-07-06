/**
 * MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Service Definitions & Structures
 * - This header describes the base service-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */
//#define __TRACE

#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <ddk/service.h>
#include <ddk/device.h>
#include <ddk/utils.h>
#include <threads.h>
#include <assert.h>

static UUId_t SessionServiceId = UUID_INVALID;
static UUId_t DeviceServiceId  = UUID_INVALID;
static UUId_t UsbServiceId     = UUID_INVALID;
static UUId_t ProcessServiceId = UUID_INVALID;
static UUId_t FileServiceId    = UUID_INVALID;
static UUId_t NetServiceId     = UUID_INVALID;

static UUId_t
__GetHandleFromPath(
    _In_ const char* Path)
{
    UUId_t Handle;
    if (Syscall_LookupHandle(Path, &Handle) != OsOK) {
        return UUID_INVALID;
    }
    return Handle;
}

static oscode_t
__WaitForService(
    _In_ thrd_t (*getHandleCallback)(void),
    _In_ size_t timeout)
{
    size_t timeLeft = timeout;
    UUId_t handle   = getHandleCallback();
    if (!timeout) {
        while (handle == UUID_INVALID) {
            thrd_sleepex(10);
            handle = getHandleCallback();
        }
    }
    else {
        while (timeLeft && handle == UUID_INVALID) {
            thrd_sleepex(10);
            timeLeft -= 10;
            handle = getHandleCallback();
        }
    }
    return (handle == UUID_INVALID) ? OsTimeout : OsOK;
}

oscode_t
RegisterPath(
    _In_ const char* Path)
{
    TRACE("RegisterPath(%s) => %u", Path, thrd_current());
    if (!Path) {
        return OsInvalidParameters;
    }
    return Syscall_RegisterHandlePath(thrd_current(), Path);
}

thrd_t GetSessionService(void)
{
    if (SessionServiceId == UUID_INVALID) {
        SessionServiceId = __GetHandleFromPath(SERVICE_SESSION_PATH);
    }
    return (thrd_t)SessionServiceId;
}

thrd_t GetDeviceService(void)
{
    if (DeviceServiceId == UUID_INVALID) {
        DeviceServiceId = __GetHandleFromPath(SERVICE_DEVICE_PATH);
    }
    return (thrd_t)DeviceServiceId;
}

thrd_t GetUsbService(void)
{
    if (UsbServiceId == UUID_INVALID) {
        UsbServiceId = __GetHandleFromPath(SERVICE_USB_PATH);
    }
    return (thrd_t)UsbServiceId;
}

thrd_t GetProcessService(void)
{
    if (ProcessServiceId == UUID_INVALID) {
        ProcessServiceId = __GetHandleFromPath(SERVICE_PROCESS_PATH);
    }
    return (thrd_t)ProcessServiceId;
}

thrd_t GetFileService(void)
{
    if (FileServiceId == UUID_INVALID) {
        FileServiceId = __GetHandleFromPath(SERVICE_FILE_PATH);
    }
    return (thrd_t)FileServiceId;
}

thrd_t GetNetService(void)
{
    if (NetServiceId == UUID_INVALID) {
        NetServiceId = __GetHandleFromPath(SERVICE_NET_PATH);
    }
    return (thrd_t)NetServiceId;
}

oscode_t
WaitForSessionService(
    _In_ size_t Timeout)
{
    return __WaitForService(GetSessionService, Timeout);
}

oscode_t
WaitForDeviceService(
    _In_ size_t Timeout)
{
    return __WaitForService(GetDeviceService, Timeout);
}

oscode_t
WaitForUsbService(
    _In_ size_t Timeout)
{
    return __WaitForService(GetUsbService, Timeout);
}

oscode_t
WaitForProcessService(
    _In_ size_t Timeout)
{
    return __WaitForService(GetProcessService, Timeout);
}

oscode_t
WaitForFileService(
    _In_ size_t Timeout)
{
    return __WaitForService(GetFileService, Timeout);
}

oscode_t
WaitForNetService(
    _In_ size_t Timeout)
{
    return __WaitForService(GetNetService, Timeout);
}
