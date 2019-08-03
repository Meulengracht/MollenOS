/* MollenOS
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

enum SystemHandleFlags {
    HandleCleanup = 0x1
};

typedef enum _SystemHandleType {
    HandleGeneric = 0,
    HandleTypeMemorySpace,
    HandleTypeMemoryRegion,
    HandleTypeThread,
    HandleTypePipe,

    HandleTypeCount
} SystemHandleType_t;

typedef void (*HandleDestructorFn)(void*);

typedef struct _SystemHandle {
    SystemHandleType_t Type;
    Flags_t            Flags;
    atomic_int         References;
    HandleDestructorFn Destructor;
    void*              Resource;
} SystemHandle_t;

KERNELAPI OsStatus_t KERNELABI
InitializeHandles(void);

KERNELAPI OsStatus_t KERNELABI
InitializeHandleJanitor(void);

/* CreateHandle
 * Allocates a new handle for a system resource with a reference of 1. */
KERNELAPI UUId_t KERNELABI
CreateHandle(
    _In_ SystemHandleType_t Type,
    _In_ Flags_t            Flags,
    _In_ HandleDestructorFn Destructor,
    _In_ void*              Resource);

/* DestroyHandle
 * Reduces the reference count of the given handle, and cleans up the handle on
 * reaching 0 references. */
KERNELAPI void KERNELABI
DestroyHandle(
    _In_ UUId_t Handle);

/* AcquireHandle
 * Acquires the handle given for the calling process. This can fail if the handle
 * turns out to be invalid, otherwise the resource will be returned. */
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
