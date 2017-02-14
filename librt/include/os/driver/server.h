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
 * MollenOS MCore - Server Definitions & Structures
 * - This header describes the base server-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _SERVER_H_
#define _SERVER_H_

/* Includes
 * - System */
#include <os/ipc/ipc.h>
#include <os/osdefs.h>

/* Server base definitions, includes things
 * like the base server-target */
#define __SERVER_TARGET(Index)				((UUId_t)0x8000 + Index)

/* MollenOS possible server targets */
#define __DEVICEMANAGER_TARGET				__SERVER_TARGET(0)
#define __FILEMANAGER_TARGET				__SERVER_TARGET(1)
#define __WINDOWMANAGER_TARGET				__SERVER_TARGET(2)

/* RegisterServer 
 * Registers a server on the current alias, allowing
 * other applications and frameworks to send commands
 * and function requests */
_MOS_API OsStatus_t RegisterServer(UUId_t Alias);

/* OnLoad
 * The entry-point of a server, this is called
 * as soon as the server is loaded in the system */
#ifdef __SERVER_IMPL
__EXTERN OsStatus_t OnLoad(void);
#endif

/* OnUnload
 * This is called when the server is being unloaded
 * and should free all resources allocated by the system */
#ifdef __SERVER_IMPL
__EXTERN OsStatus_t OnUnload(void);
#endif

/* OnEvent
 * This is called when the server recieved an external evnet
 * and should handle the given event*/
#ifdef __SERVER_IMPL
__EXTERN OsStatus_t OnEvent(MRemoteCall_t *Message);
#endif

#endif //!_MCORE_SERVER_H_
