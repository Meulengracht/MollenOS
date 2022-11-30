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

oserr_t VFSNodeRealPath(struct VFS* vfs, const char* cpath, int followLink, mstring_t** pathOut)
{
    struct VFSNode* node;
    mstring_t*      nodePath = mstr_path_new_u8(cpath);
    oserr_t         osStatus;

    if (nodePath == NULL) {
        return OS_EOOM;
    }

    osStatus = VFSNodeGet(vfs, nodePath, followLink, &node);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    // Now get the true path of the node by not asking for the local path
    // because we want the full (global) path to the node
    *pathOut = VFSNodeMakePath(node, 0);
    VFSNodePut(node);
    mstr_delete(nodePath);
    return OS_EOK;
}
