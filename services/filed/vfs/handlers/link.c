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

#define __TRACE

#include <ddk/utils.h>
#include <vfs/vfs.h>
#include "../private.h"

oserr_t VFSNodeLink(struct VFS* vfs, const char* cfrom, const char* cto, bool symbolic)
{
    mstring_t* path   = mstr_path_new_u8(cfrom);
    mstring_t* target = mstr_new_u8(cto);
    TRACE("VFSNodeLink(path=%ms, target=%ms)", path, target);

    if (path == NULL || target == NULL) {
        mstr_delete(path);
        mstr_delete(target);
        return OS_EOOM;
    }

    // Catch early someone trying to create root as a symlink, however
    // we allow people to symlink *to* root
    if (__PathIsRoot(path)) {
        mstr_delete(path);
        mstr_delete(target);
        return OS_EPERMISSIONS;
    }

    mstring_t* containingDirectoryPath = mstr_path_dirname(path);
    mstring_t* nodeName                = mstr_path_basename(path);
    mstr_delete(path);

    struct VFSNode* containingDirectory;
    oserr_t         osStatus = VFSNodeGet(
            vfs, containingDirectoryPath,
            1, &containingDirectory);

    mstr_delete(containingDirectoryPath);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    // Verify that the node is not already something that exists. I Don't know if
    // we should support overwriting symlinks or ask users explicitly to delete an existing
    // symlink. Of course this would require we verify this node is a symlink already.
    struct VFSNode* node;
    osStatus = VFSNodeFind(containingDirectory, nodeName, &node);
    if (osStatus != OS_EOK && osStatus != OS_ENOENT) {
        goto exit;
    } else if (osStatus == OS_EOK) {
        osStatus = OS_EEXISTS;
        goto exit;
    }

    osStatus = VFSNodeCreateLinkChild(
            containingDirectory, nodeName, target,
            symbolic,
            &node);

exit:
    VFSNodePut(containingDirectory);
    mstr_delete(nodeName);
    mstr_delete(target);
    return osStatus;
}
