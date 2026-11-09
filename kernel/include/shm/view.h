/**
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
 */

#ifndef __SHM_VIEW_H__
#define __SHM_VIEW_H__

#include <handles/types.h>
#include <os/types/shm.h>

struct SHMBuffer;

/**
 * @brief Represents a restricted SHM handle.
 */
typedef struct SHMView {
    struct SHMBuffer* Buffer;
    size_t            Offset;
    size_t            Length;
    GrantTokenScope_t Scope;
} SHMView_t;

/**
 * @brief Resolved SHM storage and limits protected by one independent reference.
 * Release GlobalHandleID with ReleaseGlobalHandle. View is NULL for ordinary SHM.
 * Offset and Length identify the permitted logical range within Buffer.
 */
typedef struct SHMReference {
    uuid_t            GlobalHandleID;
    HandleType_t      Type;
    struct SHMBuffer* Buffer;
    SHMView_t*        View;
    size_t            Offset;
    size_t            Length;
    GrantTokenScope_t Scope;
} SHMReference_t;

/**
 * @brief Validate SHM export policy and add an independent source reference.
 * The caller must select the SHM resource type before calling this function.
 * @param handle Ordinary buffer or restricted view granted to the caller.
 * @param options Kernel snapshot with SHM scope and byte-range restrictions.
 * @param handleTypeOut Receives the actual kernel handle type on success.
 * @return OS_EOK or a handle, range or scope error. On success, release handle
 *         with ReleaseGlobalHandle when the export no longer needs it.
 */
oserr_t
SHMReferenceForExport(
    _In_  uuid_t                     handle,
    _In_  const GrantTokenOptions_t* options,
    _Out_ HandleType_t*              handleTypeOut);

/**
 * @brief Map a range relative to a restricted handle.
 * The caller must hold an independent reference to view during the call.
 * @param viewID Global reference keeping view valid during the call.
 * @param view Fixed range and scope.
 * @param handle Receives the new mapping on success.
 * @param offset Token-relative first byte.
 * @param length Requested byte count.
 * @param access SHM_ACCESS flags for the new mapping.
 * @return OS_EOK or a range, scope, mapping or allocation error. If mapping
 *         cleanup fails, return that error and keep its allocation for retry.
 */
oserr_t
SHMViewMap(
    _In_    uuid_t       viewID,
    _In_    SHMView_t*   view,
    _InOut_ SHMHandle_t* handle,
    _In_    size_t       offset,
    _In_    size_t       length,
    _In_    unsigned int access);

/**
 * @brief Retry failed mapping cleanup in the current memory space.
 * @param viewID Handle identifying the failed restricted mappings.
 * @return OS_EOK when no cleanup remains, or the unmap error. Failed allocations
 *         stay tracked so a later map or detach can retry.
 */
oserr_t
SHMViewCleanup(
    _In_ uuid_t viewID);

/**
 * @brief Commit backing pages for an existing restricted mapping.
 * The caller must hold an independent reference to view.
 * @param view Restricted range and scope for the commit.
 * @param address First virtual byte within a compatible restricted allocation.
 * @param length Requested byte count.
 * @return OS_EOK or a mapping, scope or allocation error.
 */
oserr_t
SHMViewCommit(
    _In_ SHMView_t* view,
    _In_ vaddr_t    address,
    _In_ size_t     length);

/**
 * @brief Commit one faulting SHM page using the mapping's established limits.
 * Restricted mappings use their independent reference after the handle is closed.
 * Ordinary mappings use the caller's grant.
 * @param handle Backing tag from the VM descriptor.
 * @param base Start address from the VM descriptor.
 * @param address Faulting virtual address.
 * @return OS_EOK or a mapping, allocation or handle error.
 */
oserr_t
SHMCommitFault(
    _In_ uuid_t  handle,
    _In_ vaddr_t base,
    _In_ vaddr_t address);

/**
 * @brief Validate the scope and full range of backing pages exposed by a mapping.
 * @param view Referenced view with Fixed limits.
 * @param offset Logical backing offset of the first exposed page.
 * @param length Full span of the exposed pages.
 * @param flags Kernel mapping attributes requested by the operation.
 * @return OS_EOK, OS_EINVALPARAMS or OS_EPERMISSIONS.
 */
oserr_t
SHMViewValidateMapping(
    _In_ const SHMView_t* view,
    _In_ size_t           offset,
    _In_ size_t           length,
    _In_ unsigned int     flags);

/**
 * @brief Resolve an ordinary or restricted SHM handle through the caller's grant.
 * @param handle Handle accessible to the caller.
 * @param reference Receives the independent reference on success.
 * @return OS_EOK or a handle validation error. Output is unchanged on failure.
 */
oserr_t
SHMResolve(
    _In_  uuid_t          handle,
    _Out_ SHMReference_t* reference);

/**
 * @brief Resolve the source reference added by token verification.
 * The result describes the supplied reference without adding another.
 * @param handle Verified source handle with an independent reference.
 * @param reference Receives storage, type and limits.
 */
void
SHMResolveVerified(
    _In_  uuid_t          handle,
    _Out_ SHMReference_t* reference);

/**
 * @brief Create a caller-granted restricted handle from a complete kernel token.
 * @param token Kernel snapshot of the token to verify.
 * @param handle Receives an unmapped attachment. Unchanged on failure.
 * @return OS_EOK or a token, range, permission or allocation error.
 */
oserr_t
SHMAttachToken(
    _In_  const GrantToken_t* token,
    _Out_ SHMHandle_t*        handle);

/**
 * @brief Create a restricted view from an already referenced source.
 * @param source Referenced storage and its current limits.
 * @param offset First byte relative to the source's permitted range.
 * @param length Nonempty byte count within that range.
 * @param scope Nonzero access flags contained in the source's scope.
 * @param handle Receives an unmapped attachment on success.
 * @return OS_EOK or a range, permission or allocation error.
 */
oserr_t
SHMViewCreate(
    _In_  const SHMReference_t* source,
    _In_  size_t                offset,
    _In_  size_t                length,
    _In_  GrantTokenScope_t     scope,
    _Out_ SHMHandle_t*          handle);

#endif
