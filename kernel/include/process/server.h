/* MollenOS
 *
 * Copyright 2011 - 2016, Philip Meulengracht
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
 * MollenOS MCore - Server & Process Management
 * - The process/server manager is known as Phoenix
 */

#ifndef _MCORE_SERVER_H_
#define _MCORE_SERVER_H_

/* Includes
* - C-Library */
#include <os/osdefs.h>

/* Includes 
 * - System */
#include <process/phoenix.h>

/* Redirection of definitions from Ash.h
 * to make it more sensible for server tasks */
#define SERVER_CURRENT			PHOENIX_CURRENT
#define SERVER_NO_SERVER		PHOENIX_NO_ASH

/* The base of an server process, servers
 * are derived from Ashes, and merely extensions
 * to support userland stuff, and run in a larger
 * memory segment to allow for device memory */
typedef struct _MCoreServer
{
	/* We derive from Ashes */
	MCoreAsh_t Base;

	/* We want to be able to keep track 
	 * of some driver-features that we have
	 * available, like io-space memory */
	Addr_t ReservedMemoryPointer; //perhaps a bitmap?

	/* Also we allow for data arguments to
	 * to be copied into the userspace on startup */
	void *ArgumentBuffer;
	size_t ArgumentLength;

	/* Return Code */
	int ReturnCode;

} MCoreServer_t;

/* This function loads the executable and
 * prepares the ash-server-environment, at this point
 * it won't be completely running yet, it needs its own thread for that */
__CRT_EXTERN PhxId_t PhoenixCreateServer(MString_t *Path, void *Arguments, size_t Length);

/* Cleans up all the process-specific resources allocated
 * this this AshProcess, and afterwards call the base-cleanup */
__CRT_EXTERN void PhoenixCleanupProcess(MCoreProcess_t *Process);

/* Get Process 
 * This function looks up a process structure 
 * by id, if either PROCESS_CURRENT or PROCESS_NO_PROCESS 
 * is passed, it retrieves the current process */
__CRT_EXTERN MCoreProcess_t *PhoenixGetProcess(PhxId_t ProcessId);

 /* Get the working directory 
 * This function looks up the working directory for a process 
 * by id, if either PROCESS_CURRENT or PROCESS_NO_PROCESS 
 * is passed, it retrieves the current process's working directory */
__CRT_EXTERN MString_t *PhoenixGetWorkingDirectory(PhxId_t ProcessId);

/* Get the base directory 
 * This function looks up the base directory for a process 
 * by id, if either PROCESS_CURRENT or PROCESS_NO_PROCESS 
 * is passed, it retrieves the current process's base directory */
__CRT_EXTERN MString_t *PhoenixGetBaseDirectory(PhxId_t ProcessId);

#endif //!_MCORE_SERVER_H_
