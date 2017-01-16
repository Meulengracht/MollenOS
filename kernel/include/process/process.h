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

#ifndef _MCORE_PROCESS_H_
#define _MCORE_PROCESS_H_

/* Includes
* - C-Library */
#include <os/osdefs.h>

/* Includes 
 * - System */
#include <process/phoenix.h>

/* Redirection of definitions from Ash.h
 * to make it more sensible for processing */
#define PROCESS_CURRENT			PHOENIX_CURRENT
#define PROCESS_NO_PROCESS		PHOENIX_NO_ASH

/* The base of an process, processes
 * are derived from Ashes, and merely extensions
 * to support userland stuff */
typedef struct _MCoreProcess
{
	/* We derive from Ashes */
	MCoreAsh_t Base;

	/* Working Directory */
	MString_t *WorkingDirectory;
	MString_t *BaseDirectory;

	/* Open Files */
	List_t *OpenFiles;
	MString_t *Arguments;

} MCoreProcess_t;

/* This function loads the executable and
 * prepares the ash-process-environment, at this point
 * it won't be completely running yet, it needs
 * its own thread for that */
__CRT_EXTERN UUId_t PhoenixCreateProcess(MString_t *Path, MString_t *Arguments);

/* Cleans up all the process-specific resources allocated
 * this this AshProcess, and afterwards call the base-cleanup */
__CRT_EXTERN void PhoenixCleanupProcess(MCoreProcess_t *Process);

/* Get Process 
 * This function looks up a process structure 
 * by id, if either PROCESS_CURRENT or PROCESS_NO_PROCESS 
 * is passed, it retrieves the current process */
__CRT_EXTERN MCoreProcess_t *PhoenixGetProcess(UUId_t ProcessId);

 /* Get the working directory 
 * This function looks up the working directory for a process 
 * by id, if either PROCESS_CURRENT or PROCESS_NO_PROCESS 
 * is passed, it retrieves the current process's working directory */
__CRT_EXTERN MString_t *PhoenixGetWorkingDirectory(UUId_t ProcessId);

/* Get the base directory 
 * This function looks up the base directory for a process 
 * by id, if either PROCESS_CURRENT or PROCESS_NO_PROCESS 
 * is passed, it retrieves the current process's base directory */
__CRT_EXTERN MString_t *PhoenixGetBaseDirectory(UUId_t ProcessId);

#endif //!_MCORE_PROCESS_H_