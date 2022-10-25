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

#include <vfs/requests.h>
#include <vfs/vfs.h>
#include "../private.h"

oserr_t VFSNodeDuplicate(struct VFSRequest* request, uuid_t* handleOut)
{
    struct VFSNodeHandle* handle;
    oserr_t               osStatus;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    usched_rwlock_r_lock(&handle->Node->Lock);
    osStatus = VFSNodeOpenHandle(handle->Node, handle->AccessKind, handleOut);
    usched_rwlock_r_unlock(&handle->Node->Lock);
    VFSNodeHandlePut(handle);
    return osStatus;
}
