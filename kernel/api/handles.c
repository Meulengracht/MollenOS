/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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

#define __MODULE "SCIF"
//#define __TRACE

#include <assert.h>
#include <debug.h>
#include <handles/grants.h>
#include <handles/handle.h>
#include <handles/paths.h>
#include <handles/set.h>
#include <os/types/syscall.h>
#include <memoryspace.h>

oserr_t
ScCreateHandle(
        _Out_ uuid_t* HandleOut)
{
    *HandleOut = CreateHandle(HandleTypeGeneric, NULL, NULL, NULL);
    if (*HandleOut != UUID_INVALID) {
        return OS_EOK;
    }
    return OS_EOOM;
}

oserr_t
ScSetHandleActivity(
        _In_ uuid_t  Handle,
        _In_ unsigned int Flags)
{
    return MarkHandle(Handle, Flags);
}

oserr_t
ScLookupHandle(
        _In_  const char* Path,
        _Out_ uuid_t*     HandleOut)
{
    return LookupHandleByPath(Path, HandleOut);
}

oserr_t
ScDestroyHandle(
        _In_ uuid_t Handle)
{
    if (Handle == UUID_INVALID) {
        return OS_EINVALPARAMS;
    }
    
    // CPU and DMA resources use the release syscall, which checks their operation
    // state. Generic close must not consume a suspended begin's reference.
    if (LookupHandle(Handle, HandleTypeSHMAccess) != NULL) {
        return OS_EINVALPARAMS;
    }
    return DestroyHandle(Handle);
}

oserr_t
ScGrantTokenCreate(
    _In_  uuid_t                     handleID,
    _In_  const GrantTokenOptions_t* userOptions,
    _Out_ GrantToken_t*              tokenOut)
{
    GrantTokenOptions_t options;
    GrantToken_t        token;
    oserr_t             oserr;
    oserr_t             cleanupStatus;

    // Copy the options once so the caller cannot change them during validation
    // or export creation.
    oserr = MemorySpaceCopyUser((void*)userOptions, &options, sizeof(options), false);
    if (oserr != OS_EOK) {
        ERROR("ScGrantTokenCreate: options copy for handle %u failed: %u", handleID, oserr);
        return oserr;
    }

    oserr = GrantTokenCreate(handleID, &options, &token);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = MemorySpaceCopyUser(tokenOut, &token, sizeof(token), true);
    if (oserr != OS_EOK) {
        ERROR("ScGrantTokenCreate: token copy for control %u failed: %u",
              token.ControlHandleID, oserr);
        goto error;
    }
    return OS_EOK;

error:
    cleanupStatus = GrantTokenRevoke(token.ControlHandleID);
    // If another thread already closed the control handle, the export is
    // disabled and no further cleanup is needed.
    if (cleanupStatus != OS_EOK && cleanupStatus != OS_ENOENT) {
        ERROR("ScGrantTokenCreate: cancel control %u failed: %u (original status %u)",
              token.ControlHandleID, cleanupStatus, oserr);
    }
    return oserr;
}

oserr_t
ScGrantTokenRevoke(
    _In_ uuid_t controlHandleID)
{
    return GrantTokenRevoke(controlHandleID);
}

oserr_t
ScCreateHandleSet(
        _In_  unsigned int flags,
        _Out_ uuid_t*      handleOut)
{
    if (!handleOut) {
        return OS_EINVALPARAMS;
    }
    
    *handleOut = CreateHandleSet(flags);
    if (*handleOut != UUID_INVALID) {
        return OS_EOK;
    }
    return OS_EOOM;
}

oserr_t
ScControlHandleSet(
        _In_ uuid_t              setHandle,
        _In_ int                 operation,
        _In_ uuid_t              handle,
        _In_ struct ioset_event* event)
{
    return ControlHandleSet(setHandle, operation, handle, event);
}

oserr_t
ScListenHandleSet(
        _In_  uuid_t                     handle,
        _In_  OSAsyncContext_t*          asyncContext,
        _In_  HandleSetWaitParameters_t* parameters,
        _Out_ int*                       numberOfEventsOut)
{
    if (!parameters || !numberOfEventsOut) {
        return OS_EINVALPARAMS;
    }
    return WaitForHandleSet(
            handle,
            asyncContext,
            parameters->Events,
            parameters->MaxEvents,
            parameters->PollEvents,
            parameters->Deadline,
            numberOfEventsOut
    );
}
