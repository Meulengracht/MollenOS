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
#include <os/handle.h>
#include <os/shm.h>
#include <vfs/vfs.h>
#include <stdlib.h>
#include <string.h>
#include "private.h"

static mstring_t g_rootToken   = mstr_const("/");
static mstring_t g_dotToken    = mstr_const(".");
static mstring_t g_dotdotToken = mstr_const("..");

mstring_t* VFSNodeMakePath(struct VFSNode* node, int local)
{
    struct VFSNode* i;
    int             tokenCount = 0;
    mstring_t**     tokens;
    TRACE("VFSNodeMakePath(node=%ms, local=%i)", node ? node->Name : NULL, local);

    if (node == NULL) {
        return NULL;
    }

    i = node;
    do {
        tokenCount++;
        if (local && !mstr_cmp(&g_rootToken, i->Name)) {
            break;
        }
        i = i->Parent;
    } while (i);

    tokens = malloc(sizeof(mstring_t*) * tokenCount);
    if (tokens == NULL) {
        return NULL;
    }

    // No need to do any combining if we only have one token, then we can
    // simply return that.
    if (tokenCount == 1) {
        return mstr_clone(i->Name);
    }
    int index = 0;
    i = node;
    while (index < tokenCount) {
        tokens[tokenCount - index - 1] = i->Name;
        i = i->Parent;
        index++;
    }

    mstring_t* path = mstr_path_tokens_join(tokens, tokenCount);
    free(tokens);
    return path;
}

static void __ToVFSStat(struct VFSDirectoryEntry* in, struct VFSStat* out)
{
    const char* name = (const char*)((uint8_t*)in + sizeof(struct VFSDirectoryEntry));
    const char* link = (const char*)((uint8_t*)in + sizeof(struct VFSDirectoryEntry) + in->NameLength);

    memset(out, 0, sizeof(struct VFSStat));
    if (in->NameLength) {
        out->Name = mstr_new_u8(name);
    }
    if (in->LinkLength) {
        out->LinkTarget = mstr_new_u8(link);
    }
    out->Owner = in->UserID;
    out->Permissions = in->Permissions;
    out->Flags = in->Flags;
    out->Size = in->Size;

    out->Accessed.Seconds = in->Accessed.Seconds;
    out->Accessed.Nanoseconds = in->Accessed.Nanoseconds;
    out->Modified.Seconds = in->Modified.Seconds;
    out->Modified.Nanoseconds = in->Modified.Nanoseconds;
    out->Created.Seconds = in->Created.Seconds;
    out->Created.Nanoseconds = in->Created.Nanoseconds;
}

static void __CleanupVFSStat(struct VFSStat* stats)
{
    mstr_delete(stats->Name);
    mstr_delete(stats->LinkTarget);
}

static oserr_t __ParseEntries(struct VFSNode* node, void* buffer, size_t length)
{
    uint8_t*        i = buffer;
    size_t          bytesAvailable = length;
    struct VFSNode* result;

    while (bytesAvailable) {
        struct VFSDirectoryEntry* entry = (struct VFSDirectoryEntry*)i;
        size_t                    entryLength = sizeof(struct VFSDirectoryEntry) + entry->NameLength + entry->LinkLength;
        oserr_t                   osStatus;
        struct VFSStat            stats;

        __ToVFSStat(entry, &stats);
        osStatus = VFSNodeChildNew(node->FileSystem, node, &stats, &result);
        __CleanupVFSStat(&stats);
        if (osStatus != OS_EOK) {
            return osStatus;
        }
        bytesAvailable -= entryLength;
        i += entryLength;
    }
    return OS_EOK;
}

