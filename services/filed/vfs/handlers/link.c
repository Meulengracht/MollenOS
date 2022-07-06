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

oscode_t VFSNodeLink(struct VFS* vfs, struct VFSRequest* request)
{
    struct VFSNode* node;
    MString_t*      path   = VFSMakePath(request->parameters.link.from);
    MString_t*      target = MStringCreate(request->parameters.link.to, StrUTF8);
    oscode_t      osStatus;
    size_t          pathLength;
    int             startIndex;

    if (path == NULL || target == NULL) {
        MStringDestroy(path);
        MStringDestroy(target);
        return OsOutOfMemory;
    }
    pathLength = MStringLength(path);

    startIndex = 1;
    node       = vfs->Root;
    while (1) {
        int             endIndex = MStringFind(path, '/', startIndex);
        MString_t*      token    = MStringSubString(path, startIndex, (int)pathLength - endIndex); // TODO ehh verify the logic here
        struct VFSNode* child;

        // If we run out of tokens, then we ended on an existing path
        // and we cannot create a link at this path
        if (MStringLength(token) == 0) {
            MStringDestroy(token);
            osStatus = OsExists;
            break;
        }

        // Acquire a read lock on this node, when we exit we release the entire chain
        usched_rwlock_r_lock(&node->Lock);

        // Next is finding this token inside the current VFSNode
        osStatus = VFSNodeFind(node, token, &child);
        if (osStatus == OsNotExists) {
            // Ok, did not exist, were creation flags passed?
            if (endIndex != MSTRING_NOT_FOUND) {
                // Not end of path, did not exist
                MStringDestroy(token);
                osStatus = OsNotExists;
                break;
            }

            // OK we are at end of path and node did not exist
            // so we can now try to create this
            osStatus = VFSNodeCreateLinkChild(node, token, target, request->parameters.link.symbolic, &child);
            MStringDestroy(token);
            break;
        } else if (osStatus != OsOK) {
            MStringDestroy(token);
            break;
        }

        // Cleanup the token at this point, we don't need it anymore
        MStringDestroy(token);

        if (endIndex == MSTRING_NOT_FOUND) {
            MStringDestroy(token);
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

    MStringDestroy(path);
    return osStatus;
}
