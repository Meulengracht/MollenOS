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
#include <vfs/vfs.h>
#include <vfs/vfs_module.h>
#include "private.h"
#include <string.h>
#include <stdlib.h>

static uint64_t __HandlesHash(const void* element);
static int      __HandlesCmp(const void* lh, const void* rh);

static uint64_t __ChildrenHash(const void* element);
static int      __ChildrenCmp(const void* lh, const void* rh);

static uint64_t __MountsHash(const void* element);
static int      __MountsCmp(const void* lh, const void* rh);

static oserr_t
__CreateNode(
        _In_  struct VFS*      vfs,
        _In_  enum VFSNodeType type,
        _In_  struct VFSStat*  stats,
        _Out_ struct VFSNode** nodeOut)
{
    struct VFSNode* node;

    node = malloc(sizeof(struct VFSNode));
    if (node == NULL) {
        return OsOutOfMemory;
    }

    node->FileSystem = vfs;
    node->Parent     = NULL;
    node->Source     = NULL;
    node->Name       = mstr_clone(stats->Name);
    node->Type       = type;
    node->IsLoaded   = false;
    usched_rwlock_init(&node->Lock);
    memcpy(&node->Stats, stats, sizeof(struct VFSStat));

    usched_mtx_init(&node->HandlesLock);
    hashtable_construct(&node->Handles, 0,
                        sizeof(struct __VFSHandle),
                        __HandlesHash, __HandlesCmp);
    hashtable_construct(&node->Children, 0,
                        sizeof(struct __VFSChild),
                        __ChildrenHash, __ChildrenCmp);
    hashtable_construct(&node->Mounts, 0,
                        sizeof(struct __VFSMount),
                        __MountsHash, __MountsCmp);
    *nodeOut = node;
    return OsOK;
}

static void
__StatRootNode(
        _In_ struct VFS*     vfs,
        _In_ struct VFSStat* stat)
{
    stat->ID = 0;
    stat->StorageID = vfs->CommonData->Storage.DeviceID;
    stat->Name = mstr_new_u8("/");
    stat->LinkTarget = NULL;
    stat->Owner = 0;
    stat->Permissions = FILE_PERMISSION_READ | FILE_PERMISSION_WRITE;
    stat->Flags = FILE_FLAG_DIRECTORY;
    stat->Size = 0;

    timespec_get(&stat->Created, TIME_UTC);
    stat->Modified.tv_nsec = stat->Created.tv_nsec; stat->Modified.tv_sec = stat->Created.tv_sec;
    stat->Accessed.tv_nsec = stat->Created.tv_nsec; stat->Accessed.tv_sec = stat->Created.tv_sec;
}

static oserr_t
__CreateRootNode(
        _In_  struct VFS*      vfs,
        _Out_ struct VFSNode** nodeOut)
{
    struct VFSNode* root;

    root = malloc(sizeof(struct VFSNode));
    if (root == NULL) {
        return OsOutOfMemory;
    }

    root->FileSystem = vfs;
    root->Parent     = NULL;
    root->Source     = NULL;
    root->Name       = mstr_new_u8("/");
    root->Type       = VFS_NODE_TYPE_REGULAR;
    root->IsLoaded   = false;
    usched_rwlock_init(&root->Lock);
    __StatRootNode(vfs, &root->Stats);

    usched_mtx_init(&root->HandlesLock);
    hashtable_construct(&root->Handles, 0,
                        sizeof(struct __VFSHandle),
                        __HandlesHash, __HandlesCmp);
    hashtable_construct(&root->Children, 0,
                        sizeof(struct __VFSChild),
                        __ChildrenHash, __ChildrenCmp);
    hashtable_construct(&root->Mounts, 0,
                        sizeof(struct __VFSMount),
                        __MountsHash, __MountsCmp);
    *nodeOut = root;
    return OsOK;
}

static oserr_t
__CreateDMABuffer(
        _In_ struct dma_attachment* attachment)
{
    struct dma_buffer_info buffer;

    buffer.name     = "vfs_fs_buffer";
    buffer.flags    = 0;
    buffer.type     = DMA_TYPE_DRIVER_32;
    buffer.length   = MB(1);
    buffer.capacity = MB(1);
    return dma_create(&buffer, attachment);
}