static oserr_t __LoadNode(struct VFSNode* node)
{
    struct VFSOperations* ops;
    struct VFS*           vfs;
    oserr_t               osStatus, osStatus2;
    mstring_t*            nodePath;
    void*                 data;

    TRACE("__LoadNode(node=%ms)", node ? node->Name : NULL);
    if (node == NULL) {
        return OS_EINVALPARAMS;
    }

    vfs = node->FileSystem;
    ops = &vfs->Interface->Operations;

    nodePath = VFSNodeMakePath(node, 1);
    if (nodePath == NULL) {
        return OS_EOOM;
    }

    osStatus = ops->Open(vfs->Interface, vfs->Data, nodePath, &data);
    if (osStatus != OS_EOK) {
        TRACE("__LoadNode failed to open nodePath %ms: %u", nodePath);
        goto cleanup;
    }

    while (1) {
        size_t read;

        osStatus = ops->Read(
                vfs->Interface,
                vfs->Data, data,
                vfs->Buffer.ID,
                SHMBuffer(&vfs->Buffer),
                0,
                SHMBufferLength(&vfs->Buffer),
                &read
        );
        if (osStatus != OS_EOK || read == 0) {
            TRACE("__LoadNode done reading entries");
            break;
        }

        osStatus = __ParseEntries(node, SHMBuffer(&vfs->Buffer), read);
        if (osStatus != OS_EOK) {
            break;
        }
    }

    osStatus2 = ops->Close(vfs->Interface, vfs->Data, data);
    if (osStatus2 != OS_EOK) {
        WARNING("__LoadNode failed to cleanup handle with code %u", osStatus2);
    }

cleanup:
    mstr_delete(nodePath);
    return osStatus;
}

// VFSNodeEnsureLoaded loads all directory entries of the given node. If
// the node given is not a directory, then this function is a no-op.
// Assumes that a reader lock is held upon entry of the function.
oserr_t VFSNodeEnsureLoaded(struct VFSNode* node)
{
    oserr_t osStatus;
    TRACE("VFSNodeEnsureLoaded(node=%ms)", node->Name);

    if (node == NULL) {
        return OS_EINVALPARAMS;
    }

    if (!__NodeIsDirectory(node)) {
        return OS_EOK;
    }

    if (node->IsLoaded) {
        return OS_EOK;
    }

    osStatus = OS_EOK;
    usched_rwlock_w_promote(&node->Lock);
    // do another check while holding the lock
    if (!node->IsLoaded) {
        osStatus = __LoadNode(node);
        if (osStatus != OS_EOK) {
            usched_rwlock_w_demote(&node->Lock);
            return osStatus;
        }
        node->IsLoaded = true;
    }
    usched_rwlock_w_demote(&node->Lock);
    return osStatus;
}

oserr_t VFSNodeFind(struct VFSNode* node, mstring_t* name, struct VFSNode** nodeOut)
{
    struct __VFSChild* result;
    oserr_t            oserr;

    TRACE("VFSNodeFind(node=%ms, name=%ms)", node ? node->Name : NULL, name);
    if (node == NULL || name == NULL || nodeOut == NULL) {
        return OS_EINVALPARAMS;
    }

    // check once while having the reader lock only, this is a performance optimization,
    // so we don't on following checks acquire the writer lock for nothing
    oserr = VFSNodeEnsureLoaded(node);
    if (oserr != OS_EOK) {
        return oserr;
    }

    result = hashtable_get(
            &node->Children,
            &(struct __VFSChild) {
                .Key = name
            }
    );
    if (result == NULL) {
        return OS_ENOENT;
    }

    *nodeOut = result->Node;
    return OS_EOK;
}

oserr_t VFSNodeCreateChild(struct VFSNode* node, mstring_t* name, uint32_t flags, uint32_t permissions, struct VFSNode** nodeOut)
{
    struct VFSOperations* ops;
    struct VFS*           vfs;
    struct __VFSChild*    result;
    oserr_t               osStatus, osStatus2;
    mstring_t*            nodePath;
    void*                 parentData;
    void*                 childData;
    TRACE("VFSNodeCreateChild(node=%ms, name=%ms, flags=0x%x, perms=0x%x)",
          node ? node->Name : NULL, name, flags, permissions);

    if (node == NULL || name == NULL || nodeOut == NULL) {
        return OS_EINVALPARAMS;
    }

    vfs = node->FileSystem;
    ops = &vfs->Interface->Operations;
    nodePath = VFSNodeMakePath(node, 1);
    if (nodePath == NULL) {
        return OS_EOOM;
    }

    TRACE("VFSNodeCreateChild nodePath=%ms", nodePath);
    usched_rwlock_w_promote(&node->Lock);

    // make sure the first we do is verify it still does not exist
    result = hashtable_get(&node->Children, &(struct __VFSChild) { .Key = name });
    if (result != NULL) {
        usched_rwlock_w_demote(&node->Lock);
        mstr_delete(nodePath);
        *nodeOut = result->Node;
        return OS_EEXISTS;
    }

    osStatus = ops->Open(vfs->Interface, vfs->Data, nodePath, &parentData);
    if (osStatus != OS_EOK) {
        goto cleanup;
    }

    osStatus = ops->Create(vfs->Interface, vfs->Data, parentData, name, 0, flags, permissions, &childData);
    if (osStatus != OS_EOK) {
        goto close;
    }

    osStatus = VFSNodeChildNew(node->FileSystem, node, &(struct VFSStat) {
            .Name = mstr_clone(name),
            .Size = 0,
            .Owner = 0,
            .Flags = flags,
            .Permissions = permissions
    }, nodeOut);

    osStatus2 = ops->Close(vfs->Interface, vfs->Data, childData);
    if (osStatus2 != OS_EOK) {
        WARNING("VFSNodeCreateChild failed to cleanup handle with code %u", osStatus2);
    }

close:
    osStatus2 = ops->Close(vfs->Interface, vfs->Data, parentData);
    if (osStatus2 != OS_EOK) {
        WARNING("VFSNodeCreateChild failed to cleanup handle with code %u", osStatus2);
    }

cleanup:
    usched_rwlock_w_demote(&node->Lock);
    mstr_delete(nodePath);
    return osStatus;
}

