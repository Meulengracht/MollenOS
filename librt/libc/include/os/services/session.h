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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
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

/* SessionLoginRequest
 * Sends a login-request to the session-manager. The sessionmanager will respond
 * with a SessionObject structure containing information about success/failure. */
CRTDECL(OsStatus_t,
SessionLoginRequest(
	_In_ const char*      User,
    _In_ const char*      Password,
    _In_ SessionObject_t* Result));

/* SessionLogoutRequest
 * Sends a logout-request to the session-manager. The acquired session-id from
 * the login must be used to logout the correct user. */
CRTDECL(OsStatus_t,
SessionLogoutRequest(
	_In_ const char* SessionId));

#endif //!__SERVICES_SESSIONS_H__
