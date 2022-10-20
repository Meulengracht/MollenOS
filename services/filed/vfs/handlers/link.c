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
#include <vfs/requests.h>
#include <vfs/vfs.h>
#include "../private.h"

oserr_t VFSNodeLink(struct VFS* vfs, struct VFSRequest* request)
{
    mstring_t* path   = mstr_path_new_u8(request->parameters.link.from);
    mstring_t* target = mstr_new_u8(request->parameters.link.to);
    TRACE("VFSNodeLink(path=%ms, target=%ms)", path, target);

    if (path == NULL || target == NULL) {
        mstr_delete(path);
        mstr_delete(target);
        return OsOutOfMemory;
    }

    // Catch early someone trying to create root as a symlink, however
    // we allow people to symlink *to* root
    if (__PathIsRoot(path)) {
        mstr_delete(path);
        mstr_delete(target);
        return OsInvalidPermissions;
    }

    mstring_t* containingDirectoryPath = mstr_path_dirname(path);
    mstring_t* nodeName                = mstr_path_basename(path);
    mstr_delete(path);

    TRACE("VFSNodeLink 1");
    struct VFSNode* containingDirectory;
    oserr_t         osStatus = VFSNodeGet(
            vfs, containingDirectoryPath,
            1, &containingDirectory);

    mstr_delete(containingDirectoryPath);
    if (osStatus != OsOK) {
        return osStatus;
    }

    // Verify that the node is not already something that exists. I Don't know if
    // we should support overwriting symlinks or ask users explicitly to delete an existing
    // symlink. Of course this would require we verify this node is a symlink already.
    TRACE("VFSNodeLink 2");
    struct VFSNode* node;
    osStatus = VFSNodeFind(containingDirectory, nodeName, &node);
    if (osStatus != OsOK && osStatus != OsNotExists) {
        goto exit;
    } else if (osStatus == OsOK) {
        osStatus = OsExists;
        goto exit;
    }

    TRACE("VFSNodeLink 3");
    osStatus = VFSNodeCreateLinkChild(
            containingDirectory, nodeName, target,
            request->parameters.link.symbolic,
            &node);

exit:
    VFSNodePut(containingDirectory);
    mstr_delete(nodeName);
    mstr_delete(target);
    return osStatus;
}
