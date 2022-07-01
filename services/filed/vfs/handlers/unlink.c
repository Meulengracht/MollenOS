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

static bool __IsPathRoot(MString_t* path)
{
    if (MStringGetCharAt(path, 0) == '/' && MStringLength(path) == 1) {
        return true;
    }
    return false;
}

static bool __NodeIsDirectory(struct VFSNode* node)
{
    if (node->Stats.Flags & FILE_FLAG_DIRECTORY) {
        return true;
    }
    return false;
}

static OsStatus_t __VerifyCanDelete(struct VFSNode* node)
{
    // We do not allow bind mounted nodes to be deleted.
    if (node->Type != VFS_NODE_TYPE_REGULAR) {
        return OsInvalidPermissions;
    }

    // We do not allow deletion on nodes with children
    if (node->Children.element_count != 0) {
        return OsDirectoryNotEmpty;
    }

    // We do not allow deletion on nodes with open handles
    if (node->Handles.element_size != 0) {
        return OsBusy;
    }

    // We do not allow deletion of nodes that are currently mounted
    if (node->Mounts.element_count != 0) {
        return OsBusy;
    }
    return OsOK;
}

// __DeleteNode is a helper for deleting a VFS node. The node that has been
// passed must not be locked by the caller, AND the caller must hold a read lock
// on the parent
static OsStatus_t __DeleteNode(struct VFSNode* node)
{
    struct VFSOperations* ops = &node->FileSystem->Module->Operations;
    struct VFS*           vfs = node->FileSystem;
    struct VFSNode*       parent;
    OsStatus_t            osStatus;
    MString_t*            nodePath = VFSNodeMakePath(node, 1);

    if (nodePath == NULL)  {
        return OsOutOfMemory;
    }

    // Get a write-lock on the parent node, we must do this to ensure there
    // are no waiters on the node we are deleting.
    parent = node->Parent;
    usched_rwlock_w_promote(&parent->Lock);

    // Get a write-lock on this node
    usched_rwlock_r_lock(&node->Lock);

    // Load node before deletion to make sure the node
    // is up-to-date
    osStatus = VFSNodeEnsureLoaded(node);
    if (osStatus != OsOK) {
        goto error;
    }

    osStatus = __VerifyCanDelete(node);
    if (osStatus != OsOK) {
        goto error;
    }

    // OK at this point we are now allowed to perform the deletion
    osStatus = ops->Unlink(&vfs->Base, nodePath);
    if (osStatus != OsOK) {
        goto error;
    }

    hashtable_remove(&parent->Children, &(struct __VFSChild) { .Key = node->Name });
    VFSNodeDestroy(vfs, node);

error:
    usched_rwlock_r_unlock(&node->Lock);
    usched_rwlock_w_demote(&parent->Lock);
    MStringDestroy(nodePath);
    return osStatus;
}

static OsStatus_t __UnlinkDirectory(struct VFS* vfs, struct VFSRequest* request)
{
    struct VFSNode* node;
    OsStatus_t      osStatus;
    MString_t*      path;
    size_t          pathLength;
    int             startIndex;

    path = VFSMakePath(request->parameters.open.path);
    if (path == NULL) {
        return OsOutOfMemory;
    }

    // We do never allow deletion of the root path
    if (__IsPathRoot(path)) {
        MStringDestroy(path);
        return OsInvalidParameters;
    }

    startIndex = 1;
    node       = vfs->Root;
    while (1) {
        int             endIndex = MStringFind(path, '/', startIndex);
        MString_t*      token    = MStringSubString(path, startIndex, (int)pathLength - endIndex); // TODO ehh verify the logic here
        struct VFSNode* child;

        // If we run out of tokens (for instance path ends on '/') then we can assume at this
        // point that we are standing at the directory. So return a handle to the current node
        if (MStringLength(token) == 0) {
            // __DeleteNode expects the node to be unlocked, but we have to handle that
            // we recursively do readlocks on nodes here. So unlock the current node,
            // delete it, and then update node to point to parent, so we don't try to
            // unlock it as the node we gave to __DeleteNode will be invalid after
            // the call to __DeleteNode
            usched_rwlock_r_unlock(&node->Lock);
            osStatus = __DeleteNode(node);
            node = node->Parent;
            break;
        }

        // Acquire a read lock on this node, when we exit we release the entire chain
        usched_rwlock_r_lock(&node->Lock);

        // Next is finding this token inside the current VFSNode
        osStatus = VFSNodeFind(node, token, &child);
        if (osStatus != OsOK) {
            MStringDestroy(token);
            break;
        }

        // Cleanup the token at this point, we don't need it anymore
        MStringDestroy(token);

        // Entry we find must always be a directory
        if (!__NodeIsDirectory(child)) {
            osStatus = OsPathIsNotDirectory;
            break;
        }

        if (endIndex == MSTRING_NOT_FOUND) {
            // So at this point child is valid and points to the target node we were looking
            // for, so we can now acquire a lock on that based on permissions
            osStatus = __DeleteNode(child);
            break;
        } else {
            node = child;
        }
    }

    // release all read locks at this point
    while (node) {
        usched_rwlock_r_unlock(&node->Lock);
        node = node->Parent;
    }

    MStringDestroy(path);
    return osStatus;
}

static OsStatus_t __UnlinkFile(struct VFS* vfs, struct VFSRequest* request)
{
    struct VFSNode* node;
    OsStatus_t      osStatus;
    MString_t*      path;
    size_t          pathLength;
    int             startIndex;

    path = VFSMakePath(request->parameters.open.path);
    if (path == NULL) {
        return OsOutOfMemory;
    }
    pathLength = MStringLength(path);

    // Catch the case where we are trying to delete the root, which we do never
    // allow, but we atleast ask for the correct options, thank you very much
    if (__IsPathRoot(path)) {
        return OsPathIsDirectory;
    }

    startIndex = 1;
    node       = vfs->Root;
    while (1) {
        int             endIndex = MStringFind(path, '/', startIndex);
        MString_t*      token    = MStringSubString(path, startIndex, (int)pathLength - endIndex); // TODO ehh verify the logic here
        struct VFSNode* child;

        // If we run out of tokens, then the path passed to us was
        // a directory path and not a file path.
        if (MStringLength(token) == 0) {
            MStringDestroy(token);
            osStatus = OsPathIsDirectory;
            break;
        }

        // Acquire a read lock on this node, when we exit we release the entire chain
        usched_rwlock_r_lock(&node->Lock);

        // Next is finding this token inside the current VFSNode
        osStatus = VFSNodeFind(node, token, &child);
        if (osStatus != OsOK) {
            MStringDestroy(token);
            break;
        }

        // Cleanup the token at this point, we don't need it anymore
        MStringDestroy(token);

        if (endIndex == MSTRING_NOT_FOUND) {
            // So at this point child is valid and points to the target node we were looking
            // for, so we can now acquire a lock on that based on permissions
            osStatus = __DeleteNode(child);
            break;
        } else if (!__NodeIsDirectory(child)) {
            osStatus = OsPathIsNotDirectory;
            break;
        } else {
            node = child;
        }
    }

    // release all read locks at this point
    while (node) {
        usched_rwlock_r_unlock(&node->Lock);
        node = node->Parent;
    }

    MStringDestroy(path);
    return osStatus;
}

OsStatus_t VFSNodeUnlink(struct VFS* vfs, struct VFSRequest* request)
{
    if (request->parameters.delete_path.options & __FILE_DIRECTORY) {
        return __UnlinkDirectory(vfs, request);
    }
    return __UnlinkFile(vfs, request);
}
