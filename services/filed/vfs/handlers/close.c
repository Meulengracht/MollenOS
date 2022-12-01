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

#include <ddk/handle.h>
#include <ddk/utils.h>
#include <vfs/vfs.h>
#include "../private.h"

oserr_t VFSNodeClose(struct VFS* vfs, uuid_t handleID)
{
    struct VFSNodeHandle* handle;
    struct VFSNode*       node;
    void*                 data;
    oserr_t               osStatus;
    _CRT_UNUSED(vfs);

    osStatus = VFSNodeHandleGet(handleID, &handle);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    // store data we need from the handle
    node = handle->Node;
    data = handle->Data;

    osStatus = VFSNodeHandleRemove(handleID);
    if (osStatus != OS_EOK) {
        return osStatus;
    }
    VFSNodeHandlePut(handle);

    // Start out by closing the handle
    usched_mtx_lock(&node->HandlesLock);
    osStatus = node->FileSystem->Interface->Operations.Close(
            node->FileSystem->Interface,
            node->FileSystem->Data,
            data
    );
    if (osStatus != OS_EOK) {
        WARNING("VFSNodeClose failed to close the underlying handle");
    }

    // Remove the handle from the node
    (void)hashtable_remove(
            &node->Handles,
            &(struct __VFSHandle) {
                    .Id = handleID
            }
    );
    usched_mtx_unlock(&node->HandlesLock);
    return OSHandleDestroy(handleID);
}
