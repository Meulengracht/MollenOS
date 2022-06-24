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

#ifndef __VFS_PRIVATE_H__
#define __VFS_PRIVATE_H__

#include <ddk/filesystem.h>
#include <ds/hashtable.h>
#include <os/dmabuf.h>
#include <os/usched/mutex.h>
#include <os/usched/cond.h>
#include <vfs/vfs_module.h>

struct usched_rwlock {
    struct usched_mtx sync_object;
    struct usched_cnd signal;
    int               readers;
};

static void usched_rwlock_init(struct usched_rwlock* lock)
{
    usched_mtx_init(&lock->sync_object);
    usched_cnd_init(&lock->signal);
    lock->readers = 0;
}

static void usched_rwlock_r_lock(struct usched_rwlock* lock)
{
    usched_mtx_lock(&lock->sync_object);
    lock->readers++;
    usched_mtx_unlock(&lock->sync_object);
}

static void usched_rwlock_r_unlock(struct usched_rwlock* lock)
{
    usched_mtx_lock(&lock->sync_object);
    lock->readers--;
    if (!lock->readers) {
        usched_cnd_notify_one(&lock->signal);
    }
    usched_mtx_unlock(&lock->sync_object);
}

static void usched_rwlock_w_promote(struct usched_rwlock* lock)
{
    usched_mtx_lock(&lock->sync_object);
    if (--(lock->readers)) {
        usched_cnd_wait(&lock->signal, &lock->sync_object);
    }
}

static void usched_rwlock_w_demote(struct usched_rwlock* lock)
{
    lock->readers++;
    usched_mtx_unlock(&lock->sync_object);
}

static void usched_rwlock_w_lock(struct usched_rwlock* lock)
{
    usched_mtx_lock(&lock->sync_object);
    if (lock->readers) {
        usched_cnd_wait(&lock->signal, &lock->sync_object);
    }
}

static void usched_rwlock_w_unlock(struct usched_rwlock* lock)
{
    usched_mtx_unlock(&lock->sync_object);
    usched_cnd_notify_one(&lock->signal);
}

struct VFS {
    FileSystemBase_t      Base;
    struct VFSModule*     Module;
    struct VFSNode*       Root;
    struct usched_rwlock  Lock;
    struct dma_attachment Buffer;
};

enum VFSNodeType {
    VFS_NODE_TYPE_REGULAR,
    VFS_NODE_TYPE_MOUNTPOINT,
    VFS_NODE_TYPE_FILESYSTEM
};

struct VFSNode {
    // The filesystem instance that this node belongs to.
    struct VFS*     FileSystem;
    struct VFSNode* Parent;

    // If this node is cloned from another VFS, this will
    // be non-NULL and pointing to its original node.
    struct VFSNode*      Source;
    MString_t*           Name;
    enum VFSNodeType     Type;
    bool                 IsLoaded;
    struct usched_rwlock Lock;
    struct VFSStat       Stats;

    // TypeData contains data related to the type of the node.
    // If the node is a filesystem node, this will contain a pointer
    // to the new <struct VFS> instance which describes the filesystem.
    // Any children of this node will then be children of that filesystem.
    //
    // If this node is a mountpoint, then some other VFSNode
    // is mounted on top of this. This means that this node is
    // transparent, and that this member will contain a pointer to the source
    // <struct VFSNode>.
    void* TypeData;

    // Handles contain the table of handles associated with this node. If the node
    // is cloned (Source being non-NULL), then this member will not be used and the
    // Handles of the original node should be used instead.
    // The type of member is <struct __VFSHandle>
    hashtable_t       Handles;
    struct usched_mtx HandlesLock;

    // Children contains the list of child nodes. This is only relevant if the node
    // is a directory or a filesystem.
    // The type of member is <struct __VFSChild>
    // Requires: write lock on node
    hashtable_t Children;

    // Mounts contain a list of nodes where this node is mounted.
    // The type of member is <struct VFSMount*>
    hashtable_t Mounts;
};

struct __VFSChild {
    MString_t*      Key;
    struct VFSNode* Node;
};

struct __VFSHandle {
    UUId_t   Id;
    uint32_t AccessKind;
};

struct VFSNodeHandle {
    UUId_t          Id;
    struct VFSNode* Node;
    uint32_t        AccessKind;
};

extern OsStatus_t
VFSNodeHandleAdd(
        _In_ UUId_t          handleId,
        _In_ struct VFSNode* node);

extern OsStatus_t
VFSNodeHandleFind(
        _In_  UUId_t           handleId,
        _Out_ struct VFSNode** nodeOut);

extern OsStatus_t
VFSNodeHandleRemove(
        _In_ UUId_t handleId);

extern MString_t* VFSMakePath(const char* path);

extern MString_t* VFSNodeMakePath(struct VFSNode* node);

/**
 * @brief Ensures a node is loaded if the node is a directory node. A reader lock
 * must be held on the node.
 * @param node
 * @return
 */
extern OsStatus_t VFSNodeEnsureLoaded(struct VFSNode* node);

/**
 * @brief VFSNodeFind locates a named entry in a node. If the node is unloaded the node
 * will be loaded. Note: A reader lock must be held on node
 * @param node
 * @param name
 * @param nodeOut
 * @return
 */
extern OsStatus_t VFSNodeFind(struct VFSNode* node, MString_t* name, struct VFSNode** nodeOut);

/**
 * @brief
 * @param node
 * @param name
 * @param flags
 * @param permissions
 * @param nodeOut
 * @return
 */
extern OsStatus_t VFSNodeCreateChild(struct VFSNode* node, MString_t* name, uint32_t flags, uint32_t permissions, struct VFSNode** nodeOut);

#endif //!__VFS_PRIVATE_H__
