/* MollenOS
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
#include <ddk/service.h>
#include <ddk/device.h>
#include <threads.h>
#include <assert.h>

OsStatus_t
RegisterService(
    _In_ UUId_t Alias)
{
    if (!IsProcessModule()) {
        return OsInvalidPermissions;
    }
	return Syscall_RegisterService(Alias);
}

OsStatus_t
IsServiceAvailable(
    _In_ UUId_t Alias)
{
    return Syscall_IsServiceAvailable(Alias);
}


OsStatus_t
WaitForService(
    _In_ UUId_t Alias,
    _In_ size_t Timeout)
{
    size_t     TimeLeft = Timeout;
    OsStatus_t Status   = Syscall_IsServiceAvailable(Alias);
    if (!Timeout) {
        while (Status != OsSuccess) {
            thrd_sleepex(100);
            Status = Syscall_IsServiceAvailable(Alias);
        }
    }
    else {
        while (TimeLeft || Status != OsSuccess) {
            thrd_sleepex(100);
            TimeLeft -= 100;
            Status    = Syscall_IsServiceAvailable(Alias);
        }
    }
    return Status;
}

OsStatus_t
InstallDriver(
    _In_ MCoreDevice_t* Device, 
    _In_ size_t         Length,
    _In_ const void*    DriverBuffer,
    _In_ size_t         DriverBufferLength)
{
    if (!IsProcessModule()) {
        return OsInvalidPermissions;
    }

    assert(Device != NULL);
    assert(Length != 0);
	return Syscall_LoadDriver(Device, Length, DriverBuffer, DriverBufferLength);
}
