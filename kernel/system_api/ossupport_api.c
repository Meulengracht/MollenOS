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
 *
 *
 * MollenOS MCore - System Calls
 */
#define __MODULE "SCIF"
//#define __TRACE

#include <assert.h>
#include <handle.h>
#include <handle_set.h>
#include <heap.h>
#include <internal/_utils.h>
#include <memoryspace.h>
#include <threading.h>

oscode_t
ScInstallSignalHandler(
    _In_ uintptr_t handler)
{
    return MemorySpaceSetSignalHandler(GetCurrentMemorySpace(), handler);
}

oscode_t
ScCreateHandle(
    _Out_ UUId_t* HandleOut)
{
    *HandleOut = CreateHandle(HandleTypeGeneric, NULL, NULL);
    if (*HandleOut != UUID_INVALID) {
        return OsOK;
    }
    return OsOutOfMemory;
}

oscode_t
ScSetHandleActivity(
    _In_ UUId_t  Handle,
    _In_ unsigned int Flags)
{
    return MarkHandle(Handle, Flags);
}

oscode_t
ScRegisterHandlePath(
    _In_ UUId_t      Handle,
    _In_ const char* Path)
{
    return RegisterHandlePath(Handle, Path);
}

oscode_t
ScLookupHandle(
    _In_  const char* Path,
    _Out_ UUId_t*     HandleOut)
{
    return LookupHandleByPath(Path, HandleOut);
}

oscode_t
ScDestroyHandle(
    _In_ UUId_t Handle)
{
    if (Handle == UUID_INVALID) {
        return OsInvalidParameters;
    }
    return DestroyHandle(Handle);
}

oscode_t
ScCreateHandleSet(
    _In_  unsigned int Flags,
    _Out_ UUId_t* HandleOut)
{
    if (!HandleOut) {
        return OsInvalidParameters;
    }
    
    *HandleOut = CreateHandleSet(Flags);
    if (*HandleOut != UUID_INVALID) {
        return OsOK;
    }
    return OsOutOfMemory;
}

oscode_t
ScControlHandleSet(
    _In_ UUId_t              setHandle,
    _In_ int                 operation,
    _In_ UUId_t              handle,
    _In_ struct ioset_event* event)
{
    return ControlHandleSet(setHandle, operation, handle, event);
}

oscode_t
ScListenHandleSet(
    _In_  UUId_t                     handle,
    _In_  HandleSetWaitParameters_t* parameters,
    _Out_ int*                       numberOfEventsOut)
{
    if (!parameters || !numberOfEventsOut) {
        return OsInvalidParameters;
    }
    return WaitForHandleSet(handle, parameters->events, parameters->maxEvents,
        parameters->pollEvents, parameters->timeout, numberOfEventsOut);
}
