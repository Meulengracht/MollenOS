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
#include <vfs/requests.h>
#include <vfs/vfs.h>
#include "../private.h"

static oserr_t __RemoveHandle(struct VFSNode* node, uuid_t handleId)
{
    struct __VFSHandle* handle;

    usched_mtx_lock(&node->HandlesLock);
    handle = hashtable_remove(&node->Handles, &(struct __VFSHandle) { .Id = handleId });
    usched_mtx_unlock(&node->HandlesLock);

    if (handle == NULL) {
        return OS_ENOENT;
    }
    return OS_EOK;
}

oserr_t VFSNodeClose(struct VFS* vfs, uuid_t handleID)
{
    struct VFSNodeHandle* handle;
    struct VFSNode*       node;
    oserr_t               osStatus;

    osStatus = VFSNodeHandleGet(handleID, &handle);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    node = handle->Node;

    // After this, we can free the handle lock,
    // as we don't need it anymore after retrieving the
    // node instance
    VFSNodeHandlePut(handle);

    // When processes inherit files, they gain additional references for a handle
    // which means we try to destroy the handle first, and only if we were the final
    // call we handle cleanup of this file-handle. This also acts as a barrier for
    // synchronization.
    osStatus = handle_destroy(handleID);
    if (osStatus == OS_EINCOMPLETE) {
        return OS_EOK;
    } else if (osStatus != OS_EOK) {
        return osStatus;
    }

    // Ok last reference was destroyed
    osStatus = VFSNodeHandleRemove(handleID);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    // Also remove the handle from the node
    return __RemoveHandle(node, handleID);
}
