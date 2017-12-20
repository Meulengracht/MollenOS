/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS Service - Session Manager
 * - Contains the implementation of the session-manager which keeps track
 *   of all users on the device.
 */
#define __TRACE

/* Includes
 * - System */
#include <os/driver/service.h>
#include <os/driver/sessions.h>
#include <os/process.h>
#include <os/utils.h>
#include <string.h>

/* Globals
 * - State keeping variables */
static UUId_t WindowingSystemId = UUID_INVALID;

/* OnLoad
 * The entry-point of a server, this is called
 * as soon as the server is loaded in the system */
OsStatus_t
OnLoad(void)
{
    // Register us with server manager
	return RegisterService(__SESSIONMANAGER_TARGET);
}

/* OnUnload
 * This is called when the server is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t
OnUnload(void)
{
    return OsSuccess;
}

/* OnEvent
 * This is called when the server recieved an external evnet
 * and should handle the given event*/
OsStatus_t
OnEvent(
	_In_ MRemoteCall_t *Message)
{
	// Variables
    OsStatus_t Result = OsSuccess;
    char PathBuffer[64];
    
    // Debug
    TRACE("Sessionmanager.OnEvent(%i)", Message->Function);

    // New function call!
    switch (Message->Function) {
        case __SESSIONMANAGER_CHECKUP: {
#ifndef __OSCONFIG_DISABLE_VIOARR
            if (WindowingSystemId == UUID_INVALID) {
                // The identifier might be stored as a value here if less than a specific
                // amount of bytes
                char *DiskIdentifier = NULL;
                if (Message->Arguments[0].Type == ARGUMENT_REGISTER) {
                    DiskIdentifier = (char*)&Message->Arguments[0].Data.Value;
                }
                else {
                    DiskIdentifier = (char*)Message->Arguments[0].Data.Buffer;
                }

                // Clear up buffer and spawn app
                memset(&PathBuffer[0], 0, sizeof(PathBuffer));
                sprintf(&PathBuffer[0], "%s:/shared/bin/cpptest.app", DiskIdentifier); //%s/system/vioarr.app
                TRACE("Spawning %s", &PathBuffer[0]);
                WindowingSystemId = ProcessSpawn(&PathBuffer[0], NULL, 0);
            }
#endif
        } break;
        case __SESSIONMANAGER_LOGIN: {

        } break;
        case __SESSIONMANAGER_LOGOUT: {

        } break;

        default: {
            break;
        }
    }
    return Result;
}
