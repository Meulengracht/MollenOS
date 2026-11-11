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

#ifndef __HANDLES_GLOBAL_H__
#define __HANDLES_GLOBAL_H__

#include <os/osdefs.h>
#include <handles/types.h>

/**
 * @brief Create a handle with one independent kernel reference.
 * No process grant is created, even when the caller has a security context.
 * On failure, resource cleanup remains the caller's responsibility.
 * @param handleType Type of the resource.
 * @param cleanup Optional final cleanup, deferred to the janitor.
 * @param resource Initialized resource to associate with the handle.
 * @return The handle ID, or UUID_INVALID on failure.
 */
KERNELAPI uuid_t KERNELABI
CreateGlobalHandle(
    _In_ HandleType_t       handleType,
    _In_ HandleDestructorFn cleanup,
    _In_ void*              resource);

/**
 * @brief Add an independent kernel reference without checking process grants.
 * The caller must establish that global access is allowed for the operation.
 * IPC sending, for example, accepts global IDs but must validate the IPC buffer.
 * The returned resource remains valid until released with ReleaseGlobalHandle.
 * CPU sessions and DMA access resources cannot have independent references.
 * @param handleId Handle to reference.
 * @param handleType Required resource type.
 * @param resourceOut Receives the resource on success; NULL is allowed.
 * @return OS_EOK, or a missing-handle, type, permission or overflow error.
 */
KERNELAPI oserr_t KERNELABI
AcquireGlobalHandle(
    _In_  uuid_t       handleId,
    _In_  HandleType_t handleType,
    _Out_ void**       resourceOut);

/**
 * @brief Look up a kernel-managed resource without checking the current grant table.
 * Use when resolving the current security context itself, or when the kernel
 * already holds a reference to the resource. The caller must keep that reference
 * valid while using the returned pointer. No reference is added by this call.
 * Never use this to authorize a handle supplied by userspace; use the grant-checked
 * lookup or acquire functions at syscall boundaries.
 * @param ID Kernel-managed handle whose lifetime is already protected.
 * @param type Required resource type.
 * @return The resource, or NULL if the handle is absent, destroying or of another type.
 */
KERNELAPI void* KERNELABI
LookupGlobalHandle(
    _In_ uuid_t       ID,
    _In_ HandleType_t type);

/**
 * @brief Check the caller's grant and add an independent kernel reference.
 * Use for backing storage that may outlive its creating security context.
 * CPU sessions and DMA access resources are not eligible.
 * Release with ReleaseGlobalHandle.
 * 
 * @param handleId Handle accessible to the caller.
 * @param handleType Required resource type.
 * @param resourceOut Receives the resource on success.
 * @return OS_EOK, or a missing-handle, type, permission or overflow error.
 */
KERNELAPI oserr_t KERNELABI
AcquireHandleReference(
    _In_  uuid_t       handleId,
    _In_  HandleType_t handleType,
    _Out_ void**       resourceOut);

/**
 * @brief Drop an independent kernel reference from any context, including janitor.
 * Process grant references are not modified. Final cleanup is deferred to the
 * janitor. Each successful creation or acquisition must be released once.
 * @param handleId Handle from CreateGlobalHandle, AcquireGlobalHandle or
 * AcquireHandleReference.
 * @return OS_EOK when destruction is queued, or OS_EINCOMPLETE while references
 * remain. Both results mean the reference was released. OS_ENOENT means the
 * handle is absent or already queued for destruction.
 */
KERNELAPI oserr_t KERNELABI
ReleaseGlobalHandle(
    _In_ uuid_t handleId);

DECL_STRUCT(MSContextSecurity);

/**
 * @brief Creates a handle like CreateHandle, but the first reference is added to
 * the grant table of the given group instead of the calling one. The calling
 * process gets no grant for the handle, unless it belongs to the given group. This
 * is how the kernel creates a handle on behalf of another process.
 * The caller must have checked that the group is allowed to get the handle, and
 * must keep the group alive for the whole call. If securityContext is NULL, the
 * first reference is a kernel reference instead, which the caller must release
 * with ReleaseGlobalHandle.
 *
 * @param handleType      The type of the handle.
 * @param grantCleanup    Optional cleanup, called when a group drops its last
 *                        reference to the handle, or the group exits.
 * @param globalCleanup   Optional cleanup, called when the handle is destroyed. This
 *                        is done by the janitor thread once all references are gone.
 * @param resource        The resource the handle points to. On failure the caller
 *                        must clean it up, and neither cleanup function is called.
 * @param securityContext The group that gets the first reference, or NULL for a
 *                        kernel reference.
 * @return The ID of the new handle, or UUID_INVALID on failure.
 */
KERNELAPI uuid_t KERNELABI
CreateHandleInContext(
    _In_ HandleType_t         handleType,
    _In_ HandleDestructorFn   grantCleanup,
    _In_ HandleDestructorFn   globalCleanup,
    _In_ void*                resource,
    _In_ MSContextSecurity_t* securityContext);

/**
 * @brief Drops a reference to a handle from the grant table of the given group,
 * like DestroyHandle does for the calling group. The caller must have checked
 * that this is allowed, and must keep the group alive for the whole call. This
 * is used to undo an import done on behalf of another group, when the new handle
 * ID could not be copied to userspace (see ScHandleImport).
 *
 * @param securityContext The group to drop the reference from, or NULL to drop a
 *                        kernel reference instead.
 * @param handleId        The handle to drop a reference to.
 * @return OS_EOK or OS_EINCOMPLETE, which both mean the reference was dropped, or
 *         OS_ENOENT if there was no such reference to drop.
 */
KERNELAPI oserr_t KERNELABI
DestroyHandleInContext(
    _In_ MSContextSecurity_t* securityContext,
    _In_ uuid_t               handleId);

/**
 * @brief Checks that the given group has a grant for a handle, and if so adds a
 * global reference to it. Release the reference again with ReleaseGlobalHandle.
 * The caller must keep the security context and its grant table valid for the
 * whole call. With a NULL context the grant check is skipped, and the handle is
 * looked up directly in the global table. This is only meant for the kernel
 * itself.
 *
 * @param context     Security context of the group to check, or NULL.
 * @param handleId    Handle ID in the group's grant table. For service handles this
 *                    is the reference ID, which can differ from the service's resource ID.
 * @param handleType  Required kernel handle type.
 * @param resourceOut Receives the resource on success. May be NULL.
 * @return OS_EOK, or a grant, type or reference-count error.
 */
oserr_t
AcquireHandleReferenceInContext(
    _In_ MSContextSecurity_t* context,
    _In_ uuid_t handleId,
    _In_ HandleType_t handleType,
    _Out_ void** resourceOut);

#endif //! __HANDLES_GLOBAL_H__