oserr_t
VFSNew(
        _In_  uuid_t                id,
        _In_  guid_t*               guid,
        _In_  struct VFSModule*     module,
        _In_  struct VFSCommonData* commonData,
        _Out_ struct VFS**          vfsOut)
{
    struct VFS* vfs;
    oserr_t     osStatus;

    vfs = malloc(sizeof(struct VFS));
    if (vfs == NULL) {
        return OsOutOfMemory;
    }
    memset(vfs, 0, sizeof(struct VFS));

    vfs->ID = id;
    memcpy(&vfs->Guid, guid, sizeof(guid_t));
    vfs->CommonData = commonData;
    vfs->Module = module;
    usched_rwlock_init(&vfs->Lock);

    osStatus = __CreateRootNode(vfs, &vfs->Root);
    if (osStatus != OsOK) {
        VFSDestroy(vfs);
        return osStatus;
    }

    osStatus = __CreateDMABuffer(&vfs->Buffer);
    if (osStatus != OsOK) {
        VFSDestroy(vfs);
        return osStatus;
    }
    *vfsOut = vfs;
    return OsOK;
}

void VFSDestroy(struct VFS* vfs)
{
    if (vfs == NULL) {
        return;
    }

    // OK when destryoing a VFS we want to make sure all nodes
    // that are mounted somewhere are unmounted, and we want to
    // make sure all mounts that point to this VFS are unmounted
    // as well.
    // TODO implement this, we need to keep track of mounts

    // Cleanup children of this? If someone has cloned this tree
    // we have to make sure that there are no links back to this

    if (vfs->Buffer.buffer != NULL) {
        dma_detach(&vfs->Buffer);
    }
    VFSNodeDestroy(vfs->Root);
    free(vfs);
}

oserr_t VFSChildNew(struct VFS* parent, struct VFS** childOut)
{
    return OsNotSupported;
}

oserr_t VFSNodeChildNew(struct VFS* vfs, struct VFSNode* node, struct VFSStat* stats, struct VFSNode** nodeOut)
{
    struct VFSNode* result;
    oserr_t         osStatus;

    osStatus = __CreateNode(vfs, VFS_NODE_TYPE_REGULAR, stats, &result);
    if (osStatus != OsOK) {
        return osStatus;
    }

    hashtable_set(&node->Children, &(struct __VFSChild) { .Key = result->Name, .Node = result });
    *nodeOut = result;
    return OsOK;
}

void __CleanupHandle(int index, const void* element, void* userContext)
{
    const struct __VFSHandle* handle = element;
    oserr_t                   osStatus;

    // Unregister any handle with the filesystem
    osStatus = VFSNodeHandleRemove(handle->Id);
    if (osStatus != OsOK) {
        // LOG
    }
}

void __CleanupChild(int index, const void* element, void* userContext)
{
    const struct __VFSChild* child = element;
    VFSNodeDestroy(child->Node);
}

void __CleanupMount(int index, const void* element, void* userContext)
{

}

void VFSNodeDestroy(struct VFSNode* node)
{
    if (node == NULL) {
        return;
    }

    hashtable_enumerate(&node->Handles, __CleanupHandle, NULL);
    hashtable_enumerate(&node->Children, __CleanupChild, NULL);
    hashtable_enumerate(&node->Mounts, __CleanupMount, NULL);

    hashtable_destroy(&node->Handles);
    hashtable_destroy(&node->Children);
    hashtable_destroy(&node->Mounts);

    mstr_delete(node->Name);
    mstr_delete(node->Stats.Name);
    if (node->Stats.LinkTarget != NULL) {
        mstr_delete(node->Stats.LinkTarget);
    }
    free(node);
}

static uint64_t __HandlesHash(const void* element)
{
    const struct __VFSHandle* handle = element;
    return handle->Id;
}

static int __HandlesCmp(const void* lh, const void* rh)
{
    const struct __VFSHandle* handle1 = lh;
    const struct __VFSHandle* handle2 = rh;
    return handle1->Id == handle2->Id ? 0 : 1;
}

static uint64_t __ChildrenHash(const void* element)
{
    const struct __VFSChild* child = element;
    return mstr_hash(child->Key);
}

static int __ChildrenCmp(const void* lh, const void* rh)
{
    const struct __VFSChild* child1 = lh;
    const struct __VFSChild* child2 = rh;
    return child1->Node->Stats.ID == child2->Node->Stats.ID ? 0 : 1;
}

static uint64_t __MountsHash(const void* element)
{
    const struct __VFSMount* child = element;
    return mstr_hash(child->Key);
}

static int __MountsCmp(const void* lh, const void* rh)
{
    const struct __VFSMount* child1 = lh;
    const struct __VFSMount* child2 = rh;
    return mstr_cmp(child1->Key, child2->Key);
}
