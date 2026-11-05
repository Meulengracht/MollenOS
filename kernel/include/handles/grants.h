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
 * The caller must keep both contexts alive and exclude destination-table teardown
 * for this call. Repeated grants succeed without changing destination references.
 * CPU sessions and DMA access resources cannot be granted to another context.
 * @param securityContext [In] The security context to grant the handle to.
 * @param handleId        [In] The handle to grant.
 */
KERNELAPI oserr_t KERNELABI
GrantHandle(
    _In_ MSContextSecurity_t* securityContext,
    _In_ uuid_t               handleId);

/**
 * @brief Create an export from a handle accessible to the caller.
 * The resource must support the requested access and receiver restriction.
 *
 * @param handleID   Resource handle in the caller's grant table.
 * @param options    Access, range, and receiver restriction.
 * @param resultOut  Receives the token and its local control handle on success.
 * @return OS_EOK on success, or a validation or resource error.
 */
__EXTERN oserr_t
GrantTokenCreate(
    _In_  uuid_t                     handleID,
    _In_  const GrantTokenOptions_t* options,
    _Out_ GrantTokenCreateResult_t*  resultOut);

/**
 * @brief Revoke an export and close the caller's control reference.
 * Existing imports continue under their established reference rules.
 *
 * @param controlHandleID Local control handle returned by creation.
 * @return OS_EOK on success, or a handle validation error.
 */
__EXTERN oserr_t
GrantTokenRevoke(
    _In_ uuid_t controlHandleID);

#endif //! __HANDLES_GRANTS_H__
