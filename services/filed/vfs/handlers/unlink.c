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

//#define __TRACE

#include <ddk/utils.h>
#include <vfs/vfs.h>
#include "../private.h"

static oserr_t
__VerifyCanDelete(
        _In_ struct VFSNode* node)
{
    // We do not allow bind mounted nodes to be deleted.
    if (!__NodeIsRegular(node)) {
        return OS_EPERMISSIONS;
    }

    // We do not allow deletion on nodes with children
    if (node->Children.element_count != 0) {
        return OS_EDIRNOTEMPTY;
    }

    // We do not allow deletion on nodes with open handles
    if (node->Handles.element_count != 0) {
        return OS_EBUSY;
    }

    // We do not allow deletion of nodes that are currently mounted
    if (node->Mounts.element_count != 0) {
        return OS_EBUSY;
    }
    return OS_EOK;
}

// __DeleteNode is a helper for deleting a VFS node. The node that has been
// passed must not be locked by the caller, AND the caller must hold a read lock
// on the parent
static oserr_t
__DeleteNode(
        _In_ struct VFSNode* node)
{
    struct VFSOperations* ops;
    struct VFS*           vfs;
    struct VFSNode*       parent;
    oserr_t               oserr;
    mstring_t*            nodePath;
    TRACE("__DeleteNode(node=%ms)", node->Name);

    nodePath = VFSNodeMakePath(node, 1);
    if (nodePath == NULL)  {
        return OS_EOOM;
    }

    ops = &node->FileSystem->Interface->Operations;
    vfs = node->FileSystem;

    // Get a write-lock on the parent node, we must do this to ensure there
    // are no waiters on the node we are deleting.
    parent = node->Parent;
    usched_rwlock_w_promote(&parent->Lock);

    // Get a write-lock on this node
    usched_rwlock_w_lock(&node->Lock);

    // Load node before deletion to make sure the node
    // is up-to-date.
    oserr = VFSNodeEnsureLoaded(node);
    if (oserr != OS_EOK) {
        ERROR("__DeleteNode: failed to ensure %ms was loaded: %u", node->Name, oserr);
        goto error;
    }

    oserr = __VerifyCanDelete(node);
    if (oserr != OS_EOK) {
        ERROR("__DeleteNode: deletion checks failed: %u", oserr);
        goto error;
    }

    // OK at this point we are now allowed to perform the deletion
    oserr = ops->Unlink(vfs->Interface, vfs->Data, nodePath);
    if (oserr != OS_EOK) {
        ERROR("__DeleteNode: failed to delete the underlying node: %u", oserr);
        goto error;
    }

    hashtable_remove(
            &parent->Children,
            &(struct __VFSChild) { .Key = node->Name }
    );
    VFSNodeDestroy(node);
    goto done;

error:
    usched_rwlock_w_unlock(&node->Lock);

done:
    usched_rwlock_w_demote(&parent->Lock);
    mstr_delete(nodePath);
    return oserr;
}

oserr_t VFSNodeUnlink(struct VFS* vfs, const char* cpath)
{
    mstring_t* path;
    TRACE("VFSNodeUnlink(path=%s)", cpath);

    path = mstr_path_new_u8(cpath);
    if (path == NULL) {
        return OS_EOOM;
    }

    // Unlinking root is a special case, as we do not allow that do not
    // need to go through expensive checks
    if (__PathIsRoot(path)) {
        mstr_delete(path); // we don't need the path from this point
        WARNING("VFSNodeUnlink deletion of root was requested, returning no-no");
        return OS_EPERMISSIONS;
    }

    mstring_t* containingDirectoryPath = mstr_path_dirname(path);
    mstring_t* nodeName                = mstr_path_basename(path);
    mstr_delete(path);

    struct VFSNode* containingDirectory;
    oserr_t         oserr = VFSNodeGet(
            vfs,
            containingDirectoryPath,
            1,
            &containingDirectory
    );

    mstr_delete(containingDirectoryPath);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Find the requested entry in the containing folder
    struct VFSNode* node;
    oserr = VFSNodeFind(containingDirectory, nodeName, &node);
    if (oserr != OS_EOK && oserr != OS_ENOENT) {
        goto exit;
    }

    oserr = __DeleteNode(node);

exit:
    VFSNodePut(containingDirectory);
    mstr_delete(nodeName);
    return oserr;
}
