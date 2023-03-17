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
#include <handle.h>
#include <handle_set.h>
#include <os/types/syscall.h>
#include <memoryspace.h>

oserr_t
ScCreateHandle(
        _Out_ uuid_t* HandleOut)
{
    *HandleOut = CreateHandle(HandleTypeGeneric, NULL, NULL);
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
    return DestroyHandle(Handle);
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
