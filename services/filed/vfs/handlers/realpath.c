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
#include <vfs/requests.h>
#include <vfs/vfs.h>
#include "../private.h"

oserr_t VFSNodeRealPath(struct VFS* vfs, struct VFSRequest* request , MString_t** pathOut)
{
    struct VFSNode* node;
    MString_t*      nodePath = VFSMakePath(request->parameters.stat_path.path);
    oserr_t      osStatus;

    if (nodePath == NULL) {
        return OsOutOfMemory;
    }

    osStatus = VFSNodeGet(vfs, nodePath, request->parameters.stat_path.follow_links, &node);
    if (osStatus != OsOK) {
        return osStatus;
    }

    // Now get the true path of the node by not asking for the local path
    // because we want the full (global) path to the node
    *pathOut = VFSNodeMakePath(node, 0);

    osStatus = VFSNodePut(node);
    if (osStatus != OsOK) {
        WARNING("VFSNodeStat failed to put node back (path=%s)", MStringRaw(nodePath));
    }
    MStringDestroy(nodePath);
    return OsOK;
}