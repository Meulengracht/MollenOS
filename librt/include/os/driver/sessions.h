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
#include <os/driver/service.h>

/* These are the different IPC functions supported
 * by the driver, note that some of them might
 * be changed in the different versions, and/or new
 * functions will be added */
#define __SESSIONMANAGER_CHECKUP        IPC_DECL_FUNCTION(0)

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
	RPCInitialize(&Request, 1, PIPE_RPCOUT, __SESSIONMANAGER_CHECKUP);
	RPCSetArgument(&Request, 0, (__CONST void*)DiskIdentifier, strlen(DiskIdentifier) + 1);

	// Send
	return RPCEvent(&Request, __SESSIONMANAGER_TARGET);
}

#endif //!__SDK_SESSIONS_H__
