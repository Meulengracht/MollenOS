/**
 * Copyright, Philip Meulengracht
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
 */

#include <internal/_io.h>
#include <os/handle.h>
#include <os/shm.h>
#include <os/services/net.h>
#include <os/types/file.h>

#include "private.h"

struct __inheritance_type {
    unsigned int             signature;
    unsigned int             flags;
    unsigned int             local_flags;
    GrantTokenResourceType_t resource_type;
    uint32_t                 scope;
    uint32_t               (*mode_scope)(unsigned int);
    oserr_t                (*adopt)(uuid_t, OSHandle_t*);
};

static uint32_t
__file_scope(
    _In_ unsigned int flags)
{
    uint32_t scope;

    // The child gets only the file access its descriptor mode can use.
    scope = (flags & __IO_READ) ? __FILE_READ_ACCESS : 0;
    scope |= (flags & __IO_WRITE) ? __FILE_WRITE_ACCESS : 0;
    return scope;
}

static oserr_t
__adopt_file(
    _In_  uuid_t      reference,
    _Out_ OSHandle_t* handle)
{
    return OSHandleWrap(reference, OSHANDLE_NULL, NULL, true, handle);
}

static oserr_t
__adopt_pipe(
    _In_  uuid_t      reference,
    _Out_ OSHandle_t* handle)
{
    OSHandle_t attachment;
    oserr_t    status;

    status = SHMAdopt(reference, &attachment);
    if (status != OS_EOK) {
        return status;
    }

    // Both ends update the ring indexes. Descriptor read/write modes do not
    // narrow the CPU mapping permissions needed by the stream implementation.
    status = SHMMap(
        &attachment,
        0,
        SHMBufferCapacity(&attachment),
        SHM_ACCESS_READ | SHM_ACCESS_WRITE
    );
    if (status != OS_EOK) {
        OSHandleDestroy(&attachment);
        return status;
    }

    *handle = attachment;
    return OS_EOK;
}

static const struct __inheritance_type g_inheritanceTypes[] = {
    { FILE_SIGNATURE, __IO_INHERIT_FLAGS, 0,
      GrantTokenResourceTypeProvider, 0, __file_scope, __adopt_file },
    { PIPE_SIGNATURE, __IO_INHERIT_FLAGS | __IO_NONBLOCK, __IO_PIPE,
      GrantTokenResourceTypeSHM, GrantTokenScopeRead | GrantTokenScopeWrite, NULL, __adopt_pipe },
    { NET_SIGNATURE, __IO_INHERIT_FLAGS, 0,
      GrantTokenResourceTypeProvider, OS_SOCKET_ACCESS, NULL, OSSocketAcquire }
};

static const struct __inheritance_type*
__inheritance_type(
    _In_ unsigned int signature)
{
    for (size_t i = 0; i < SIZEOF_ARRAY(g_inheritanceTypes); i++) {
        if (g_inheritanceTypes[i].signature == signature) {
            return &g_inheritanceTypes[i];
        }
    }
    return NULL;
}

static uint32_t
__inheritance_scope(
    _In_ const struct __inheritance_type* type,
    _In_ unsigned int                     flags)
{
    if (type->mode_scope != NULL) {
        return type->mode_scope(flags);
    }
    return type->scope;
}

oserr_t
CRTPrepareInheritedHandle(
    _In_  const stdio_handle_t* handle,
    _Out_ unsigned int*         flagsOut,
    _Out_ GrantTokenOptions_t*  optionsOut)
{
    const struct __inheritance_type* type = __inheritance_type(handle->Signature);
    GrantTokenOptions_t              options = { 0 };
    size_t                           capacity;

    if (type == NULL || handle->OSHandle.ID == UUID_INVALID) {
        return OS_ENOTSUPPORTED;
    }

    // The child closes inherited aliases through the shared handle path. A
    // descriptor without Shared closes its operation context once per copy,
    // so it cannot be exported as an alias.
    if (!handle->Shared) {
        return OS_ENOTSUPPORTED;
    }

    *flagsOut = handle->XTFlags & type->flags;
    
    options.ResourceType = type->resource_type;
    options.TargetType = GrantTokenTargetTypeAny;
    options.Flags = GrantTokenFlagsSingleUse;
    options.Scope = __inheritance_scope(type, *flagsOut);
    
    if (type->resource_type == GrantTokenResourceTypeSHM) {
        // Capacity is relative to this attachment, including an inherited
        // restricted view. Exporting it again must never expand the range.
        capacity = SHMBufferCapacity((OSHandle_t*)&handle->OSHandle);
        if (capacity == 0 || capacity > UINT32_MAX) {
            return OS_EINVALPARAMS;
        }
        options.Data.SHM.Length = (uint32_t)capacity;
    }

    *optionsOut = options;
    return OS_EOK;
}

oserr_t
CRTValidateInheritedDescriptor(
    _In_ unsigned int signature,
    _In_ unsigned int flags)
{
    const struct __inheritance_type* type = __inheritance_type(signature);

    if (type == NULL) {
        return OS_ENOTSUPPORTED;
    }
    if (flags & ~type->flags) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

oserr_t
CRTAdoptInheritedHandle(
    _In_  unsigned int signature,
    _In_  uuid_t       reference,
    _Out_ OSHandle_t*  handle)
{
    const struct __inheritance_type* type = __inheritance_type(signature);

    // The complete descriptor list was validated before adoption began.
    return type->adopt(reference, handle);
}

unsigned int
CRTInheritedLocalFlags(
    _In_ unsigned int signature,
    _In_ unsigned int flags)
{
    const struct __inheritance_type* type = __inheritance_type(signature);

    // Intrinsic stream behavior is reconstructed from the validated signature.
    // The persistent close flag is not part of the wire contract.
    return flags | type->local_flags | __IO_OPEN;
}
