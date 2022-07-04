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
 *
 */

#include <ddk/utils.h>
#include <vfs/requests.h>
#include <vfs/vfs.h>
#include "../private.h"

OsStatus_t VFSNodeFlush(struct VFSRequest* request)
{
    struct VFSNodeHandle* handle;
    struct VFS*           nodeVfs;
    OsStatus_t            osStatus, osStatus2;
    LargeUInteger_t       position;

    position.u.LowPart  = request->parameters.seek.position_low;
    position.u.HighPart = request->parameters.seek.position_high;

    osStatus = VFSNodeHandleGet(request->parameters.seek.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    // TODO implement
    osStatus = OsNotSupported;

    osStatus2 = VFSNodeHandlePut(handle);
    if (osStatus2 != OsOK) {
        WARNING("VFSNodeFlush failed to release handle lock");
    }
    return osStatus;
}
