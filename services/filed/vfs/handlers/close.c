
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
#include <os/handle.h>
#include <os/grants.h>
#include <vfs/vfs.h>
#include "../private.h"

oserr_t
VFSNodeClose(
    _In_ struct VFS* vfs,
    _In_ uuid_t      handleID)
{
    struct VFSNodeHandle* handle;
    struct VFSNode*       node;
    OSHandle_t            initialReference;
    uuid_t                tokenControlHandleID;
    void*                 data;
    oserr_t               oserr;

    _CRT_UNUSED(vfs);
    oserr = VFSNodeHandleGet(handleID, &handle);
    if (oserr != OS_EOK) {
        return oserr;
    }

    node = handle->Node;
    data = handle->Data;
    tokenControlHandleID = handle->TokenControlHandleID;

    // Removal waits for existing Get/Put users before invalidating the record.
    // Never wait for table removal while holding the node's HandlesLock: open
    // adds its record while holding that lock.
    oserr = VFSNodeHandleRemove(handleID);
    VFSNodeHandlePut(handle);
    if (oserr != OS_EOK) {
        return oserr;
    }

    usched_mtx_lock(&node->HandlesLock);
    oserr = node->FileSystem->Interface->Operations.Close(
        node->FileSystem->Interface, node->FileSystem->Data, data
    );
    if (oserr != OS_EOK) {
        WARNING("VFSNodeClose failed to close filesystem state: %u", oserr);
    }
    hashtable_remove(&node->Handles, &(struct __VFSHandle) { .Id = handleID });
    usched_mtx_unlock(&node->HandlesLock);

    // Internal opens still have their first reference. Client handoffs have
    // already released it, and final cleanup IDs do not themselves give a grant.
    if (OSHandlesFind(handleID, &initialReference) == OS_EOK) {
        OSHandleDestroy(&initialReference);
    }
    if (tokenControlHandleID != UUID_INVALID) {
        (void)OSGrantTokenRevoke(tokenControlHandleID);
    }
    return OS_EOK;
}
