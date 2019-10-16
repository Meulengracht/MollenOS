/**
 * MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * Resource Handle Interface
 * - Implementation of the resource handle interface. This provides system-wide
 *   resource handles and maintience of resources. 
 */

#ifndef __HANDLE_INTERFACE__
#define __HANDLE_INTERFACE__

#include <os/osdefs.h>
#include <ds/collection.h>
#include <ds/rbtree.h>

typedef struct handle_event handle_event_t;

enum SystemHandleFlags {
    HandleCleanup = 0x1
};

typedef enum _SystemHandleType {
    HandleTypeGeneric = 0,
    HandleTypeSet,
    HandleTypeMemorySpace,
    HandleTypeMemoryRegion,
    HandleTypeThread,
    HandleTypePipe
} SystemHandleType_t;

typedef void (*HandleDestructorFn)(void*);

KERNELAPI OsStatus_t KERNELABI
InitializeHandles(void);

KERNELAPI OsStatus_t KERNELABI
InitializeHandleJanitor(void);

/**
 * CreateHandle
 * * Allocates a new handle for a system resource with a reference of 1.
 */
KERNELAPI UUId_t KERNELABI
CreateHandle(
    _In_ SystemHandleType_t Type,
    _In_ Flags_t            Flags,
    _In_ HandleDestructorFn Destructor,
    _In_ void*              Resource);

/**
 * DestroyHandle
 * * Reduces the reference count of the given handle, and cleans up the handle on
 * * reaching 0 references.
 */
KERNELAPI void KERNELABI
DestroyHandle(
    _In_ UUId_t Handle);

/**
 * CreateHandleSet
 * * Creates a new handle set that can be used for asynchronus events.
 * @param Flags [In] Creation flags that configure the new handle set behaviour. 
 */
KERNELAPI UUId_t KERNELABI
CreateHandleSet(
    _In_  Flags_t Flags);

/**
 * ControlHandleSet
 * * Add, remove or modify a handle in the set.
 * @param SetHandle [In] The handle of the handle set.
 * @param Operation [In] The operation that should be performed.
 * @param Handle    [In] The handle that should be operated on.
 * @param Flags     [In] The flags that should be configured with the handle.
 */
KERNELAPI OsStatus_t KERNELABI
ControlHandleSet(
    _In_ UUId_t  SetHandle,
    _In_ int     Operation,
    _In_ UUId_t  Handle,
    _In_ Flags_t Flags,
    _In_ void*   Context);

/**
 * WaitForHandleSet
 * * Waits for the given handle set and stores the events that occurred in the
 * * provided array.
 * @param Handle            [In]
 * @param Events            [In]
 * @param MaxEvents         [In]
 * @param Timeout           [In]
 * @param NumberOfEventsOut [Out]
 */
KERNELAPI OsStatus_t KERNELABI
WaitForHandleSet(
    _In_  UUId_t          Handle,
    _In_  handle_event_t* Events,
    _In_  int             MaxEvents,
    _In_  size_t          Timeout,
    _Out_ int*            NumberOfEventsOut);

/** 
 * MarkHandle
 * * Marks a handle that an event has been completed. If the handle has any
 * * sets registered they will be notified.
 * @param Handle [In] The handle upon which an event has taken place
 * @param Flags  [In] The event flags which denote which kind of event.
 */
KERNELAPI OsStatus_t KERNELABI
MarkHandle(
    _In_ UUId_t  Handle,
    _In_ Flags_t Flags);

/**
 * RegisterHandlePath
 * * Registers a global handle path that can be used to look up the handle.
 * @param Handle [In] The handle to register the path with.
 * @param Path   [In] The path at which the handle should reside.
 */
KERNELAPI OsStatus_t KERNELABI
RegisterHandlePath(
    _In_ UUId_t      Handle,
    _In_ const char* Path);

/**
 * LookupHandleByPath
 * * Tries to resolve a handle from the given path.
 * @param Path      [In]  The path to resolve a handle for.
 * @param HandleOut [Out] A pointer to handle storage.
 */
KERNELAPI OsStatus_t KERNELABI
LookupHandleByPath(
    _In_  const char* Path,
    _Out_ UUId_t*     HandleOut);

/**
 * AcquireHandle
 * * Acquires the handle given for the calling process. This can fail if the handle
 * * turns out to be invalid, otherwise the resource will be returned. 
 * @param Handle [In] The handle that should be acquired.
 */
KERNELAPI void* KERNELABI
AcquireHandle(
    _In_ UUId_t Handle);

/* LookupHandle
 * Retrieves the handle given. This can fail if the handle
 * turns out to be invalid, otherwise the resource will be returned. */
KERNELAPI void* KERNELABI
LookupHandle(
    _In_ UUId_t Handle);

/* LookupHandleOfType
 * Retrieves the handle given, while also performing type validation of the handle. 
 * This can fail if the handle turns out to be invalid, otherwise the resource will be returned. */
KERNELAPI void* KERNELABI
LookupHandleOfType(
    _In_ UUId_t             Handle,
    _In_ SystemHandleType_t Type);

/**
 * EnumerateHandlesOfType
 * * Enumerates all system handles of the given type, and executes the callback function
 * @param Type    [In] The type of handle to perform the callback on
 * @param Fn      [In] A function pointer that takes two parameters, 1. Resource pointer, 2. Context pointer
 * @param Context [In] A context pointer that is passed to the callback
 */
KERNELAPI void KERNELABI
EnumerateHandlesOfType(
    _In_ SystemHandleType_t Type,
    _In_ void               (*Fn)(void*, void*),
    _In_ void*              Context);

#endif //! __HANDLE_INTERFACE__
