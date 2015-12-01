/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS MCore - Process Manager
*/

/* Includes */
#include <ProcessManager.h>
#include <Threading.h>
#include <Semaphore.h>
#include <Scheduler.h>
#include <Heap.h>
#include <List.h>
#include <Log.h>

/* Prototypes */
void PmEventHandler(void *Args);

/* Globals */
PId_t GlbProcessId = 0;
list_t *GlbProcesses = NULL;
list_t *GlbProcessRequests = NULL;
Semaphore_t *GlbProcessEventLock = NULL;

/* Setup & Start Request Handler */
void PmInit(void)
{
	/* Debug */
	LogInformation("PROC", "Installing Request Handler");

	/* Reset */
	GlbProcessId = 0;

	/* Create */
	GlbProcesses = list_create(LIST_SAFE);
	GlbProcessRequests = list_create(LIST_SAFE);
	GlbProcessEventLock = SemaphoreCreate(0);

	/* Start */
	ThreadingCreateThread("Process Event Thread", PmEventHandler, NULL, 0);
}

/* Event Handler */
void PmEventHandler(void *Args)
{
	/* Vars */
	list_node_t *eNode = NULL;
	MCoreProcessRequest_t *Request = NULL;

	/* Unused */
	_CRT_UNUSED(Args);

	/* Forever! */
	while (1)
	{
		/* Get event */
		SemaphoreP(GlbProcessEventLock);

		/* Pop from event queue */
		eNode = list_pop_front(GlbProcessRequests);

		/* Sanity */
		if (eNode == NULL)
			continue;

		/* Cast */
		Request = (MCoreProcessRequest_t*)eNode->data;

		/* Cleanup */
		kfree(eNode);

		/* Sanity */
		if (Request == NULL)
			continue;

		/* Set initial */
		Request->State = RequestInProgress;

		/* Depends on request */
		switch (Request->Type)
		{
			/* Spawn Process */
			case ProcessSpawn:
			{

			} break;

			/* Panic */
			default:
			{
				LogDebug("PROC", "Unhandled Event %u", (uint32_t)Request->Type);
			} break;
		}

		/* Signal Completion */
		SchedulerWakeupAllThreads((Addr_t*)Request);
	}
}