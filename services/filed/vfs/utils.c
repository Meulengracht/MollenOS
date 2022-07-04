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

#include <ddk/handle.h>
#include <ddk/utils.h>
#include <vfs/vfs.h>
#include "private.h"

MString_t* VFSMakePath(const char* path)
{

}

MString_t* VFSNodeMakePath(struct VFSNode* node, int local)
{
    // iterate back untill we hit '/'
    struct VFSNode* i = node;
    MString_t*      root = MStringCreate("/", StrUTF8);
    MString_t*      path;

    // TODO const mstring support
    if (root == NULL) {
        return NULL;
    }

    // Now we are at root, we have spooled back to start, and we will build
    // up the full path from this point
    path = MStringCreate(NULL, StrUTF8);
    if (path == NULL) {
        MStringDestroy(root);
        return NULL;
    }

    while (i) {
        MStringPrepend(path, i->Name);
        i = i->Parent;
    }

    MStringDestroy(root);
    return path;
}

static OsStatus_t __AddEntry(struct VFSNode* node, struct VFSStat* entry)
{
    struct VFSNode* entryNode;
    OsStatus_t      osStatus;

    osStatus = VFSNodeChildNew(node->FileSystem, node, entry, &entryNode);
    if (osStatus != OsOK) {
        return osStatus;
    }
    return OsOK;
}

static OsStatus_t __ParseEntries(struct VFSNode* node, void* buffer, size_t length) {
    struct VFSStat* i              = (struct VFSStat*)buffer;
    size_t          bytesAvailable = length;

    while (bytesAvailable) {
        OsStatus_t osStatus = __AddEntry(node, i);
        if (osStatus != OsOK) {
            return osStatus;
        }
        bytesAvailable -= sizeof(struct VFSStat);
        i++;
    }
    return OsOK;
}

static OsStatus_t __LoadNode(struct VFSNode* node) {
    struct VFSOperations* ops = &node->FileSystem->Module->Operations;
    struct VFS*           vfs = node->FileSystem;
    OsStatus_t            osStatus, osStatus2;
    MString_t*            nodePath = VFSNodeMakePath(node, 1);
    void*                 data;

    if (nodePath == NULL) {
        return OsOutOfMemory;
    }

    osStatus = ops->Open(&vfs->Base, nodePath, &data);
    if (osStatus != OsOK) {
        goto cleanup;
    }

    while (1) {
        size_t read;

        osStatus = ops->Read(&vfs->Base, data, vfs->Buffer.handle, vfs->Buffer.buffer, 0, vfs->Buffer.length, &read);
        if (osStatus != OsOK || read == 0) {
            break;
        }

        osStatus = __ParseEntries(node, vfs->Buffer.buffer, read);
        if (osStatus != OsOK) {
            break;
        }
    }

    osStatus2 = ops->Close(&vfs->Base, data);
    if (osStatus2 != OsOK) {
        WARNING("__LoadNode failed to cleanup handle with code %u", osStatus2);
    }

    cleanup:
    MStringDestroy(nodePath);
    return osStatus;
}

