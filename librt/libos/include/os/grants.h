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

#ifndef __OS_GRANTS_H__
#define __OS_GRANTS_H__

#include <os/types/grants.h>

/**
 * @brief Creates a grant token for a resource handle in the caller's grant table.
 * Only the SHM resource type is supported, and the buffer must not have
 * SHM_PRIVATE set. Do not share the token before this call has succeeded.
 * The fixed secret must be replaced before production use.
 *
 * @param handleID Handle of the resource in the caller's grant table.
 * @param options Resource type, scope, limits for that resource type and the
 *                receiver that may use the token.
 * @param tokenOut Receives the token on success. Do not use its contents on failure,
 *                 even if some bytes were written. A failed export cannot be used.
 * @return OS_EOK, OS_ENOTSUPPORTED for an unsupported resource type, or a
 *         validation, resource or user-memory copy error.
 */
CRTDECL(oserr_t,
OSGrantTokenCreate(
    _In_  uuid_t                     handleID,
    _In_  const GrantTokenOptions_t* options,
    _Out_ GrantToken_t*              tokenOut));

/**
 * @brief Revokes an export and closes the caller's control handle.
 * The caller must have a grant for the control handle. A copy of the token
 * does not give that grant. Existing imported references and mappings remain valid.
 *
 * @param controlHandleID ControlHandleID returned by OSGrantTokenCreate.
 * @return OS_EOK on success, or a handle validation error.
 */
CRTDECL(oserr_t,
OSGrantTokenRevoke(
    _In_ uuid_t controlHandleID));

#endif //!__OS_GRANTS_H__
