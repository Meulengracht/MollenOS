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
#include <ds/guid.h>
#include <ds/hashtable.h>
#include <os/dmabuf.h>
#include <os/usched/mutex.h>
#include <os/usched/cond.h>
#include <os/usched/rwlock.h>
#include <os/types/file.h>
#include <vfs/interface.h>
#include <vfs/stat.h>

struct VFS {
    uuid_t                 ID;
    guid_t                 Guid;
    void*                  Data;
    struct VFSStorage*     Storage;
    struct VFSInterface*   Interface;
    struct VFSNode*        Root;
    struct usched_rwlock   Lock;
    DMAAttachment_t        Buffer;
};

enum VFSNodeType {
    VFS_NODE_TYPE_REGULAR    = 0,
    VFS_NODE_TYPE_MOUNTPOINT = 0x1,
    VFS_NODE_TYPE_FILESYSTEM = 0x2
};

struct VFSNode {
    // The filesystem instance that this node belongs to.
    struct VFS*     FileSystem;
    struct VFSNode* Parent;

    // If this node is cloned from another VFS, this will
    // be non-NULL and pointing to its original node.
    struct VFSNode*      Source;
    mstring_t*           Name;
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

    // Children contains the list of child nodes. This is only relevant if the node
    // is a directory or a filesystem.
    // The type of member is <struct __VFSChild>
    // Requires: write lock on node
    hashtable_t Children;

    // Handles contain the table of handles associated with this node. If the node
    // is cloned (Source being non-NULL), then this member will not be used and the
    // Handles of the original node should be used instead.
    // The type of member is <struct __VFSHandle>
    hashtable_t       Handles;
    struct usched_mtx HandlesLock;

    // Mounts contain a list of nodes where this node is mounted.
    // The type of member is <struct __VFSMount*>
    hashtable_t       Mounts;
    struct usched_mtx MountsLock;
};

struct __VFSMount {
    struct VFSNode* Target;
};

struct __VFSChild {
    mstring_t*      Key;
    struct VFSNode* Node;
};

struct __VFSHandle {
    uuid_t   Id;
    uint32_t AccessKind;
};

enum VFSNodeMode {
    MODE_NONE,
    MODE_READ,
    MODE_WRITE
};

struct VFSNodeHandle {
    uuid_t           Id;
    struct VFSNode*  Node;
    enum VFSNodeMode Mode;
    uint32_t         AccessKind;
    uint64_t         Position;
    void*            Data;
};

static inline bool __NodeIsMountPoint(struct VFSNode* node) {
    if (node->Type == (VFS_NODE_TYPE_MOUNTPOINT | VFS_NODE_TYPE_FILESYSTEM)) {
        return true;
    }
    return false;
}

static inline bool __NodeIsBindMount(struct VFSNode* node) {
    if (node->Type == VFS_NODE_TYPE_MOUNTPOINT) {
        return true;
    }
    return false;
}

static inline bool __NodeIsRegular(struct VFSNode* node) {
    if (node->Type == VFS_NODE_TYPE_REGULAR) {
        return true;
    }
    return false;
}

static inline bool __NodeIsFile(struct VFSNode* node) {
    if (FILE_FLAG_TYPE(node->Stats.Flags) == FILE_FLAG_FILE) {
        return true;
    }
    return false;
}

static inline bool __NodeIsSymlink(struct VFSNode* node) {
    if (FILE_FLAG_TYPE(node->Stats.Flags) & FILE_FLAG_LINK) {
        return true;
    }
    return false;
}

static inline bool __NodeIsDirectory(struct VFSNode* node) {
    if (FILE_FLAG_TYPE(node->Stats.Flags) & FILE_FLAG_DIRECTORY) {
        return true;
    }
    return false;
}

static inline bool __PathIsRoot(mstring_t* path) {
    if (mstr_at(path, 0) == U'/' && mstr_len(path) == 1) {
        return true;
    }
    return false;
}

/**
 * @brief Adds a new node handle to the register.
 * @param handleId
 * @param node
 * @return
 */
extern oserr_t
VFSNodeHandleAdd(
        _In_ uuid_t          handleId,
        _In_ struct VFSNode* node,
        _In_ void*           data,
        _In_ uint32_t        accessKind);

/**
 * @brief Retrieves a file handle, this will automatically acquire one reader lock
 * if this returns OS_EOK. For each call to this function, one call to VFSNodeHandlePut
 * must be called.
 * @param handleId
 * @param handleOut
 * @return
 */
extern oserr_t
VFSNodeHandleGet(
        _In_  uuid_t                 handleId,
        _Out_ struct VFSNodeHandle** handleOut);

/**
 * @brief Releases one reader lock on the handles. This should be called exactly once per
 * call to VFSNodeHandleGet.
 * @param handle
 * @return
 */
extern void
VFSNodeHandlePut(
        _In_ struct VFSNodeHandle* handle);

/**
 * @brief Removes a vfs node handle from the register. This can only be called if the
 * caller already has a reader-lock (from calling VFSNodeHandleGet first). After this
 * call the struct VFSNodeHandle instance is no longer valid and should NOT be used anymore
 * except for a final call to VFSNodeHandlePut.
 * @param handleId
 * @return
 */
extern oserr_t
VFSNodeHandleRemove(
        _In_ uuid_t handleId);

extern oserr_t VFSNodeGet(struct VFS* vfs, mstring_t* path, int followLinks, struct VFSNode** nodeOut);
extern void    VFSNodePut(struct VFSNode* node);

/**
 * @brief Ensures a node is loaded if the node is a directory node. A reader lock
 * must be held on the node.
 * @param node
 * @return
 */
extern oserr_t VFSNodeEnsureLoaded(struct VFSNode* node);

/**
 * @brief VFSNodeFind locates a named entry in a node. If the node is unloaded the node
 * will be loaded. Note: A reader lock must be held on node
 * @param node
 * @param name
 * @param nodeOut
 * @return
 */
extern oserr_t VFSNodeFind(struct VFSNode* node, mstring_t* name, struct VFSNode** nodeOut);

/**
 * @brief Creates a new child in the node. This will create the node on the filesystem as well. A reader lock on the
 * node must be held when calling this function.
 * @param node
 * @param name
 * @param flags
 * @param permissions
 * @param nodeOut
 * @return
 */
extern oserr_t VFSNodeCreateChild(struct VFSNode* node, mstring_t* name, uint32_t flags, uint32_t permissions, struct VFSNode** nodeOut);

/**
 * @brief
 * @param node
 * @param name
 * @param target
 * @param symbolic
 * @param nodeOut
 * @return
 */
extern oserr_t VFSNodeCreateLinkChild(struct VFSNode* node, mstring_t* name, mstring_t* target, int symbolic, struct VFSNode** nodeOut);

/**
 * @brief
 * @param node
 * @param accessKind
 * @param handleOut
 * @return
 */
extern oserr_t VFSNodeOpenHandle(struct VFSNode* node, uint32_t accessKind, uuid_t* handleOut);

#endif //!__VFS_PRIVATE_H__
