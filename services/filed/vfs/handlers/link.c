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

static bool __NodeIsDirectory(struct VFSNode* node)
{
    if (node->Stats.Flags & FILE_FLAG_DIRECTORY) {
        return true;
    }
    return false;
}

oserr_t VFSNodeLink(struct VFS* vfs, struct VFSRequest* request)
{
    struct VFSNode* node;
    mstring_t*      path   = mstr_path_new_u8(request->parameters.link.from);
    mstring_t*      target = mstr_new_u8(request->parameters.link.to);
    oserr_t      osStatus;
    size_t          pathLength;
    int             startIndex;

    if (path == NULL || target == NULL) {
        mstr_delete(path);
        mstr_delete(target);
        return OsOutOfMemory;
    }
    pathLength = mstr_len(path);

    startIndex = 1;
    node       = vfs->Root;
    while (1) {
        int             endIndex = mstr_find_u8(path, "/", startIndex);
        mstring_t*      token    = mstr_substr(path, startIndex, (int)pathLength - endIndex); // TODO ehh verify the logic here
        struct VFSNode* child;

        // If we run out of tokens, then we ended on an existing path
        // and we cannot create a link at this path
        if (mstr_len(token) == 0) {
            mstr_delete(token);
            osStatus = OsExists;
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

            // OK we are at end of path and node did not exist
            // so we can now try to create this
            osStatus = VFSNodeCreateLinkChild(node, token, target, request->parameters.link.symbolic, &child);
            mstr_delete(token);
            break;
        } else if (osStatus != OsOK) {
            mstr_delete(token);
            break;
        }

        // Cleanup the token at this point, we don't need it anymore
        mstr_delete(token);

        if (endIndex == -1) {
            mstr_delete(token);
            osStatus = OsExists;
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
