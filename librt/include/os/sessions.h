/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
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
 * MollenOS User Session Interface
 * - MollenOS SDK 
 */

#ifndef __SDK_SESSIONS_H__
#define __SDK_SESSIONS_H__

/* Includes
 * - Library */
#include <os/osdefs.h>
#include <os/ipc/ipc.h>
#include <os/service.h>
#include <time.h>

/* These are the different IPC functions supported
 * by the driver, note that some of them might
 * be changed in the different versions, and/or new
 * functions will be added */
#define __SESSIONMANAGER_CHECKUP        IPC_DECL_FUNCTION(0)
#define __SESSIONMANAGER_LOGIN          IPC_DECL_FUNCTION(1)
#define __SESSIONMANAGER_LOGOUT         IPC_DECL_FUNCTION(2)

/* SessionObject 
 * Response object from login-requests and session inquiries. */
PACKED_TYPESTRUCT(SessionObject, {
    OsStatus_t              Status;

    char                    SessionId[16];
    time_t                  LastLogin;
 /* UserProfile_t           Profile; */
});

/* SessionCheckDisk
 * Notifies the sessionmanager of a new accessible system disk. */
SERVICEAPI
OsStatus_t
SERVICEABI
SessionCheckDisk(
	_In_ __CONST char *DiskIdentifier)
{
	// Variables
	MRemoteCall_t Request;

	// Initialze RPC
	RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_CHECKUP);
	RPCSetArgument(&Request, 0, (__CONST void*)DiskIdentifier, strlen(DiskIdentifier) + 1);

	// Send
	return RPCEvent(&Request);
}

/* SessionLoginRequest
 * Sends a login-request to the session-manager. The sessionmanager will respond
 * with a SessionObject structure containing information about success/failure. */
SERVICEAPI
OsStatus_t
SERVICEABI
SessionLoginRequest(
	_In_ __CONST char *User,
    _In_ __CONST char *Password,
    _Out_ SessionObject_t *Result)
{
	// Variables
	MRemoteCall_t Request;

	// Initialze RPC
	RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_LOGIN);
	RPCSetArgument(&Request, 0, (__CONST void*)User, strlen(User) + 1);
    RPCSetArgument(&Request, 1, (__CONST void*)Password, strlen(Password) + 1);
    RPCSetResult(&Request, (__CONST void*)Result, sizeof(SessionObject_t));

	// Send
	return RPCExecute(&Request);
}

/* SessionLogoutRequest
 * Sends a logout-request to the session-manager. The acquired session-id from
 * the login must be used to logout the correct user. */
SERVICEAPI
OsStatus_t
SERVICEABI
SessionLogoutRequest(
	_In_ __CONST char *SessionId)
{
	// Variables
	MRemoteCall_t Request;
    OsStatus_t Result   = OsError;

	// Initialze RPC
	RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_LOGOUT);
	RPCSetArgument(&Request, 0, (__CONST void*)SessionId, strlen(SessionId) + 1);
    RPCSetResult(&Request, (__CONST void*)&Result, sizeof(OsStatus_t));

	// Send
	if (RPCExecute(&Request) != OsSuccess) {
        return OsError;
    }
    return Result;
}

#endif //!__SDK_SESSIONS_H__
