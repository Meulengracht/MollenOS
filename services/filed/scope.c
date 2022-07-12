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

static struct VFS* g_rootScope = NULL;
static guid_t      g_rootGuid  = GUID_EMPTY;

void VFSScopeInitialize(void)
{
    oserr_t    osStatus;
    MString_t* name;

    // TODO mstring_const
    name = MStringCreate("vfs-root", StrUTF8);
    if (name == NULL) {
        ERROR("VFSScopeInitialize failed to allocate memory for fs name");
        return;
    }

    osStatus = VFSNewMemFS(name, &g_rootGuid, &g_rootScope);
    if (osStatus != OsOK) {
        ERROR("VFSScopeInitialize failed to create root filesystem scope");
    }

    MStringDestroy(name);
}

struct VFS*
VFSScopeGet(
        _In_ uuid_t processId)
{
    return g_rootScope;
}
