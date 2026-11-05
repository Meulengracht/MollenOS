/**
 * MollenOS
 *
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
 * 
 */

#ifndef __HANDLES_HANDLE_H__
#define __HANDLES_HANDLE_H__

#include <os/osdefs.h>
#include <handles/types.h>

/**
 * @brief Create a resource handle in the caller's grant table.
 * Without a security context, creation still returns a global reference for
 * existing kernel callers. New kernel code must use CreateGlobalHandle.
 * Each grant keeps one global reference; its temporary uses are counted within
 * that context. Both callbacks run without handle-table locks. Creation failure
 * leaves resource cleanup to the caller and invokes neither callback.
 * @param handleType     [In] The type of the handle.
 * @param grantCleanup   [In] Optional cleanup when a context's final reference ends,
 *                      or when its entire grant table is destroyed.
 * @param globalCleanup  [In] Optional final destruction, deferred to the janitor
 *                      after every grant and independent kernel reference ends.
 * @param resource       [In] The resource associated with the handle.
 * @return The UUID of the newly created handle, or UUID_INVALID on failure.
 */
KERNELAPI uuid_t KERNELABI
CreateHandle(
    _In_ HandleType_t       handleType,
    _In_ HandleDestructorFn grantCleanup,
    _In_ HandleDestructorFn globalCleanup,
    _In_ void*              resource);

/**
 * @brief Reduces the reference count of the given handle, and cleans up the handle on
 * reaching 0 references.
 * In a security context this drops a reference in that context's grant table.
 * Without a security context it drops a global reference. Temporary uses must
 * finish in the same live context; independent kernel references use
 * ReleaseGlobalHandle instead.
 * @param handleId
 * @return OsIncomplete if there are still active references
 *         OS_EOK if the handle was destroyed
 */
KERNELAPI oserr_t KERNELABI
DestroyHandle(
    _In_ uuid_t handleId);

/**
 * @brief Check the resource type and add a reference in the caller's grant table.
 * Without a security context, existing kernel callers add a global reference.
 * Pair with DestroyHandle in the same context. New kernel code must use
 * AcquireGlobalHandle and ReleaseGlobalHandle for independent references.
 * @param handleId Handle accessible to the caller.
 * @param handleType Required resource type.
 * @param resourceOut Receives the resource on success; NULL is allowed.
 * @return OS_EOK, or a missing-handle or type error.
 */
KERNELAPI oserr_t KERNELABI
AcquireHandle(
    _In_  uuid_t       handleId,
    _In_  HandleType_t handleType,
    _Out_ void**       resourceOut);

/**
 * @brief Look up a resource in the caller's grant table without adding a reference.
 * Without a security context, existing kernel callers use the global table.
 * The caller must keep an existing reference valid while using the resource.
 * @param ID Handle accessible to the caller.
 * @param type Required resource type.
 * @return The resource, or NULL if unavailable or of another type.
 */
KERNELAPI void* KERNELABI
LookupHandle(
    _In_ uuid_t       ID,
    _In_ HandleType_t type);

#endif //! __HANDLES_HANDLE_H__
