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
#include <ddk/service.h>
#include <ddk/utils.h>
#include <threads.h>
#include <assert.h>

static uuid_t g_sessionServiceId = UUID_INVALID;
static uuid_t g_deviceServiceId  = UUID_INVALID;
static uuid_t g_usbServiceId     = UUID_INVALID;
static uuid_t g_processServiceId = UUID_INVALID;
static uuid_t g_fileServiceId    = UUID_INVALID;
static uuid_t g_netServiceId     = UUID_INVALID;

static uuid_t
__GetHandleFromPath(
    _In_ const char* Path)
{
    uuid_t Handle;
    if (Syscall_LookupHandle(Path, &Handle) != OsOK) {
        return UUID_INVALID;
    }
    return Handle;
}

static oserr_t
__WaitForService(
    _In_ uuid_t (*getHandleCallback)(void),
    _In_ size_t timeout)
{
    size_t timeLeft = timeout;
    uuid_t handle   = getHandleCallback();
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

oserr_t
RegisterPath(
    _In_ const char* Path)
{
    TRACE("RegisterPath(%s) => %u", Path, ThreadsCurrentId());
    if (!Path) {
        return OsInvalidParameters;
    }
    return Syscall_RegisterHandlePath(ThreadsCurrentId(), Path);
}

uuid_t GetSessionService(void)
{
    if (g_sessionServiceId == UUID_INVALID) {
        g_sessionServiceId = __GetHandleFromPath(SERVICE_SESSION_PATH);
    }
    return g_sessionServiceId;
}

uuid_t GetDeviceService(void)
{
    if (g_deviceServiceId == UUID_INVALID) {
        g_deviceServiceId = __GetHandleFromPath(SERVICE_DEVICE_PATH);
    }
    return g_deviceServiceId;
}

uuid_t GetUsbService(void)
{
    if (g_usbServiceId == UUID_INVALID) {
        g_usbServiceId = __GetHandleFromPath(SERVICE_USB_PATH);
    }
    return g_usbServiceId;
}

uuid_t GetProcessService(void)
{
    if (g_processServiceId == UUID_INVALID) {
        g_processServiceId = __GetHandleFromPath(SERVICE_PROCESS_PATH);
    }
    return g_processServiceId;
}

uuid_t GetFileService(void)
{
    if (g_fileServiceId == UUID_INVALID) {
        g_fileServiceId = __GetHandleFromPath(SERVICE_FILE_PATH);
    }
    return g_fileServiceId;
}

uuid_t GetNetService(void)
{
    if (g_netServiceId == UUID_INVALID) {
        g_netServiceId = __GetHandleFromPath(SERVICE_NET_PATH);
    }
    return g_netServiceId;
}

oserr_t
WaitForSessionService(
    _In_ size_t Timeout)
{
    return __WaitForService(GetSessionService, Timeout);
}

oserr_t
WaitForDeviceService(
    _In_ size_t Timeout)
{
    return __WaitForService(GetDeviceService, Timeout);
}

oserr_t
WaitForUsbService(
    _In_ size_t Timeout)
{
    return __WaitForService(GetUsbService, Timeout);
}

oserr_t
WaitForProcessService(
    _In_ size_t Timeout)
{
    return __WaitForService(GetProcessService, Timeout);
}

oserr_t
WaitForFileService(
    _In_ size_t Timeout)
{
    return __WaitForService(GetFileService, Timeout);
}

oserr_t
WaitForNetService(
    _In_ size_t Timeout)
{
    return __WaitForService(GetNetService, Timeout);
}
