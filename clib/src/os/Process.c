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
* MollenOS - Process Functions
*/

/* Includes */
#include <os/MollenOS.h>
#include <os/Syscall.h>

/* Kernel Guard */
#ifdef LIBC_KERNEL
void __ProcessLibCEmpty(void)
{
}
#else

/* Process Spawn
 * This function spawns a new process 
 * in it's own address space, and returns
 * the new process id 
 * If startup is failed, the returned value 
 * is 0xFFFFFFFF */
PId_t ProcessSpawn(const char *Path, const char *Arguments)
{
	/* Variables */
	int RetVal = 0;

	/* Syscall! */
	RetVal = Syscall2(MOLLENOS_SYSCALL_PROCSPAWN, MOLLENOS_SYSCALL_PARAM(Path),
		MOLLENOS_SYSCALL_PARAM(Arguments));

	/* Done */
	return (PId_t)RetVal;
}

/* Process Join 
 * Attaches to a running process 
 * and waits for the process to quit
 * the return value is the return code
 * from the target process */
int ProcessJoin(PId_t ProcessId)
{
	/* Variables */
	int RetVal = 0;

	/* Syscall! */
	RetVal = Syscall1(MOLLENOS_SYSCALL_PROCJOIN, MOLLENOS_SYSCALL_PARAM(ProcessId));

	/* Done */
	return RetVal;
}

/* Process Kill 
 * Kills target process id 
 * On error, returns -1, or if the 
 * kill was succesful, returns 0 */
int ProcessKill(PId_t ProcessId)
{
	/* Variables */
	int RetVal = 0;

	/* Sanity -- Who the 
	 * fuck would try to kill 
	 * window server */
	if (ProcessId == 0)
		return -1;

	/* Syscall! */
	RetVal = Syscall1(MOLLENOS_SYSCALL_PROCKILL, MOLLENOS_SYSCALL_PARAM(ProcessId));

	/* Done */
	return RetVal;
}

#endif