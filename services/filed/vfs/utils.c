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

#define __TRACE

#include <ddk/handle.h>
#include <ddk/utils.h>
#include <vfs/vfs.h>
#include <stdlib.h>
#include "private.h"

static mstring_t g_rootToken   = mstr_const(U"/");
static mstring_t g_dotToken    = mstr_const(U".");
static mstring_t g_dotdotToken = mstr_const(U"..");

mstring_t* VFSNodeMakePath(struct VFSNode* node, int local)
{
    struct VFSNode* i;
    int             tokenCount = 0;
    mstring_t**     tokens;
    TRACE("VFSNodeMakePath(node=%ms, local=%i)", node->Name, local);

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

static oserr_t __ParseEntries(struct VFSNode* node, void* buffer, size_t length) {
    struct VFSStat* i              = (struct VFSStat*)buffer;
    size_t          bytesAvailable = length;
    struct VFSNode* result;

    while (bytesAvailable) {
        oserr_t osStatus = VFSNodeChildNew(node->FileSystem, node, i, &result);
        if (osStatus != OsOK) {
            return osStatus;
        }
        bytesAvailable -= sizeof(struct VFSStat);
        i++;
    }
    return OsOK;
}

static oserr_t __LoadNode(struct VFSNode* node) {
    struct VFSOperations* ops = &node->FileSystem->Interface->Operations;
    struct VFS*           vfs = node->FileSystem;
    oserr_t               osStatus, osStatus2;
    mstring_t*            nodePath;
    void*                 data;
    TRACE("__LoadNode(node=%ms)", node->Name);

    nodePath = VFSNodeMakePath(node, 1);
    if (nodePath == NULL) {
        return OsOutOfMemory;
    }

    osStatus = ops->Open(vfs->Data, nodePath, &data);
    if (osStatus != OsOK) {
        goto cleanup;
    }

    while (1) {
        size_t read;

        osStatus = ops->Read(vfs->Data, data, vfs->Buffer.handle, vfs->Buffer.buffer, 0, vfs->Buffer.length, &read);
        if (osStatus != OsOK || read == 0) {
            break;
        }

        osStatus = __ParseEntries(node, vfs->Buffer.buffer, read);
        if (osStatus != OsOK) {
            break;
        }
    }

    osStatus2 = ops->Close(vfs->Data, data);
    if (osStatus2 != OsOK) {
        WARNING("__LoadNode failed to cleanup handle with code %u", osStatus2);
    }

cleanup:
    mstr_delete(nodePath);
    return osStatus;
}

oserr_t VFSNodeEnsureLoaded(struct VFSNode* node)
{
    oserr_t osStatus = OsOK;

    if (!node->IsLoaded) {
        usched_rwlock_w_promote(&node->Lock);
        // do another check while holding the lock
        if (!node->IsLoaded) {
            osStatus = __LoadNode(node);
            if (osStatus != OsOK) {
                usched_rwlock_w_demote(&node->Lock);
                return osStatus;
            }
            node->IsLoaded = true;
        }
        usched_rwlock_w_demote(&node->Lock);
    }

    return osStatus;
}

oserr_t VFSNodeFind(struct VFSNode* node, mstring_t* name, struct VFSNode** nodeOut)
{
    struct __VFSChild* result;
    oserr_t            oserr;
    TRACE("VFSNodeFind(node=%ms, name=%ms)", node->Name, name);

    // check once while having the reader lock only, this is a performance optimization,
    // so we don't on following checks acquire the writer lock for nothing
    if (!node->IsLoaded) {
        usched_rwlock_w_promote(&node->Lock);
        // do another check while holding the lock
        if (!node->IsLoaded) {
            oserr = __LoadNode(node);
            if (oserr != OsOK) {
                usched_rwlock_w_demote(&node->Lock);
                return oserr;
            }
            node->IsLoaded = true;
        }
        usched_rwlock_w_demote(&node->Lock);
    }

    result = hashtable_get(&node->Children, &(struct __VFSChild) { .Key = name });
    if (result == NULL) {
        return OsNotExists;
    }

    *nodeOut = result->Node;
    return OsOK;
}

oserr_t VFSNodeCreateChild(struct VFSNode* node, mstring_t* name, uint32_t flags, uint32_t permissions, struct VFSNode** nodeOut)
{
    struct VFSOperations* ops = &node->FileSystem->Interface->Operations;
    struct VFS*           vfs = node->FileSystem;
    struct __VFSChild*    result;
    oserr_t               osStatus, osStatus2;
    mstring_t*            nodePath = VFSNodeMakePath(node, 1);
    void*                 data, *fileData;
    TRACE("VFSNodeCreateChild(node=%ms, name=%ms, flags=0x%x, perms=0x%x)", nodePath, name, flags, permissions);

    if (nodePath == NULL) {
        return OsOutOfMemory;
    }

    usched_rwlock_w_promote(&node->Lock);

    // make sure the first we do is verify it still does not exist
    result = hashtable_get(&node->Children, &(struct __VFSChild) { .Key = name });
    if (result != NULL) {
        usched_rwlock_w_demote(&node->Lock);
        mstr_delete(nodePath);
        *nodeOut = result->Node;
        return OsExists;
    }

    osStatus = ops->Open(vfs->Data, nodePath, &data);
    if (osStatus != OsOK) {
        goto cleanup;
    }

    osStatus = ops->Create(vfs->Data, data, name, 0, flags, permissions, &fileData);
    if (osStatus != OsOK) {
        goto close;
    }

    osStatus = VFSNodeChildNew(node->FileSystem, node, &(struct VFSStat) {
            .Name = name,
            .Size = 0,
            .Owner = 0,
            .Flags = flags,
            .Permissions = permissions
    }, nodeOut);

    osStatus2 = ops->Close(vfs->Data, fileData);
    if (osStatus2 != OsOK) {
        WARNING("VFSNodeCreateChild failed to cleanup handle with code %u", osStatus2);
    }

close:
    osStatus2 = ops->Close(vfs->Data, data);
    if (osStatus2 != OsOK) {
        WARNING("VFSNodeCreateChild failed to cleanup handle with code %u", osStatus2);
    }

cleanup:
    usched_rwlock_w_demote(&node->Lock);
    mstr_delete(nodePath);
    return osStatus;
}

oserr_t VFSNodeCreateLinkChild(struct VFSNode* node, mstring_t* name, mstring_t* target, int symbolic, struct VFSNode** nodeOut)
{
    struct VFSOperations* ops = &node->FileSystem->Interface->Operations;
    struct VFS*           vfs = node->FileSystem;
    struct __VFSChild*    result;
    oserr_t               osStatus, osStatus2;
    mstring_t*            nodePath = VFSNodeMakePath(node, 1);
    void*                 data;

    if (nodePath == NULL) {
        return OsOutOfMemory;
    }

    usched_rwlock_w_promote(&node->Lock);

    // make sure the first we do is verify it still does not exist
    result = hashtable_get(&node->Children, &(struct __VFSChild) { .Key = name });
    if (result != NULL) {
        osStatus = OsExists;
        goto cleanup;
    }

    osStatus = ops->Open(vfs->Data, nodePath, &data);
    if (osStatus != OsOK) {
        goto cleanup;
    }

    osStatus = ops->Link(vfs->Data, data, name, target, symbolic);
    if (osStatus != OsOK) {
        goto close;
    }

    osStatus = VFSNodeChildNew(node->FileSystem, node, &(struct VFSStat) {
            .Name = name,
            .Size = mstr_bsize(target),
            .Owner = 0,
            .Flags = FILE_FLAG_LINK,
            .Permissions = FILE_PERMISSION_READ | FILE_PERMISSION_WRITE | FILE_PERMISSION_EXECUTE
    }, nodeOut);

close:
    osStatus2 = ops->Close(vfs->Data, data);
    if (osStatus2 != OsOK) {
        WARNING("__CreateInNode failed to cleanup handle with code %u", osStatus2);
    }

cleanup:
    usched_rwlock_w_demote(&node->Lock);
    mstr_delete(nodePath);
    return osStatus;
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
    oserr_t                        osStatus;
    uuid_t                         handleId;
    mstring_t*                     nodePath;
    TRACE("VFSNodeOpenHandle(node=%ms)", node->Name);

    nodePath = VFSNodeMakePath(node, 1);
    if (nodePath == NULL) {
        return OsOutOfMemory;
    }

    TRACE("VFSNodeOpenHandle exclusivity check");
    usched_mtx_lock(&node->HandlesLock);

    // Perform a handle exclusivity check before opening new handles.
    context.Success = true;
    context.AccessKind = accessKind;
    hashtable_enumerate(&node->Handles, __VerifyHandleExclusivityEnum, &context);
    if (!context.Success) {
        osStatus = OsInvalidPermissions;
        goto cleanup;
    }

    osStatus = handle_create(&handleId);
    if (osStatus != OsOK) {
        goto cleanup;
    }

    osStatus = node->FileSystem->Interface->Operations.Open(node->FileSystem->Data, nodePath, &data);
    if (osStatus != OsOK) {
        goto cleanup;
    }

    osStatus = VFSNodeHandleAdd(handleId, node, data, accessKind);
    if (osStatus != OsOK) {
        handle_destroy(handleId);
        goto error;
    }

    // Everything OK, we can add a new handle
    hashtable_set(&node->Handles, &(struct __VFSHandle) {
            .Id = handleId,
            .AccessKind = accessKind
    });
    *handleOut = handleId;
    goto cleanup;

error:
    node->FileSystem->Interface->Operations.Close(node->FileSystem->Data, data);

cleanup:
    usched_mtx_unlock(&node->HandlesLock);
    mstr_delete(nodePath);
    return osStatus;
}

oserr_t VFSNodeNewDirectory(struct VFS* vfs, mstring_t* path, uint32_t permissions, struct VFSNode** nodeOut)
{
    struct VFSNode* baseDirectoryNode;
    mstring_t*      directoryPath;
    mstring_t*      directoryName;
    oserr_t         osStatus;
    TRACE("VFSNodeNewDirectory(path=%ms, perms=0x%x)", path, permissions);

    directoryPath = mstr_path_dirname(path);
    if (directoryPath == NULL) {
        return OsOutOfMemory;
    }

    directoryName = mstr_path_basename(path);
    if (directoryName == NULL) {
        mstr_delete(directoryPath);
        return OsOutOfMemory;
    }

    osStatus = VFSNodeGet(vfs, directoryPath, 1, &baseDirectoryNode);
    if (osStatus != OsOK) {
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
    oserr_t     osStatus = OsOK;
    mstring_t** tokens;
    int         tokenCount;
    ENTRY("__GetRelative(path=%ms, followLinks=1)", path, followLinks);

    tokenCount = mstr_path_tokens(path, &tokens);
    if (tokenCount < 0) {
        return OsOutOfMemory;
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
        osStatus = VFSNodeFind(node, tokens[i], &next);
        if (osStatus != OsOK) {
            ERROR("__GetRelative failed to find %ms in %ms", tokens[i], node->Name);
            break;
        }

        // Get a lock on next while we 'process' next
        usched_rwlock_r_lock(&next->Lock);

        // If a node is a symlink, we must resolve that instead if we were charged
        // to follow symlinks
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
            osStatus = __GetRelative(node, next->Stats.LinkTarget, followLinks, &real);
            usched_rwlock_r_unlock(&next->Lock);
            if (osStatus != OsOK) {
                break;
            }

            // At this point, we have a lock on 'node' and 'real'
            next = real;
        } else if (__NodeIsBindMount(next)) {
            struct VFSNode* real = next->TypeData;
            if (real == NULL) {
                // *should* never happen
                ERROR("__GetRelative discovered node having a NULL type-data but marked as bind mount");
                osStatus = OsError;
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
                osStatus = OsError;
                usched_rwlock_r_unlock(&next->Lock);
                break;
            }

            // make sure the new next (real) is locked
            real = fs->Root;
            usched_rwlock_r_lock(&real->Lock);
            usched_rwlock_r_unlock(&next->Lock);
            next = real;
        }

        // Move one level down the tree, so we release the lock on the current
        // node and move to 'next' which is locked by this point
        usched_rwlock_r_unlock(&node->Lock);
        node = next;
    }

    if (osStatus != OsOK) {
        // Don't keep the lock if we failed to get the target node
        usched_rwlock_r_unlock(&node->Lock);
    }

    // When exiting the loop, we must hold a reader lock still on
    // node that needs to be unlocked.
    mstrv_delete(tokens);
    *nodeOut = node;
    EXIT("__GetRelative");
    return osStatus;
}

oserr_t VFSNodeGet(struct VFS* vfs, mstring_t* path, int followLinks, struct VFSNode** nodeOut)
{
    return __GetRelative(vfs->Root, path, followLinks, nodeOut);
}

void VFSNodePut(struct VFSNode* node)
{
    if (node == NULL) {
        return;
    }
    usched_rwlock_r_unlock(&node->Lock);
}
