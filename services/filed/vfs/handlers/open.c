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

#include <ddk/utils.h>
#include <vfs/vfs.h>
#include "../private.h"

oserr_t VFSNodeOpen(struct VFS* vfs, const char* cpath, uint32_t options, uint32_t permissions, uuid_t* handleOut)
{
    mstring_t* path;
    TRACE("VFSNodeOpen(%s)", cpath);

    path = mstr_path_new_u8(cpath);
    if (path == NULL) {
        return OS_EOOM;
    }

    // Opening root is a special case, as we won't be able to find the containing folder,
    // and they are easily handled here.
    if (__PathIsRoot(path)) {
        TRACE("VFSNodeOpen path was root");
        mstr_delete(path); // we don't need the path anymore from this point

        // Did user request to create root? lmao
        if (options & __FILE_CREATE) {
            return OS_EPERMISSIONS;
        }

        // Allow this only if requested to be opened as a dir
        if (options & __FILE_DIRECTORY) {
            TRACE("VFSNodeOpen returning root handle");
            return VFSNodeOpenHandle(vfs->Root, options, handleOut);
        }
        return OS_EISDIR;
    }

    mstring_t* containingDirectoryPath = mstr_path_dirname(path);
    mstring_t* nodeName                = mstr_path_basename(path);
    TRACE("VFSNodeOpen containingDirectoryPath=%ms", containingDirectoryPath);
    TRACE("VFSNodeOpen nodeName=%ms", nodeName);
    mstr_delete(path);

    struct VFSNode* containingDirectory;
    oserr_t         osStatus = VFSNodeGet(
            vfs, containingDirectoryPath,
            1, &containingDirectory);

    mstr_delete(containingDirectoryPath);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    // Find the requested entry in the containing folder
    struct VFSNode* node;
    osStatus = VFSNodeFind(containingDirectory, nodeName, &node);
    if (osStatus != OS_EOK && osStatus != OS_ENOENT) {
        TRACE("VFSNodeOpen find returned %u", osStatus);
        goto exit;
    }

    if (osStatus == OS_ENOENT) {
        if (options & __FILE_CREATE) {
            // Create the file with regular options and regular permissions
            osStatus = VFSNodeCreateChild(
                    containingDirectory,
                    nodeName,
                    FILE_FLAG_FILE,
                    permissions,
                    &node
            );
            if (osStatus != OS_EOK) {
                goto exit;
            }
        } else {
            // OK it wasn't found, just exit with that error code
            goto exit;
        }
    }

    if (!__NodeIsDirectory(node) && (options & __FILE_DIRECTORY)) {
        osStatus = OS_ENOTDIR;
        goto exit;
    } else if (__NodeIsDirectory(node) && !(options & __FILE_DIRECTORY)) {
        osStatus = OS_EISDIR;
        goto exit;
    }
    
    osStatus = VFSNodeOpenHandle(node, options, handleOut);

exit:
    VFSNodePut(containingDirectory);
    mstr_delete(nodeName);
    TRACE("VFSNodeOpen done");
    return osStatus;
}
