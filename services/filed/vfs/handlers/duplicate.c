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

#include <vfs/vfs.h>
#include "../private.h"

oserr_t VFSNodeDuplicate(uuid_t handle, uuid_t* handleOut)
{
    struct VFSNodeHandle* nodeHandle;
    oserr_t               osStatus;

    osStatus = VFSNodeHandleGet(handle, &nodeHandle);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    usched_rwlock_r_lock(&nodeHandle->Node->Lock);
    osStatus = VFSNodeOpenHandle(nodeHandle->Node, nodeHandle->AccessKind, handleOut);
    usched_rwlock_r_unlock(&nodeHandle->Node->Lock);
    VFSNodeHandlePut(nodeHandle);
    return osStatus;
}
