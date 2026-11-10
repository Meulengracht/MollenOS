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

#ifndef __HANDLES_GRANTS_H__
#define __HANDLES_GRANTS_H__

#include <os/osdefs.h>
#include <handles/types.h>

DECL_STRUCT(MSContextSecurity);
DECL_STRUCT(HandleGrantsTable);

KERNELAPI HandleGrantsTable_t* KERNELABI CreateHandleGrantsTable(void);

/**
 * @brief Releases all grants in a security group's handle table and frees the table.
 * The caller must prevent further access to the table before destroying it.
 *
 * @param table Handle table to destroy.
 */
KERNELAPI void KERNELABI
DestroyHandleGrantsTable(
    _In_ HandleGrantsTable_t* table);

/**
 * @brief Grants the specified handle to the given security context.
 * The caller must keep both security contexts valid and prevent the destination
 * handle table from being destroyed during this call. If the destination already
 * has a grant, the call succeeds without adding a reference.
 * Service registrations, CPU sessions, DMA access resources and token control
 * handles cannot be granted to another context.
 *
 * @param securityContext [In] The security context to grant the handle to.
 * @param handleId        [In] The handle to grant.
 */
KERNELAPI oserr_t KERNELABI
GrantHandle(
    _In_ MSContextSecurity_t* securityContext,
    _In_ uuid_t               handleId);

/**
 * @brief Creates a typed resource grant token from the caller's grant table.
 * The caller must have a current security context. Only GrantTokenResourceTypeSHM
 * and GrantTokenResourceTypeProvider are supported. SHM_PRIVATE buffers cannot be shared.
 * options and tokenOut must point to kernel memory. Only use tokenOut on success.
 * The fixed secret is temporary and must be replaced before production use.
 *
 * @param handleID   Resource handle in the caller's grant table.
 * @param options    Resource type, scope, typed restrictions and allowed receiver.
 * @param tokenOut   Receives the token on success.
 * @return OS_EOK, OS_ENOTSUPPORTED for an unsupported resource type, or a
 *         validation or resource error.
 */
__EXTERN oserr_t
GrantTokenCreate(
    _In_  uuid_t                     handleID,
    _In_  const GrantTokenOptions_t* options,
    _Out_ GrantToken_t*              tokenOut);

/**
 * @brief Disables an export and closes the caller's control handle.
 * The caller must have a grant for the control handle. Source references handed
 * out by successful GrantTokenImport calls stay valid until they are released.
 *
 * @param controlHandleID Control handle returned by GrantTokenCreate.
 * @return OS_EOK on success, or a handle validation error.
 */
__EXTERN oserr_t
GrantTokenRevoke(
    _In_ uuid_t controlHandleID);

/**
 * @brief Checks a typed token and imports it, giving the caller a reference to
 * its resource.
 * token and handleIDOut must point to kernel memory. The current security group
 * is the receiver. The resource type must match expectedType, and every
 * requested scope flag must be allowed by the authenticated policy. SHM tokens
 * require a nonzero scope, while GrantTokenResourceTypeProvider tokens also
 * accept a zero scope (identity checks only).
 * The export status is checked and the import is done under the export lock,
 * so a revoke or a second import of a single-use token cannot happen in between.
 * A reusable token adds a new reference to the resource. A single-use token is
 * revoked by this call, and the export's own resource reference is handed to
 * the caller. In this case the token stays used up, even if the caller fails to
 * create its receiving handle afterwards.
 * The calling resource module must enforce the policy restrictions before it
 * exposes any access, and then release the reference with ReleaseGlobalHandle.
 * The reference to the export record is released before returning. This call
 * creates no process grant or mapping.
 *
 * @param token        Kernel token to copy and authenticate.
 * @param expectedType Resource type required by the caller.
 * @param scope        Requested access flags for that resource type.
 * @param handleIDOut  Receives the source handle with one reference for the
 *                     caller on success. Unchanged on failure.
 * @return OS_EOK on success. Invalid tokens and resource-type mismatches return
 *         OS_EINVALPARAMS. Receiver or scope failures return OS_EPERMISSIONS.
 *         Revoked or used-up exports return OS_ECANCELLED. A missing security
 *         context returns OS_ENOTSUPPORTED. Resource reference errors are passed
 *         through. Type, receiver, scope and revoke checks are only done after
 *         the token has been authenticated.
 */
__EXTERN oserr_t
GrantTokenImport(
    _In_  const GrantToken_t*      token,
    _In_  GrantTokenResourceType_t expectedType,
    _In_  GrantTokenScope_t        scope,
    _Out_ uuid_t*                  handleIDOut);

#endif //! __HANDLES_GRANTS_H__
