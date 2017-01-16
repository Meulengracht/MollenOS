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
#include <os/osdefs.h>

/* Server base definitions, includes things
 * like the base server-target */
#define __SERVER_TARGET(Index)				((IpcComm_t)0x8000 + Index)

/* MollenOS possible server targets */
#define __DEVICEMANAGER_TARGET				__SERVER_TARGET(0)
#define __FILEMANAGER_TARGET				__SERVER_TARGET(1)
#define __WINDOWMANAGER_TARGET				__SERVER_TARGET(2)

/* RegisterServer 
 * Registers a server on the current alias, allowing
 * other applications and frameworks to send commands
 * and function requests */
_MOS_API OsStatus_t RegisterServer(IpcComm_t Alias);

#endif //!_MCORE_SERVER_H_
