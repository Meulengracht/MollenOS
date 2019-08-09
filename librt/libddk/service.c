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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Service Definitions & Structures
 * - This header describes the base service-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <ddk/services/service.h>
#include <ddk/device.h>
#include <threads.h>
#include <assert.h>

static UUId_t SessionServiceId = UUID_INVALID;
static UUId_t DeviceServiceId  = UUID_INVALID;
static UUId_t UsbServiceId     = UUID_INVALID;
static UUId_t ProcessServiceId = UUID_INVALID;
static UUId_t FileServiceId    = UUID_INVALID;

static UUId_t
GetHandleFromPath(
    _In_ const char* Path)
{
    UUId_t Handle;
    if (Syscall_LookupHandle(Path, &Handle) != OsSuccess) {
        return UUID_INVALID;
    }
    return Handle;
}

static OsStatus_t
WaitForService(
    _In_ thrd_t (*Callback)(void),
    _In_ size_t Timeout)
{
    size_t TimeLeft = Timeout;
    UUId_t Handle   = Callback();
    if (!Timeout) {
        while (Handle == UUID_INVALID) {
            thrd_sleepex(100);
            Handle = Callback();
        }
    }
    else {
        while (TimeLeft || Handle == UUID_INVALID) {
            thrd_sleepex(100);
            TimeLeft -= 100;
            Handle   = Callback();
        }
    }
    return (Handle == UUID_INVALID) ? OsTimeout : OsSuccess;
}

OsStatus_t 
RegisterPath(
    _In_ const char* Path)
{
    if (!Path) {
        return OsInvalidParameters;
    }
    return Syscall_RegisterHandlePath(thrd_current(), Path);
}

thrd_t GetSessionService(void)
{
    if (SessionServiceId == UUID_INVALID) {
        SessionServiceId = GetHandleFromPath(SERVICE_SESSION_PATH);
    }
    return (thrd_t)SessionServiceId;
}

thrd_t GetDeviceService(void)
{
    if (DeviceServiceId == UUID_INVALID) {
        DeviceServiceId = GetHandleFromPath(SERVICE_DEVICE_PATH);
    }
    return (thrd_t)DeviceServiceId;
}

thrd_t GetUsbService(void)
{
    if (UsbServiceId == UUID_INVALID) {
        UsbServiceId = GetHandleFromPath(SERVICE_USB_PATH);
    }
    return (thrd_t)UsbServiceId;
}

thrd_t GetProcessService(void)
{
    if (ProcessServiceId == UUID_INVALID) {
        ProcessServiceId = GetHandleFromPath(SERVICE_PROCESS_PATH);
    }
    return (thrd_t)ProcessServiceId;
}

thrd_t GetFileService(void)
{
    if (FileServiceId == UUID_INVALID) {
        FileServiceId = GetHandleFromPath(SERVICE_FILE_PATH);
    }
    return (thrd_t)FileServiceId;
}

OsStatus_t
WaitForSessionService(
    _In_ size_t Timeout)
{
    return WaitForService(GetSessionService, Timeout);
}

OsStatus_t
WaitForDeviceService(
    _In_ size_t Timeout)
{
    return WaitForService(GetDeviceService, Timeout);
}

OsStatus_t
WaitForUsbService(
    _In_ size_t Timeout)
{
    return WaitForService(GetUsbService, Timeout);
}

OsStatus_t
WaitForProcessService(
    _In_ size_t Timeout)
{
    return WaitForService(GetProcessService, Timeout);
}

OsStatus_t
WaitForFileService(
    _In_ size_t Timeout)
{
    return WaitForService(GetFileService, Timeout);
}

OsStatus_t
InstallDriver(
    _In_ MCoreDevice_t* Device, 
    _In_ size_t         Length,
    _In_ const void*    DriverBuffer,
    _In_ size_t         DriverBufferLength)
{
    assert(Device != NULL);
    assert(Length != 0);
	return Syscall_LoadDriver(Device, Length, DriverBuffer, DriverBufferLength);
}