OsStatus_t VFSNodeEnsureLoaded(struct VFSNode* node)
{
    OsStatus_t osStatus = OsOK;

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

OsStatus_t VFSNodeFind(struct VFSNode* node, MString_t* name, struct VFSNode** nodeOut)
{
    struct __VFSChild* result;
    OsStatus_t         osStatus;

    // check once while having the reader lock only, this is a performance optimization,
    // so we don't on following checks acquire the writer lock for nothing
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

    result = hashtable_get(&node->Children, &(struct __VFSChild) { .Key = name });
    if (result == NULL) {
        return OsNotExists;
    }

    *nodeOut = result->Node;
    return OsOK;
}

OsStatus_t VFSNodeCreateChild(struct VFSNode* node, MString_t* name, uint32_t flags, uint32_t permissions, struct VFSNode** nodeOut)
{
    struct VFSOperations* ops = &node->FileSystem->Module->Operations;
    struct VFS*           vfs = node->FileSystem;
    struct __VFSChild*    result;
    OsStatus_t            osStatus, osStatus2;
    MString_t*            nodePath = VFSNodeMakePath(node, 1);
    void*                 data, *fileData;

    if (nodePath == NULL) {
        return OsOutOfMemory;
    }

    usched_rwlock_w_promote(&node->Lock);

    // make sure the first we do is verify it still does not exist
    result = hashtable_get(&node->Children, &(struct __VFSChild) { .Key = name });
    if (result != NULL) {
        usched_rwlock_w_demote(&node->Lock);
        MStringDestroy(nodePath);
        *nodeOut = result->Node;
        return OsExists;
    }

    osStatus = ops->Open(&vfs->Base, node->Name, &data);
    if (osStatus != OsOK) {
        goto cleanup;
    }

    osStatus = ops->Create(&vfs->Base, data, name, 0, flags, permissions, &fileData);
    if (osStatus != OsOK) {
        goto close;
    }

    osStatus = __AddEntry(node, &(struct VFSStat) {
            .Name = name,
            .Size = 0,
            .Owner = 0,
            .Flags = flags,
            .Permissions = permissions
    });

    osStatus2 = ops->Close(&vfs->Base, fileData);
    if (osStatus2 != OsOK) {
        WARNING("VFSNodeCreateChild failed to cleanup handle with code %u", osStatus2);
    }

close:
    osStatus2 = ops->Close(&vfs->Base, data);
    if (osStatus2 != OsOK) {
        WARNING("VFSNodeCreateChild failed to cleanup handle with code %u", osStatus2);
    }

    cleanup:
    usched_rwlock_w_demote(&node->Lock);
    MStringDestroy(nodePath);
    return osStatus;
}

OsStatus_t VFSNodeCreateLinkChild(struct VFSNode* node, MString_t* name, MString_t* target, int symbolic, struct VFSNode** nodeOut)
{
    struct VFSOperations* ops = &node->FileSystem->Module->Operations;
    struct VFS*           vfs = node->FileSystem;
    struct __VFSChild*    result;
    OsStatus_t            osStatus, osStatus2;
    MString_t*            nodePath = VFSNodeMakePath(node, 1);
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

    osStatus = ops->Open(&vfs->Base, node->Name, &data);
    if (osStatus != OsOK) {
        goto cleanup;
    }

    osStatus = ops->Link(&vfs->Base, data, name, target, symbolic);
    if (osStatus != OsOK) {
        goto close;
    }

    osStatus = __AddEntry(node, &(struct VFSStat) {
            .Name = name,
            .Size = MStringSize(target),
            .Owner = 0,
            .Flags = FILE_FLAG_LINK,
            .Permissions = FILE_PERMISSION_READ | FILE_PERMISSION_WRITE | FILE_PERMISSION_EXECUTE
    });

close:
    osStatus2 = ops->Close(&vfs->Base, data);
    if (osStatus2 != OsOK) {
        WARNING("__CreateInNode failed to cleanup handle with code %u", osStatus2);
    }

cleanup:
    usched_rwlock_w_demote(&node->Lock);
    MStringDestroy(nodePath);
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

OsStatus_t VFSNodeOpenHandle(struct VFSNode* node, uint32_t accessKind, UUId_t* handleOut)
{
    struct __HandleExcCheckContext context;
    struct VFSNodeHandle*          result;
    OsStatus_t                     osStatus;
    UUId_t                         handleId;

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

    //
    osStatus = VFSNodeHandleAdd(handleId, node);
    if (osStatus != OsOK) {
        handle_destroy(handleId);
        goto cleanup;
    }

    // Everything OK, we can add a new handle
    hashtable_set(&node->Handles, &(struct __VFSHandle) {
            .Id = handleId,
            .AccessKind = accessKind
    });
    *handleOut = handleId;

    cleanup:
    usched_mtx_unlock(&node->HandlesLock);
    return osStatus;
}