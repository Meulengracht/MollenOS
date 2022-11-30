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
#include <vfs/storage.h>
#include <vfs/vfs.h>
#include "../private.h"

oserr_t VFSNodeStat(struct VFS* vfs, const char* cpath, int followLinks, struct VFSStat* stat)
{
    struct VFSNode* node;
    mstring_t*      nodePath = mstr_path_new_u8(cpath);
    oserr_t         osStatus;

    if (nodePath == NULL) {
        return OS_EOOM;
    }

    osStatus = VFSNodeGet(vfs, nodePath, followLinks, &node);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    memcpy(stat, &node->Stats, sizeof(struct VFSStat));
    VFSNodePut(node);

    mstr_delete(nodePath);
    return OS_EOK;
}

oserr_t VFSNodeStatFs(struct VFS* vfs, const char* cpath, int followLinks, struct VFSStatFS* stat)
{
    struct VFSNode* node;
    mstring_t*      nodePath = mstr_path_new_u8(cpath);
    oserr_t         osStatus;

    if (nodePath == NULL) {
        return OS_EOOM;
    }

    osStatus = VFSNodeGet(vfs, nodePath, followLinks, &node);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    // store the return value, so we can return the result of that instead of
    // returning the result of cleanup
    osStatus = node->FileSystem->Interface->Operations.Stat(
            node->FileSystem->Interface,
            node->FileSystem->Data,
            stat
    );
    VFSNodePut(node);
    mstr_delete(nodePath);
    return osStatus;
}

oserr_t VFSNodeStatStorage(struct VFS* vfs, const char* cpath, int followLinks, StorageDescriptor_t* stat)
{
    struct VFSNode* node;
    mstring_t*      nodePath = mstr_path_new_u8(cpath);
    oserr_t         osStatus;

    if (nodePath == NULL) {
        return OS_EOOM;
    }

    osStatus = VFSNodeGet(vfs, nodePath, followLinks, &node);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    memcpy(stat, &node->FileSystem->Storage->Stats, sizeof(StorageDescriptor_t));
    VFSNodePut(node);
    mstr_delete(nodePath);
    return OS_EOK;
}
