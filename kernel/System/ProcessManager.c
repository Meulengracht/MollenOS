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
* MollenOS MCore - Processes Implementation
*/

/* Includes */
#include <Arch.h>
#include <Process.h>
#include <Vfs/Vfs.h>
#include <GarbageCollector.h>
#include <Threading.h>
#include <Semaphore.h>
#include <Scheduler.h>
#include <Log.h>

/* CLib */
#include <stddef.h>
#include <ds/list.h>
#include <ds/mstring.h>
#include <string.h>

/* Prototypes */
int PmEventHandler(void *UserData, MCoreEvent_t *Event);
PId_t PmCreateProcess(MString_t *Path, MString_t *Arguments);

/* Globals */
MCoreEventHandler_t *GlbProcessEventHandler = NULL;
PId_t GlbProcessId = 0;
List_t *GlbProcesses = NULL;
List_t *GlbZombieProcesses = NULL;

/* Setup & Start Request Handler */
void PmInit(void)
{
	/* Debug */
	LogInformation("PROC", "Installing Request Handler");

	/* Reset */
	GlbProcessId = 1;

	/* Create */
	GlbProcesses = ListCreate(KeyInteger, LIST_SAFE);
	GlbZombieProcesses = ListCreate(KeyInteger, LIST_SAFE);

	/* Create event handler */
	GlbProcessEventHandler = EventInit("Process Manager", PmEventHandler, NULL);
}

/* Create Request
* We simply move it on to
* the event handler */
void PmCreateRequest(MCoreProcessRequest_t *Request)
{
	/* Deep call */
	EventCreate(GlbProcessEventHandler, &Request->Base);
}

/* Wait for request
* just as above we just call further
* the event handler */
void PmWaitRequest(MCoreProcessRequest_t *Request, size_t Timeout)
{
	/* Deep Call */
	EventWait(&Request->Base, Timeout);
}

/* Event Handler */
int PmEventHandler(void *UserData, MCoreEvent_t *Event)
{
	/* Vars */
	MCoreProcessRequest_t *Request = NULL;

	/* Unused */
	_CRT_UNUSED(UserData);

	/* Cast */
	Request = (MCoreProcessRequest_t*)Event;

	/* Depends on request */
	switch (Request->Base.Type)
	{
		/* Spawn Process */
	case ProcessSpawn:
	{
		/* Deep Call */
		LogInformation("PROC", "Spawning %s", Request->Path->Data);
		Request->ProcessId = PmCreateProcess(Request->Path, Request->Arguments);

		/* Sanity */
		if (Request->ProcessId != 0xFFFFFFFF)
			Request->Base.State = EventOk;
		else
			Request->Base.State = EventFailed;

	} break;

	/* Kill Process */
	case ProcessKill:
	{
		/* Lookup process */
		MCoreProcess_t *Process = PmGetProcess(Request->ProcessId);

		/* Sanity */
		if (Process != NULL)
		{
			/* Terminate all threads used by process */
			ThreadingTerminateProcessThreads(Process->Id);

			/* Mark process for reaping */
			PmTerminateProcess(Process);
		}
		else
			Request->Base.State = EventFailed;

	} break;

	/* Panic */
	default:
	{
		LogDebug("PROC", "Unhandled Event %u", (uint32_t)Request->Base.Type);
	} break;
	}

	/* Cleanup? */
	if (Request->Base.Cleanup != 0)
	{
		if (Request->Path != NULL)
			MStringDestroy(Request->Path);
		if (Request->Arguments != NULL)
			MStringDestroy(Request->Arguments);
	}

	/* Return 0 */
	return 0;
}

/* End Process */
void PmTerminateProcess(MCoreProcess_t *Process)
{
	/* Variables needed */
	ListNode_t *pNode = NULL;
	DataKey_t Key;

	/* Lookup node */
	Key.Value = (int)Process->Id;
	pNode = ListGetNodeByKey(GlbProcesses, Key, 0);

	/* Sanity */
	if (pNode == NULL)
		return;

	/* Remove it, add to zombies */
	ListRemoveByNode(GlbProcesses, pNode);
	ListAppend(GlbZombieProcesses, pNode);

	/* Wake all that waits for this to finish */
	SchedulerWakeupAllThreads((Addr_t*)pNode->Data);

	/* Tell GC */
	GcAddWork();
}

/* Cleans up all the unused processes */
void PmReapZombies(void)
{
	/* Reap untill list is empty */
	ListNode_t *tNode = ListPopFront(GlbZombieProcesses);

	while (tNode != NULL)
	{
		/* Cast */
		MCoreProcess_t *Process = (MCoreProcess_t*)tNode->Data;

		/* Clean it up */
		PmCleanupProcess(Process);

		/* Clean up rest */
		kfree(tNode);

		/* Get next node */
		tNode = ListPopFront(GlbZombieProcesses);
	}
}
