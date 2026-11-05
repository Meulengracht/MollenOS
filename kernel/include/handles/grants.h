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
 * @brief Grants the specified handle to the given security context.
 * The caller must keep both security contexts valid and prevent the destination
 * handle table from being destroyed during this call. If the destination already
 * has a grant, the call succeeds without adding a reference.
 * CPU sessions, DMA access resources and token control handles cannot be granted
 * to another context.
 *
 * @param securityContext [In] The security context to grant the handle to.
 * @param handleId        [In] The handle to grant.
 */
KERNELAPI oserr_t KERNELABI
GrantHandle(
    _In_ MSContextSecurity_t* securityContext,
    _In_ uuid_t               handleId);

/**
 * @brief Creates a grant token for an SHM buffer in the caller's grant table.
 * The caller must have a current security context. SHM_PRIVATE buffers cannot be shared.
 * If the buffer was created with SHMExport, its original memory must stay valid
 * until the export and all references added by verification have been released.
 * options and tokenOut must point to kernel memory. Only use tokenOut on success.
 * The fixed secret is temporary and must be replaced before production use.
 *
 * @param handleID   SHM handle in the caller's grant table.
 * @param options    Requested access, buffer range and allowed receiver.
 * @param tokenOut   Receives the token on success.
 * @return OS_EOK on success, or a validation or resource error.
 */
__EXTERN oserr_t
GrantTokenCreate(
    _In_  uuid_t                     handleID,
    _In_  const GrantTokenOptions_t* options,
    _Out_ GrantToken_t*              tokenOut);

/**
 * @brief Disables an export and closes the caller's control handle.
 * The caller must have a grant for the control handle. References added by
 * successful verification calls remain valid until released.
 *
 * @param controlHandleID Control handle returned by GrantTokenCreate.
 * @return OS_EOK on success, or a handle validation error.
 */
__EXTERN oserr_t
GrantTokenRevoke(
    _In_ uuid_t controlHandleID);

/**
 * @brief Verifies a grant token and adds a reference to its SHM buffer.
 * token and handleIDOut must point to kernel memory. The current security group
 * is the receiver, and scope must match the token's Read or Write value exactly.
 * The kernel operation using the returned handle must enforce the token's range
 * and scope, then release the reference with ReleaseGlobalHandle when done.
 * This call creates no process grant or mapping. Verification must be part of
 * the kernel operation that uses the buffer. The returned ID must not give
 * userspace unrestricted access to it.
 *
 * @param token       Token to copy and verify.
 * @param scope       Requested access to the buffer.
 * @param handleIDOut Receives the SHM handle with one added reference on success.
 *                    Unchanged on failure.
 * @return OS_EOK on success, OS_EINVALPARAMS for an invalid token,
 *         OS_EPERMISSIONS if the receiver or scope does not match,
 *         OS_ECANCELLED if the export was revoked, OS_ENOTSUPPORTED without a
 *         current security context, or an error from adding the SHM reference.
 *         Receiver, scope and revoke errors are reported only after the token
 *         fields have been verified.
 */
__EXTERN oserr_t
GrantTokenVerify(
    _In_  const GrantToken_t* token,
    _In_  GrantTokenScope_t   scope,
    _Out_ uuid_t*             handleIDOut);

#endif //! __HANDLES_GRANTS_H__