oserr_t VFSNodeCreateLinkChild(struct VFSNode* node, mstring_t* name, mstring_t* target, int symbolic, struct VFSNode** nodeOut)
{
    struct VFSOperations* ops;
    struct VFS*           vfs;
    struct __VFSChild*    result;
    oserr_t               oserr, oserr2;
    mstring_t*            nodePath;
    void*                 data;

    TRACE("VFSNodeCreateLinkChild(node=%ms, name=%ms, symbolic=%i)",
          node ? node->Name : NULL, name, symbolic);
    if (node == NULL || name == NULL || nodeOut == NULL) {
        return OS_EINVALPARAMS;
    }

    vfs = node->FileSystem;
    ops = &vfs->Interface->Operations;
    nodePath = VFSNodeMakePath(node, 1);
    if (nodePath == NULL) {
        return OS_EOOM;
    }

    TRACE("VFSNodeCreateLinkChild nodePath=%ms", nodePath);
    usched_rwlock_w_promote(&node->Lock);

    // make sure the first we do is verify it still does not exist
    result = hashtable_get(&node->Children, &(struct __VFSChild) { .Key = name });
    if (result != NULL) {
        oserr = OS_EEXISTS;
        goto cleanup;
    }

    oserr = ops->Open(vfs->Interface, vfs->Data, nodePath, &data);
    if (oserr != OS_EOK) {
        goto cleanup;
    }

    oserr = ops->Link(vfs->Interface, vfs->Data, data, name, target, symbolic);
    if (oserr != OS_EOK) {
        goto close;
    }

    oserr = VFSNodeChildNew(node->FileSystem, node, &(struct VFSStat) {
            .Name = mstr_clone(name),
            .Size = mstr_bsize(target),
            .Owner = 0,
            .Flags = FILE_FLAG_LINK,
            .Permissions = FILE_PERMISSION_READ | FILE_PERMISSION_WRITE | FILE_PERMISSION_EXECUTE
    }, nodeOut);

close:
    oserr2 = ops->Close(vfs->Interface, vfs->Data, data);
    if (oserr2 != OS_EOK) {
        WARNING("__CreateInNode failed to cleanup handle with code %u", oserr2);
    }

cleanup:
    usched_rwlock_w_demote(&node->Lock);
    mstr_delete(nodePath);
    return oserr;
}

struct __HandleExcCheckContext {
    uint32_t AccessKind;
    bool     Success;
};

static inline bool __IsAccessKindExclusive(uint32_t accessKind)
{
    // Exclusive read access?
    if ((accessKind & (__FILE_READ_ACCESS | __FILE_READ_SHARE)) == __FILE_READ_ACCESS) {
        return true;
    }

    // Exclusive write access?
    if ((accessKind & (__FILE_WRITE_ACCESS | __FILE_WRITE_SHARE)) == __FILE_WRITE_ACCESS) {
        return true;
    }
    return false;
}

