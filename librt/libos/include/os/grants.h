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
 * @brief Creates a grant token for an SHM buffer in the caller's grant table.
 * The buffer must not have SHM_PRIVATE set. If it was created with SHMExport,
 * its original memory must stay valid until the export and all imported
 * references have been released. Only share the token after this call succeeds.
 * The fixed secret must be replaced before production use.
 *
 * @param handleID SHM handle in the caller's grant table.
 * @param options Requested access, buffer range and allowed receiver.
 * @param tokenOut Receives the token on success. Ignore its contents on failure,
 *                 even if some bytes were written. The failed export cannot be used.
 * @return OS_EOK, a validation or resource error, or a user-memory copy error.
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
