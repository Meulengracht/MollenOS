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
#include <vfs/vfs.h>
#include "private.h"
#include <string.h>

oserr_t VFSNodeMount(struct VFS* vfs, uuid_t atID, struct VFS* what)
{
    struct VFSNodeHandle* handle;
    oserr_t               oserr;
    _CRT_UNUSED(vfs);

    oserr = VFSNodeHandleGet(atID, &handle);
    if (oserr != OsOK) {
        return oserr;
    }

    usched_rwlock_w_lock(&handle->Node->Lock);
    if (!__NodeIsRegular(handle->Node)) {
        oserr = OsInvalidParameters;
        goto exit;
    }

    handle->Node->Type     = VFS_NODE_TYPE_MOUNTPOINT | VFS_NODE_TYPE_FILESYSTEM;
    handle->Node->TypeData = what;

exit:
    usched_rwlock_w_unlock(&handle->Node->Lock);
    VFSNodeHandlePut(handle);
    return oserr;
}

oserr_t VFSNodeUnmount(struct VFS* vfs, uuid_t directoryHandleID)
{
    struct VFSNodeHandle* handle;
    oserr_t               oserr;
    _CRT_UNUSED(vfs);

    oserr = VFSNodeHandleGet(directoryHandleID, &handle);
    if (oserr != OsOK) {
        return oserr;
    }

    usched_rwlock_w_lock(&handle->Node->Lock);
    if (!__NodeIsMountPoint(handle->Node)) {
        oserr = OsInvalidParameters;
        goto exit;
    }

    handle->Node->Type     = VFS_NODE_TYPE_REGULAR;
    handle->Node->TypeData = NULL;

exit:
    usched_rwlock_w_unlock(&handle->Node->Lock);
    VFSNodeHandlePut(handle);
    return oserr;
}

oserr_t VFSNodeUnmountPath(struct VFS* vfs, mstring_t* path)
{
    struct VFSNode* node;
    oserr_t         oserr;

    oserr = VFSNodeGet(vfs, path, 1, &node);
    if (oserr != OsOK) {
        return oserr;
    }

    usched_rwlock_w_lock(&node->Lock);
    if (!__NodeIsMountPoint(node)) {
        oserr = OsInvalidParameters;
        goto exit;
    }

    node->Type     = VFS_NODE_TYPE_REGULAR;
    node->TypeData = NULL;

exit:
    usched_rwlock_w_unlock(&node->Lock);
    return oserr;
}
