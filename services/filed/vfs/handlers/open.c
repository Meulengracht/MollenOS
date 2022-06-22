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

static MString_t* __GetNodePath(struct VFSNode* node)
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
    if (osStatus != OsSuccess) {
        return osStatus;
    }
    return OsSuccess;
}

static OsStatus_t __ParseEntries(struct VFSNode* node, void* buffer, size_t length) {
    struct VFSStat* i              = (struct VFSStat*)buffer;
    size_t          bytesAvailable = length;

    while (bytesAvailable) {
        OsStatus_t osStatus = __AddEntry(node, i);
        if (osStatus != OsSuccess) {
            return osStatus;
        }
        bytesAvailable -= sizeof(struct VFSStat);
        i++;
    }
    return OsSuccess;
}

static OsStatus_t __LoadNode(struct VFSNode* node) {
    struct VFSOperations* ops = &node->FileSystem->Module->Operations;
    struct VFS*           vfs = node->FileSystem;
    OsStatus_t            osStatus, osStatus2;
    MString_t*            nodePath = __GetNodePath(node);
    void*                 data;

    if (nodePath == NULL) {
        return OsOutOfMemory;
    }

    osStatus = ops->Open(&vfs->Base, nodePath, &data);
    if (osStatus != OsSuccess) {
        goto cleanup;
    }

    while (1) {
        size_t read;

        osStatus = ops->Read(&vfs->Base, data, vfs->Buffer.handle, vfs->Buffer.buffer, 0, vfs->Buffer.length, &read);
        if (osStatus != OsSuccess || read == 0) {
            break;
        }

        osStatus = __ParseEntries(node, vfs->Buffer.buffer, read);
        if (osStatus != OsSuccess) {
            break;
        }
    }

    osStatus2 = ops->Close(&vfs->Base, data);
    if (osStatus2 != OsSuccess) {
        WARNING("__LoadNode failed to cleanup handle with code %u", osStatus2);
    }

cleanup:
    MStringDestroy(nodePath);
    return osStatus;
}

//  __FindNameInNode locates a named entry in a node. If the node is unloaded the node
// will be loaded. Note: A reader lock must be held on node
static OsStatus_t __FindNameInNode(struct VFSNode* node, MString_t* name, struct VFSNode** nodeOut)
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
            if (osStatus != OsSuccess) {
                usched_rwlock_w_demote(&node->Lock);
                return osStatus;
            }
            node->IsLoaded = true;
        }
        usched_rwlock_w_demote(&node->Lock);
    }

    result = hashtable_get(&node->Children, &(struct __VFSChild) { .Key = name });
    if (result == NULL) {
        return OsDoesNotExist;
    }

    *nodeOut = result->Node;
    return OsSuccess;
}

static OsStatus_t __CreateInNode(struct VFSNode* node, MString_t* name, uint32_t flags, uint32_t permissions, struct VFSNode** nodeOut)
{
    struct VFSOperations* ops = &node->FileSystem->Module->Operations;
    struct VFS*           vfs = node->FileSystem;
    struct __VFSChild*    result;
    OsStatus_t            osStatus, osStatus2;
    MString_t*            nodePath = __GetNodePath(node);
    void*                 data;

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
    if (osStatus != OsSuccess) {
        goto cleanup;
    }

    osStatus = ops->Create(&vfs->Base, data, name, 0, flags, permissions);
    if (osStatus != OsSuccess) {
        goto close;
    }

    osStatus = __AddEntry(node, &(struct VFSStat) {
        .Name = name,
        .Size = 0,
        .Owner = 0,
        .Flags = flags,
        .Permissions = permissions
    });

close:
    osStatus2 = ops->Close(&vfs->Base, data);
    if (osStatus2 != OsSuccess) {
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

static OsStatus_t __OpenHandle(struct VFSNode* node, uint32_t accessKind, UUId_t* handleOut)
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
    if (osStatus != OsSuccess) {
        goto cleanup;
    }

    osStatus = VFSNodeHandleAdd(handleId, node);
    if (osStatus != OsSuccess) {
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

static OsStatus_t __OpenDirectory(struct VFS* vfs, struct VFSRequest* request, UUId_t* handleOut)
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

    if (__IsPathRoot(path)) {
        MStringDestroy(path);
        return __OpenHandle(vfs->Root, request->parameters.open.access, handleOut);
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
            osStatus = __OpenHandle(node, request->parameters.open.access, handleOut);
            break;
        }

        // Acquire a read lock on this node, when we exit we release the entire chain
        usched_rwlock_r_lock(&node->Lock);

        // Next is finding this token inside the current VFSNode
        osStatus = __FindNameInNode(node, token, &child);
        if (osStatus == OsDoesNotExist) {
            // Ok, did not exist, were creation flags passed?
            if (endIndex != MSTRING_NOT_FOUND) {
                // Not end of path, did not exist
                MStringDestroy(token);
                osStatus = OsDoesNotExist;
                break;
            }

            // Cool, we are at the end of the path, if creation flags were passed
            // we can continue
            if (request->parameters.open.options & __FILE_CREATE) {
                osStatus = __CreateInNode(node, token,
                                          request->parameters.open.options,
                                          request->parameters.open.access,
                                          &child);
                if (osStatus != OsSuccess) {
                    MStringDestroy(token);
                    break;
                }
            }
        } else if (osStatus != OsSuccess) {
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
            if (request->parameters.open.options & __FILE_FAILONEXIST) {
                MStringDestroy(token);
                osStatus = OsExists;
                break;
            }

            // So at this point child is valid and points to the target node we were looking
            // for, so we can now acquire a lock on that based on permissions
            osStatus = __OpenHandle(child, request->parameters.open.access, handleOut);
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

static  OsStatus_t __OpenFile(struct VFS* vfs, struct VFSRequest* request, UUId_t* handleOut)
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

    // Catch the case where we are opening the root, but have not specified
    // the __FILE_DIRECTORY flag.
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
        osStatus = __FindNameInNode(node, token, &child);
        if (osStatus == OsDoesNotExist) {
            // Ok, did not exist, were creation flags passed?
            if (endIndex != MSTRING_NOT_FOUND) {
                // Not end of path, did not exist
                MStringDestroy(token);
                osStatus = OsDoesNotExist;
                break;
            }

            // Cool, we are at the end of the path, if creation flags were passed
            // we can continue
            if (request->parameters.open.options & __FILE_CREATE) {
                osStatus = __CreateInNode(node, token,
                                          request->parameters.open.options,
                                          request->parameters.open.access,
                                          &child);
                if (osStatus != OsSuccess) {
                    MStringDestroy(token);
                    break;
                }
            }
        } else if (osStatus != OsSuccess) {
            MStringDestroy(token);
            break;
        }

        // Cleanup the token at this point, we don't need it anymore
        MStringDestroy(token);

        if (endIndex == MSTRING_NOT_FOUND) {
            if (request->parameters.open.options & __FILE_FAILONEXIST) {
                MStringDestroy(token);
                osStatus = OsExists;
                break;
            }

            // So at this point child is valid and points to the target node we were looking
            // for, so we can now acquire a lock on that based on permissions
            osStatus = __OpenHandle(child, request->parameters.open.access, handleOut);
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

OsStatus_t VFSNodeOpen(struct VFS* vfs, struct VFSRequest* request, UUId_t* handleOut)
{
    // Split out the logic to keep functions simpler, we don't want to handle to many
    // cases inside one function
    if (request->parameters.open.options & __FILE_DIRECTORY) {
        return __OpenDirectory(vfs, request, handleOut);
    }
    return __OpenFile(vfs, request, handleOut);
}
