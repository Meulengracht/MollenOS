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

oserr_t VFSNodeMount(struct VFS* vfs, struct VFSNode* at, struct VFS* what)
{
    oserr_t osStatus = OsOK;

    usched_rwlock_w_lock(&at->Lock);
    if (!__NodeIsRegular(at)) {
        osStatus = OsInvalidParameters;
        goto exit;
    }

    at->Type     = VFS_NODE_TYPE_MOUNTPOINT | VFS_NODE_TYPE_FILESYSTEM;
    at->TypeData = what;

exit:
    usched_rwlock_w_unlock(&at->Lock);
    return osStatus;
}

oserr_t VFSNodeUnmount(struct VFS* vfs, struct VFSNode* node)
{
    oserr_t osStatus = OsOK;

    usched_rwlock_w_lock(&node->Lock);
    if (!__NodeIsMountPoint(node)) {
        osStatus = OsInvalidParameters;
        goto exit;
    }

    node->Type     = VFS_NODE_TYPE_REGULAR;
    node->TypeData = NULL;

exit:
    usched_rwlock_w_unlock(&node->Lock);
    return osStatus;
}