static void __VerifyHandleExclusivityEnum(int index, const void* element, void* userContext)
{
    struct __HandleExcCheckContext* context = userContext;
    const struct __VFSHandle*       handle  = element;
    _CRT_UNUSED(index);

    if (context->Success == false) {
        return;
    }

    if (__IsAccessKindExclusive(context->AccessKind)) {
        context->Success = false;
        return;
    }

    if (__IsAccessKindExclusive(handle->AccessKind)) {
        context->Success = false;
        return;
    }
}

oserr_t VFSNodeOpenHandle(struct VFSNode* node, uint32_t accessKind, uuid_t* handleOut)
{
    struct __HandleExcCheckContext context;
    void*                          data;
    oserr_t                        oserr;
    struct __VFSHandle             vfsHandle;
    mstring_t*                     nodePath;

    TRACE("VFSNodeOpenHandle(node=%ms)", node ? node->Name : NULL);
    if (node == NULL || handleOut == NULL) {
        return OS_EINVALPARAMS;
    }

    nodePath = VFSNodeMakePath(node, 1);
    if (nodePath == NULL) {
        return OS_EOOM;
    }

    TRACE("VFSNodeOpenHandle exclusivity check");
    usched_mtx_lock(&node->HandlesLock);

    // Perform a handle exclusivity check before opening new handles.
    context.Success = true;
    context.AccessKind = accessKind;
    hashtable_enumerate(&node->Handles, __VerifyHandleExclusivityEnum, &context);
    if (!context.Success) {
        oserr = OS_EPERMISSIONS;
        goto cleanup;
    }

    oserr = OSHandleCreate(OSHANDLE_NULL, NULL, &vfsHandle.OSHandle);
    if (oserr != OS_EOK) {
        goto cleanup;
    }

    oserr = node->FileSystem->Interface->Operations.Open(
            node->FileSystem->Interface,
            node->FileSystem->Data,
            nodePath,
            &data
    );
    if (oserr != OS_EOK) {
        goto cleanup;
    }

    oserr = VFSNodeHandleAdd(&vfsHandle.OSHandle, node, data, accessKind);
    if (oserr != OS_EOK) {
        OSHandleDestroy(&vfsHandle.OSHandle);
        goto error;
    }

    // Everything OK, we can add a new handle
    vfsHandle.AccessKind = accessKind;
    hashtable_set(&node->Handles, &vfsHandle);
    *handleOut = vfsHandle.OSHandle.ID;
    goto cleanup;

error:
    node->FileSystem->Interface->Operations.Close(
            node->FileSystem->Interface,
            node->FileSystem->Data,
            data
    );

cleanup:
    usched_mtx_unlock(&node->HandlesLock);
    mstr_delete(nodePath);
    return oserr;
}

oserr_t VFSNodeNewDirectory(struct VFS* vfs, mstring_t* path, uint32_t permissions, struct VFSNode** nodeOut)
{
    struct VFSNode* baseDirectoryNode;
    mstring_t*      directoryPath;
    mstring_t*      directoryName;
    oserr_t         osStatus;

    TRACE("VFSNodeNewDirectory(path=%ms, perms=0x%x)", path, permissions);
    if (vfs == NULL || path == NULL || nodeOut == NULL) {
        return OS_EINVALPARAMS;
    }

    directoryPath = mstr_path_dirname(path);
    if (directoryPath == NULL) {
        return OS_EOOM;
    }

    directoryName = mstr_path_basename(path);
    if (directoryName == NULL) {
        mstr_delete(directoryPath);
        return OS_EOOM;
    }

    osStatus = VFSNodeGet(vfs, directoryPath, 1, &baseDirectoryNode);
    if (osStatus != OS_EOK) {
        mstr_delete(directoryPath);
        mstr_delete(directoryName);
        return osStatus;
    }

    osStatus = VFSNodeCreateChild(
            baseDirectoryNode,
            directoryName,
            FILE_FLAG_DIRECTORY,
            permissions,
            nodeOut
    );

    VFSNodePut(baseDirectoryNode);
    mstr_delete(directoryPath);
    mstr_delete(directoryName);
    return osStatus;
}

