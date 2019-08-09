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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - System Calls
 */
#define __MODULE "SCIF"
//#define __TRACE

#include <modules/manager.h>
#include <memoryspace.h>
#include <ds/mstring.h>
#include <threading.h>
#include <handle.h>
#include <assert.h>
#include <debug.h>
#include <heap.h>

OsStatus_t
ScCreateMemoryHandler(
    _In_  Flags_t    Flags,
    _In_  size_t     Length,
    _Out_ UUId_t*    HandleOut,
    _Out_ uintptr_t* AddressBaseOut)
{
    SystemMemorySpace_t* Space = GetCurrentMemorySpace();
    assert(Space->Context != NULL);

    if (Space->Context->HeapSpace != NULL) {
        SystemMemoryMappingHandler_t* Handler = (SystemMemoryMappingHandler_t*)kmalloc(sizeof(SystemMemoryMappingHandler_t));
        Handler->Handle  = CreateHandle(HandleGeneric, 0, NULL, Handler);
        Handler->Address = AllocateBlocksInBlockmap(Space->Context->HeapSpace, __MASK, Length);
        Handler->Length  = Length;
        
        *HandleOut       = Handler->Handle;
        *AddressBaseOut  = Handler->Address;
        return CollectionAppend(Space->Context->MemoryHandlers, &Handler->Header);
    }
    return OsInvalidPermissions;
}

OsStatus_t
ScDestroyMemoryHandler(
    _In_ UUId_t Handle)
{
    SystemMemoryMappingHandler_t* Handler = (SystemMemoryMappingHandler_t*)LookupHandle(Handle);
    SystemMemorySpace_t*          Space   = GetCurrentMemorySpace();
    assert(Space->Context != NULL);

    if (Space->Context->MemoryHandlers != NULL && Handler != NULL) {
        CollectionRemoveByNode(Space->Context->MemoryHandlers, &Handler->Header);
        ReleaseBlockmapRegion(Space->Context->HeapSpace, Handler->Address, Handler->Length);
        DestroyHandle(Handle);
        kfree(Handler);
        return OsSuccess;
    }
    return OsDoesNotExist;
}

OsStatus_t
ScInstallSignalHandler(
    _In_ uintptr_t Handler) 
{
    SystemMemorySpace_t* Space = GetCurrentMemorySpace();
    assert(Space->Context != NULL);

    Space->Context->SignalHandler = Handler;
    return OsSuccess;
}

OsStatus_t
ScRegisterHandlePath(
    _In_ UUId_t      Handle,
    _In_ const char* Path)
{
    return RegisterHandlePath(Handle, Path);
}

OsStatus_t
ScLookupHandle(
    _In_  const char* Path,
    _Out_ UUId_t*     HandleOut)
{
    return LookupHandleByPath(Path, HandleOut);
}

OsStatus_t
ScDestroyHandle(
    _In_ UUId_t Handle)
{
    if (Handle == 0 || Handle == UUID_INVALID) {
        return OsInvalidParameters;
    }
    DestroyHandle(Handle);
    return OsSuccess;
}
