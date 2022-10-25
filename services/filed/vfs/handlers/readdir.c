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
#include <string.h>

struct __ReadDirectoryContext {
    uint32_t        Next;
    struct VFSStat* Stats;
    bool            Found;
};

static void __ReadEntry(int index, const void* item, void* userContext)
{
    const struct __VFSChild*       child = item;
    struct __ReadDirectoryContext* context = userContext;

    // Are we already done?
    if (context->Found) {
        return;
    }

    // Are we at the requested index?
    if (context->Next) {
        context->Next--;
        return;
    }

    memcpy(context->Stats, &child->Node->Stats, sizeof(struct VFSStat));
    context->Found = true;
}

oserr_t VFSNodeReadDirectory(uuid_t fileHandle, struct VFSStat* stats, uint32_t* indexOut)
{
    struct __ReadDirectoryContext context;
    struct VFSNodeHandle*         handle;
    oserr_t                       oserr;

    oserr = VFSNodeHandleGet(fileHandle, &handle);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Like the name implies, this operation is solely supported for directories nodes.
    if (!__NodeIsDirectory(handle->Node) || __NodeIsSymlink(handle->Node)) {
        oserr = OS_ENOTSUPPORTED;
        goto cleanup;
    }

    context.Found = false;
    context.Stats = stats;
    context.Next = LODWORD(handle->Position);

    // store the current index
    *indexOut = LODWORD(handle->Position);

    usched_rwlock_r_lock(&handle->Node->Lock);
    // Ensure directory is loaded before accessing entries. We do this on
    // demand to optimize for the case where we just check if directory exists.
    oserr = VFSNodeEnsureLoaded(handle->Node);
    if (oserr != OS_EOK) {
        usched_rwlock_r_unlock(&handle->Node->Lock);
        goto cleanup;
    }

    hashtable_enumerate(
            &handle->Node->Children,
            __ReadEntry,
            &context
    );
    usched_rwlock_r_unlock(&handle->Node->Lock);
    if (!context.Found) {
        oserr = OS_ENOENT;
        goto cleanup;
    } else {
        handle->Position++;
        oserr = OS_EOK;
    }

cleanup:
    VFSNodeHandlePut(handle);
    return oserr;
}
