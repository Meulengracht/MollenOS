/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * Session Manager Interface
 * - Part of the SDK. Provides user session related functionality through the session manager.
 */

#ifndef __SDK_SESSIONS_H__
#define __SDK_SESSIONS_H__

#include <os/osdefs.h>
#include <time.h>

// IPC Function Declarations
#define __SESSIONMANAGER_CHECKUP           IPC_DECL_FUNCTION(0)
#define __SESSIONMANAGER_LOGIN             IPC_DECL_FUNCTION(1)
#define __SESSIONMANAGER_LOGOUT            IPC_DECL_FUNCTION(2)

#define __SESSIONMANAGER_CREATE_PROCESS    IPC_DECL_FUNCTION(3)
#define __SESSIONMANAGER_JOIN_PROCESS      IPC_DECL_FUNCTION(4)
#define __SESSIONMANAGER_KILL_PROCESS      IPC_DECL_FUNCTION(5)
#define __SESSIONMANAGER_TERMINATE_PROCESS IPC_DECL_FUNCTION(6)
#define __SESSIONMANAGER_GET_PROCESS_ID    IPC_DECL_FUNCTION(7)
#define __SESSIONMANAGER_GET_ARGUMENTS     IPC_DECL_FUNCTION(8)
#define __SESSIONMANAGER_GET_INHERIT_BLOCK IPC_DECL_FUNCTION(9)
#define __SESSIONMANAGER_GET_PROCESS_NAME  IPC_DECL_FUNCTION(10)

#define __SESSIONMANAGER_GET_ASSEMBLY_DIRECTORY IPC_DECL_FUNCTION(11)
#define __SESSIONMANAGER_GET_WORKING_DIRECTORY  IPC_DECL_FUNCTION(12)
#define __SESSIONMANAGER_SET_WORKING_DIRECTORY  IPC_DECL_FUNCTION(13)

#define __SESSIONMANAGER_GET_LIBRARY_HANDLES IPC_DECL_FUNCTION(14)
#define __SESSIONMANAGER_GET_LIBRARY_ENTRIES IPC_DECL_FUNCTION(15)
#define __SESSIONMANAGER_LOAD_LIBRARY        IPC_DECL_FUNCTION(16)
#define __SESSIONMANAGER_RESOLVE_FUNCTION    IPC_DECL_FUNCTION(17)
#define __SESSIONMANAGER_UNLOAD_LIBRARY      IPC_DECL_FUNCTION(18)

/* SessionObject 
 * Response object from login-requests and session inquiries. */
PACKED_TYPESTRUCT(SessionObject, {
    OsStatus_t              Status;

    char                    SessionId[16];
    time_t                  LastLogin;
 /* UserProfile_t           Profile; */
});

/* SessionCheckDisk
 * Notifies the sessionmanager if a new accessible system disk. */
CRTDECL(OsStatus_t,
SessionCheckDisk(
	_In_ const char* DiskIdentifier));

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

#endif //!__SDK_SESSIONS_H__
