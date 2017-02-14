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
 * MollenOS MCore - Process Definitions & Structures
 * - This header describes the process structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _PROCESS_INTERFACE_H_
#define _PROCESS_INTERFACE_H_

/* Includes
 * - Library */
#include <os/osdefs.h>

/* Process Queries
 * List of the different options
 * for process queries */
typedef enum _ProcessQueryFunction {
	ProcessQueryName,
	ProcessQueryMemory,
	ProcessQueryParent,
	ProcessQueryTopMostParent
} ProcessQueryFunction_t;

/* ProcessSpawn
 * Spawns a new process by the given path and
 * optionally the given parameters are passed 
 * returns UUID_INVALID in case of failure */
_MOS_API UUId_t ProcessSpawn(_In_ __CONST char *Path, 
							 _In_Opt_ __CONST char *Arguments);

/* ProcessJoin
 * Waits for the given process to terminate and
 * returns the return-code the process exit'ed with */
_MOS_API int ProcessJoin(_In_ UUId_t Process);

/* Process Kill
 * Terminates the process with the given id */
_MOS_API OsStatus_t ProcessKill(_In_ UUId_t Process);

/* Process Query
 * Queries information about the given process
 * based on the function it returns the requested information */
_MOS_API OsStatus_t ProcessQuery(_In_ UUId_t Process, 
								 _In_ ProcessQueryFunction_t Function, 
								 _In_ void *Buffer, 
								 _In_ size_t Length);

#endif //!_PROCESS_INTERFACE_H_
