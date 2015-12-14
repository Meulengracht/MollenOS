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
* MollenOS MCore - Shared Memory System
*/

/* Includes */
#include <Arch.h>
#include <ProcessManager.h>
#include <Threading.h>
#include <Scheduler.h>
#include <Log.h>

/* Shorthand */
#define DefineSyscall(_Sys) ((Addr_t)&_Sys)

/***********************
 * Process Functions   *
 ***********************/
PId_t ScProcessSpawn(void)
{
	/* Alloc on stack */
	MCoreProcessRequest_t Request;

	/* Setup */
	Request.Type = ProcessSpawn;
	Request.Cleanup = 0;
	Request.Path = NULL;
	Request.Arguments = NULL;
	
	/* Fire! */
	PmCreateRequest(&Request);
	PmWaitRequest(&Request);

	/* Done */
	return Request.ProcessId;
}

int ScProcessJoin(PId_t ProcessId)
{
	/* Wait for process */
	MCoreProcess_t *Process = PmGetProcess(ProcessId);

	/* Sanity */
	if (Process == NULL)
		return -1;

	/* Sleep */
	SchedulerSleepThread((Addr_t*)Process);
	_ThreadYield();

	/* Return the exit code */
	return Process->ReturnCode;
}

int ScProcessKill(PId_t ProcessId)
{
	/* Alloc on stack */
	MCoreProcessRequest_t Request;

	/* Setup */
	Request.Type = ProcessKill;
	Request.Cleanup = 0;
	Request.ProcessId = ProcessId;

	/* Fire! */
	PmCreateRequest(&Request);
	PmWaitRequest(&Request);

	/* Return the exit code */
	if (Request.State == ProcessRequestOk)
		return 0;
	else
		return -1;
}

void ScProcessExit(int ExitCode)
{
	/* Disable interrupts */
	IntStatus_t IntrState = InterruptDisable();
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process = PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);

	/* Save return code */
	LogDebug("SYSC", "Process %s terminated with code %i", Process->Name->Data, ExitCode);
	Process->ReturnCode = ExitCode;

	/* Terminate all threads used by process */
	ThreadingTerminateProcessThreads(Process->Id);

	/* Mark process for reaping */
	PmTerminateProcess(Process);

	/* Enable Interrupts */
	InterruptRestoreState(IntrState);
}

void ScProcessYield(void)
{
	/* Deep Call */
	_ThreadYield();
}

/***********************
* Threading Functions  *
***********************/


/***********************
* Memory Functions     *
***********************/
Addr_t ScMemoryAllocate(size_t Size, int Flags)
{

}

int ScMemoryFree(Addr_t Adress, size_t Length)
{

}

/***********************
* IPC Functions        *
***********************/


/***********************
* VFS Functions        *
***********************/






/* NoP */
void NoOperation(void)
{

}

/* Syscall Table */
Addr_t GlbSyscallTable[51] =
{
	/* Kernel Log */
	DefineSyscall(LogDebug),

	/* Process Functions */
	DefineSyscall(ScProcessExit),
	DefineSyscall(ScProcessYield),
	DefineSyscall(ScProcessSpawn),
	DefineSyscall(ScProcessJoin),
	DefineSyscall(ScProcessKill),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* Threading Functions */
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* Memory Functions */
	DefineSyscall(ScMemoryAllocate),
	DefineSyscall(ScMemoryFree),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* IPC Functions */
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* Vfs Functions */
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation)
};