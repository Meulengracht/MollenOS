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
 * MollenOS MCore - Service Definitions & Structures
 * - This header describes the base service-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _SERVICE_H_
#define _SERVICE_H_

/* Includes
 * - System */
#include <os/ipc/ipc.h>
#include <os/osdefs.h>

/* Service base definitions, includes things
 * like the base server-target */
#define __SERVICE_TARGET(Index)				((UUId_t)0x8000 + Index)

/* MollenOS possible Service targets */
#define __DEVICEMANAGER_TARGET				__SERVICE_TARGET(0)
#define __FILEMANAGER_TARGET				__SERVICE_TARGET(1)
#define __WINDOWMANAGER_TARGET				__SERVICE_TARGET(2)
#define __USBMANAGER_TARGET				__SERVICE_TARGET(3)

/* RegisterService 
 * Registers a service on the current alias, allowing
 * other applications and frameworks to send commands
 * and function requests */
MOSAPI 
OsStatus_t
MOSABI
RegisterService(
	_In_ UUId_t Alias);

/* OnLoad
 * The entry-point of a service, this is called
 * as soon as the server is loaded in the system */
#ifdef __SERVICE_IMPL
__EXTERN 
OsStatus_t
OnLoad(void);
#endif

/* OnUnload
 * This is called when the service is being unloaded
 * and should free all resources allocated by the system */
#ifdef __SERVICE_IMPL
__EXTERN 
OsStatus_t 
OnUnload(void);
#endif

/* OnEvent
 * This is called when the service recieved an external event
 * and should handle the given event*/
#ifdef __SERVICE_IMPL
__EXTERN 
OsStatus_t 
OnEvent(
	_In_ MRemoteCall_t *Message);
#endif

#endif //!_SERVICE_H_
