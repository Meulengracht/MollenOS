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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
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

KERNELAPI oscode_t KERNELABI InitializeHandles(void);
KERNELAPI oscode_t KERNELABI InitializeHandleJanitor(void);

/**
 * @brief Allocates a new handle for a system resource with a reference of 1.
 */
KERNELAPI uuid_t KERNELABI
CreateHandle(
    _In_ HandleType_t       handleType,
    _In_ HandleDestructorFn destructor,
    _In_ void*              resource);

/**
 * @brief Reduces the reference count of the given handle, and cleans up the handle on
 * reaching 0 references.
 * @param handleId
 * @return OsIncomplete if there are still active references
 *         OsOK if the handle was destroyed
 */
KERNELAPI oscode_t KERNELABI
DestroyHandle(
        _In_ uuid_t handleId);

/**
 * @brief Registers a global handle path that can be used to look up the handle.
 *
 * @param handleId [In] The handle to register the path with.
 * @param path   [In] The path at which the handle should reside.
 */
KERNELAPI oscode_t KERNELABI
RegisterHandlePath(
        _In_ uuid_t      handleId,
        _In_ const char* path);

/**
 * @brief Tries to resolve a handle from the given path.
 *
 * @param path      [In]  The path to resolve a handle for.
 * @param handleOut [Out] A pointer to handle storage.
 */
KERNELAPI oscode_t KERNELABI
LookupHandleByPath(
        _In_  const char* path,
        _Out_ uuid_t*     handleOut);

/**
 * @brief Acquires the handle given for the calling process. This can fail if the handle
 * turns out to be invalid, otherwise the resource will be returned.
 * @param handleId [In] The handle that should be acquired.
 */
KERNELAPI oscode_t KERNELABI
AcquireHandle(
        _In_  uuid_t handleId,
        _Out_ void** resourceOut);

/**
 * Retrieves the handle given, while also performing type validation of the handle. 
 * This can fail if the handle turns out to be invalid, otherwise the resource will be returned.
 */
KERNELAPI void* KERNELABI
LookupHandleOfType(
        _In_ uuid_t       handleId,
        _In_ HandleType_t handleType);

#endif //! __HANDLE_H__
