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
#include <ds/hashtable.h>
#include <vfs/vfs.h>
#include "private.h"

static hashtable_t g_handles;



oserr_t
VFSNodeHandleAdd(
        _In_ uuid_t          handleId,
        _In_ struct VFSNode* node,
        _In_ void*           data,
        _In_ uint32_t        accessKind)
{

    return OsOK;
}

oserr_t
VFSNodeHandleRemove(
        _In_ uuid_t handleId)
{
    void* found;

    found = hashtable_remove(&g_handles, &(struct VFSNodeHandle) { .Id = handleId });
    if (found == NULL) {
        return OsNotExists;
    }
    return OsOK;
}

oserr_t
VFSNodeHandleGet(
        _In_  uuid_t                 handleId,
        _Out_ struct VFSNodeHandle** handleOut)
{

}

oserr_t
VFSNodeHandlePut(
        _In_ struct VFSNodeHandle* handle)
{

}
