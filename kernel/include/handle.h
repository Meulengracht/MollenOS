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

#ifndef __HANDLE_H__
#define __HANDLE_H__

#include <os/osdefs.h>

typedef enum HandleType {
    HandleTypeGeneric = 0,
    HandleTypeSet,
    HandleTypeMemorySpace,
    HandleTypeMemoryRegion,
    HandleTypeThread,
    HandleTypeIpcContext,
    HandleTypeUserEvent
} HandleType_t;

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
    _In_ HandleType_t       Type,
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
KERNELAPI OsStatus_t KERNELABI
AcquireHandle(
    _In_  UUId_t Handle,
    _Out_ void** ResourceOut);

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
    _In_ UUId_t       Handle,
    _In_ HandleType_t Type);

#endif //! __HANDLE_H__
