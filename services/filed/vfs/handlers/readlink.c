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
#include "../private.h"

oserr_t VFSNodeReadLink(struct VFS* vfs, const char* cpath, mstring_t** linkOut)
{
    mstring_t*      path;
    struct VFSNode* node;
    oserr_t         osStatus;

    path = mstr_new_u8(cpath);
    if (path == NULL) {
        return OS_EOOM;
    }

    osStatus = VFSNodeFind(vfs->Root, path, &node);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    usched_rwlock_r_lock(&node->Lock);
    if (node->Stats.Flags & __FILE_LINK) {
        *linkOut = mstr_clone(node->Stats.LinkTarget);
        osStatus = OS_EOK;
    } else {
        osStatus = OS_ELINKINVAL;
    }
    usched_rwlock_r_unlock(&node->Lock);
    VFSNodePut(node);
    return osStatus;
}
