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
#include <string.h>
#include <vfs/requests.h>
#include <vfs/vfs.h>
#include "../private.h"

oscode_t VFSNodeStat(struct VFS* vfs, struct VFSRequest* request, struct VFSStat* stat)
{
    struct VFSNode* node;
    MString_t*      nodePath = VFSMakePath(request->parameters.stat_path.path);
    oscode_t      osStatus;

    if (nodePath == NULL) {
        return OsOutOfMemory;
    }

    osStatus = VFSNodeGet(vfs, nodePath, request->parameters.stat_path.follow_links, &node);
    if (osStatus != OsOK) {
        return osStatus;
    }

    memcpy(stat, &node->Stats, sizeof(struct VFSStat));
    osStatus = VFSNodePut(node);
    if (osStatus != OsOK) {
        WARNING("VFSNodeStat failed to put node back (path=%s)", MStringRaw(nodePath));
    }

    MStringDestroy(nodePath);
    return OsOK;
}

oscode_t VFSNodeStatFs(struct VFS* vfs, struct VFSRequest* request, struct VFSStatFS* stat)
{
    struct VFSNode* node;
    MString_t*      nodePath = VFSMakePath(request->parameters.stat_path.path);
    oscode_t      osStatus, osStatus2;

    if (nodePath == NULL) {
        return OsOutOfMemory;
    }

    osStatus = VFSNodeGet(vfs, nodePath, request->parameters.stat_path.follow_links, &node);
    if (osStatus != OsOK) {
        return osStatus;
    }

    // store the return value, so we can return the result of that instead of
    // returning the result of cleanup
    osStatus = node->FileSystem->Module->Operations.Stat(&node->FileSystem->Base, stat);

    osStatus2 = VFSNodePut(node);
    if (osStatus2 != OsOK) {
        WARNING("VFSNodeStat failed to put node back (path=%s)", MStringRaw(nodePath));
    }

    MStringDestroy(nodePath);
    return osStatus;
}

oscode_t VFSNodeStatStorage(struct VFS* vfs, struct VFSRequest* request, StorageDescriptor_t* stat)
{
    struct VFSNode* node;
    MString_t*      nodePath = VFSMakePath(request->parameters.stat_path.path);
    oscode_t      osStatus;

    if (nodePath == NULL) {
        return OsOutOfMemory;
    }

    osStatus = VFSNodeGet(vfs, nodePath, request->parameters.stat_path.follow_links, &node);
    if (osStatus != OsOK) {
        return osStatus;
    }

    memcpy(stat, &node->FileSystem->Base.Storage, sizeof(StorageDescriptor_t));
    osStatus = VFSNodePut(node);
    if (osStatus != OsOK) {
        WARNING("VFSNodeStat failed to put node back (path=%s)", MStringRaw(nodePath));
    }

    MStringDestroy(nodePath);
    return OsOK;
}
