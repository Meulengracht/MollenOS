/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Session Service Definitions & Structures
 * - This header describes the base session-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __SERVICES_SESSION_H__
#define __SERVICES_SESSION_H__

#include <os/osdefs.h>
#include <os/types/session.h>

/**
 * @brief Sends a login request to the session manager.
 * @param User User name.
 * @param Password User password.
 * @param Result Receives the session result.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
SessionLoginRequest(
	_In_ const char*      User,
    _In_ const char*      Password,
    _In_ SessionObject_t* Result));

/**
 * @brief Sends a logout request for an active session.
 * @param SessionId Identifier returned by the login request.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
SessionLogoutRequest(
	_In_ const char* SessionId));

#endif //!__SERVICES_SESSIONS_H__
