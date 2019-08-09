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
 * Network Manager
 * - Contains the implementation of the network-manager which keeps track
 *   of sockets, network interfaces and connectivity status
 */
#define __TRACE

#include <ds/collection.h>
#include <os/services/process.h>
#include <ddk/services/session.h>
#include <ddk/service.h>
#include <ddk/utils.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static UUId_t ServiceHandleGenerator = 0;

OsStatus_t
OnLoad(
    _In_ char** ServicePathOut)
{
    *ServicePathOut = "na";
}

OsStatus_t
OnUnload(void)
{
    return OsSuccess;
}

OsStatus_t
OnEvent(
	_In_ MRemoteCall_t* Message)
{
    OsStatus_t Result = OsSuccess;
    
    TRACE("Networkmanager.OnEvent(%i)", Message->Function);

    switch (Message->Function) {
        
        default: {
            break;
        }
    }
    return Result;
}
