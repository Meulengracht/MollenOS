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

#include <vfs/vfs.h>
#include "../private.h"

oserr_t VFSNodeMkdir(struct VFS* vfs, mstring_t* path, uint32_t access, uuid_t* handleOut)
{
    struct VFSNode* node;
    oserr_t         oserr;

    oserr = VFSNodeNewDirectory(vfs, path, access, &node);
    if (oserr != OsOK) {
        return oserr;
    }
    return VFSNodeOpenHandle(node, access, handleOut);
}
