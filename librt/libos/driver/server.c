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

/* Includes
 * - System */
#include <os/driver/server.h>
#include <os/syscall.h>

/* RegisterServer 
 * Registers a server on the current alias, allowing
 * other applications and frameworks to send commands
 * and function requests */
OsStatus_t RegisterServer(IpcComm_t Alias)
{
	/* Redirect a syscall, it does all */
	return (OsStatus_t)Syscall1(
		SYSCALL_SERVERREGISTER, SYSCALL_PARAM(Alias));
}
