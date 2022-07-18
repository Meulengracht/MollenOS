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

static bool __IsPathRoot(mstring_t* path)
{
    if (mstr_at(path, 0) == '/' && mstr_len(path) == 1) {
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

static oserr_t __OpenDirectory(struct VFS* vfs, struct VFSRequest* request, uuid_t* handleOut)
{
    struct VFSNode* node;
    oserr_t         osStatus;
    mstring_t*      path;
    size_t          pathLength = 0;
    int             startIndex;

    path = VFSMakePath(request->parameters.open.path);
    if (path == NULL) {
        return OsOutOfMemory;
    }

    if (__IsPathRoot(path)) {
        mstr_delete(path);
        return VFSNodeOpenHandle(vfs->Root, request->parameters.open.access, handleOut);
    }

    startIndex = 1;
    node       = vfs->Root;
    while (1) {
        int             endIndex = mstr_find_u8(path, "/", startIndex);
        mstring_t*      token    = mstr_substr(path, startIndex, (int)pathLength - endIndex); // TODO ehh verify the logic here
        struct VFSNode* child;

        // If we run out of tokens (for instance path ends on '/') then we can assume at this
        // point that we are standing at the directory. So return a handle to the current node
        if (mstr_len(token) == 0) {
            osStatus = VFSNodeOpenHandle(node, request->parameters.open.access, handleOut);
            break;
        }

        // Acquire a read lock on this node, when we exit we release the entire chain
        usched_rwlock_r_lock(&node->Lock);

        // Next is finding this token inside the current VFSNode
        osStatus = VFSNodeFind(node, token, &child);
        if (osStatus == OsNotExists) {
            // Ok, did not exist, were creation flags passed?
            if (endIndex != -1) {
                // Not end of path, did not exist
                mstr_delete(token);
                osStatus = OsNotExists;
                break;
            }

            // Cool, we are at the end of the path, if creation flags were passed
            // we can continue
            if (request->parameters.open.options & __FILE_CREATE) {
                osStatus = VFSNodeCreateChild(node, token,
                                              request->parameters.open.options,
                                              request->parameters.open.access,
                                              &child);
                if (osStatus != OsOK) {
                    mstr_delete(token);
                    break;
                }
            }
        } else if (osStatus != OsOK) {
            mstr_delete(token);
            break;
        }

        // Cleanup the token at this point, we don't need it anymore
        mstr_delete(token);

        // Entry we find must always be a directory
        if (!__NodeIsDirectory(child)) {
            osStatus = OsPathIsNotDirectory;
            break;
        }

        if (endIndex == -1) {
            if (request->parameters.open.options & __FILE_FAILONEXIST) {
                mstr_delete(token);
                osStatus = OsExists;
                break;
            }

            // So at this point child is valid and points to the target node we were looking
            // for, so we can now acquire a lock on that based on permissions
            osStatus = VFSNodeOpenHandle(child, request->parameters.open.access, handleOut);
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

    mstr_delete(path);
    return osStatus;
}

static oserr_t __OpenFile(struct VFS* vfs, struct VFSRequest* request, uuid_t* handleOut)
{
    struct VFSNode* node;
    oserr_t      osStatus;
    mstring_t*      path;
    size_t          pathLength;
    int             startIndex;

    path = VFSMakePath(request->parameters.open.path);
    if (path == NULL) {
        return OsOutOfMemory;
    }
    pathLength = mstr_len(path);

    // Catch the case where we are opening the root, but have not specified
    // the __FILE_DIRECTORY flag.
    if (__IsPathRoot(path)) {
        return OsPathIsDirectory;
    }

    startIndex = 1;
    node       = vfs->Root;
    while (1) {
        int             endIndex = mstr_find_u8(path, "/", startIndex);
        mstring_t*      token    = mstr_substr(path, startIndex, (int)pathLength - endIndex); // TODO ehh verify the logic here
        struct VFSNode* child;

        // If we run out of tokens, then the path passed to us was
        // a directory path and not a file path.
        if (mstr_len(token) == 0) {
            mstr_delete(token);
            osStatus = OsPathIsDirectory;
            break;
        }

        // Acquire a read lock on this node, when we exit we release the entire chain
        usched_rwlock_r_lock(&node->Lock);

        // Next is finding this token inside the current VFSNode
        osStatus = VFSNodeFind(node, token, &child);
        if (osStatus == OsNotExists) {
            // Ok, did not exist, were creation flags passed?
            if (endIndex != -1) {
                // Not end of path, did not exist
                mstr_delete(token);
                osStatus = OsNotExists;
                break;
            }

            // Cool, we are at the end of the path, if creation flags were passed
            // we can continue
            if (request->parameters.open.options & __FILE_CREATE) {
                osStatus = VFSNodeCreateChild(node, token,
                                              request->parameters.open.options,
                                              request->parameters.open.access,
                                              &child);
                if (osStatus != OsOK) {
                    mstr_delete(token);
                    break;
                }
            }
        } else if (osStatus != OsOK) {
            mstr_delete(token);
            break;
        }

        // Cleanup the token at this point, we don't need it anymore
        mstr_delete(token);

        if (endIndex == -1) {
            if (request->parameters.open.options & __FILE_FAILONEXIST) {
                mstr_delete(token);
                osStatus = OsExists;
                break;
            }

            // So at this point child is valid and points to the target node we were looking
            // for, so we can now acquire a lock on that based on permissions
            osStatus = VFSNodeOpenHandle(child, request->parameters.open.access, handleOut);
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

    mstr_delete(path);
    return osStatus;
}

oserr_t VFSNodeOpen(struct VFS* vfs, struct VFSRequest* request, uuid_t* handleOut)
{
    // Split out the logic to keep functions simpler, we don't want to handle to many
    // cases inside one function
    if (request->parameters.open.options & __FILE_DIRECTORY) {
        return __OpenDirectory(vfs, request, handleOut);
    }
    return __OpenFile(vfs, request, handleOut);
}
