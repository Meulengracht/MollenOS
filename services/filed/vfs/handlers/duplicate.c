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

OsStatus_t VFSNodeDuplicate(struct VFSRequest* request, UUId_t* handleOut)
{
    struct VFSNodeHandle* handle;
    OsStatus_t            osStatus, osStatus2;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    usched_rwlock_r_lock(&handle->Node->Lock);
    osStatus = VFSNodeOpenHandle(handle->Node, handle->AccessKind, handleOut);
    usched_rwlock_r_unlock(&handle->Node->Lock);

    osStatus2 = VFSNodeHandlePut(handle);
    if (osStatus2 != OsOK) {
        WARNING("VFSNodeDuplicate failed to release handle lock");
    }
    return osStatus;
}