oserr_t __GetRelative(struct VFSNode* from, mstring_t* path, int followLinks, struct VFSNode** nodeOut)
{
    oserr_t     oserr = OS_EOK;
    mstring_t** tokens;
    int         tokenCount;
    ENTRY("__GetRelative(path=%ms, followLinks=1)", path, followLinks);

    tokenCount = mstr_path_tokens(path, &tokens);
    if (tokenCount < 0) {
        return OS_EOOM;
    }

    struct VFSNode* node = from;
    struct VFSNode* next;
    usched_rwlock_r_lock(&node->Lock);
    for (int i = 0; i < tokenCount; i++) {
        TRACE("__GetRelative token[%i] = %ms", i, tokens[i]);
        // Assumptions on entry of this loop.
        // node => current node, we have a reader lock on this
        // next => not used on entry

        // Handle '.' and '..'
        if (!mstr_cmp(tokens[i], &g_dotToken)) {
            continue;
        } else if (!mstr_cmp(tokens[i], &g_dotdotToken)) {
            if (node->Parent) {
                // Move one level up the tree
                next = node->Parent;
                usched_rwlock_r_lock(&next->Lock);
                usched_rwlock_r_unlock(&node->Lock);
                node = next;
            }
            continue;
        }

        // Find the next entry in the folder. This will automatically handle
        // folder loading and whatnot if the folder is not currently loaded.
        oserr = VFSNodeFind(node, tokens[i], &next);
        if (oserr != OS_EOK) {
            TRACE("__GetRelative failed to find %ms in %ms", tokens[i], node->Name);
            break;
        }

        // Get a lock on next while we 'process' next
        usched_rwlock_r_lock(&next->Lock);

        // If a node is a not a regular file, but instead a redirect, then we must resolve
        // the redirect before continuing with the provided path. We must do this in a loop
        // as it is possible for these redirects to be chained.
        for (;;) {
            if (__NodeIsSymlink(next)) {
                struct VFSNode* real;

                // If we are at last node, then we switch node based on sym or not
                if (i == (tokenCount - 1) && !followLinks) {
                    usched_rwlock_r_unlock(&node->Lock);
                    node = next;
                    break;
                }

                // OK in all other cases we *must* follow the symlink
                // NOTE: we now end up with two reader locks on 'node'
                oserr = __GetRelative(node, next->Stats.LinkTarget, followLinks, &real);
                usched_rwlock_r_unlock(&next->Lock);
                if (oserr != OS_EOK) {
                    break;
                }

                // At this point, we have a lock on 'node' and 'real'
                next = real;
            } else if (__NodeIsBindMount(next)) {
                struct VFSNode* real = next->TypeData;
                if (real == NULL) {
                    // *should* never happen
                    ERROR("__GetRelative discovered node having a NULL type-data but marked as bind mount");
                    oserr = OS_EUNKNOWN;
                    usched_rwlock_r_unlock(&next->Lock);
                    break;
                }

                // make sure the new next (real) is locked
                usched_rwlock_r_lock(&real->Lock);
                usched_rwlock_r_unlock(&next->Lock);
                next = real;
            } else if (__NodeIsMountPoint(next)) {
                struct VFS*     fs = next->TypeData;
                struct VFSNode* real;
                if (fs == NULL) {
                    // *should* never happen
                    ERROR("__GetRelative discovered node having a NULL type-data but marked as mountpoint");
                    oserr = OS_EUNKNOWN;
                    usched_rwlock_r_unlock(&next->Lock);
                    break;
                }

                // make sure the new next (real) is locked
                real = fs->Root;
                usched_rwlock_r_lock(&real->Lock);
                usched_rwlock_r_unlock(&next->Lock);
                next = real;
            } else {
                break;
            }
        }

        // Move one level down the tree, so we release the lock on the current
        // node and move to 'next' which is locked by this point
        usched_rwlock_r_unlock(&node->Lock);
        node = next;
    }

    if (oserr != OS_EOK) {
        // Don't keep the lock if we failed to get the target node
        usched_rwlock_r_unlock(&node->Lock);
    }

    // When exiting the loop, we must hold a reader lock still on
    // node that needs to be unlocked.
    mstrv_delete(tokens);
    *nodeOut = node;
    EXIT("__GetRelative");
    return oserr;
}

oserr_t VFSNodeGet(struct VFS* vfs, mstring_t* path, int followLinks, struct VFSNode** nodeOut)
{
    if (vfs == NULL || path == NULL || nodeOut == NULL) {
        return OS_EINVALPARAMS;
    }
    return __GetRelative(vfs->Root, path, followLinks, nodeOut);
}

void VFSNodePut(struct VFSNode* node)
{
    if (node == NULL) {
        return;
    }
    usched_rwlock_r_unlock(&node->Lock);
}
