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
 * MollenOS MCore - Server & Process Management
 * - The process/server manager is known as Phoenix
 */

/* Includes 
 * - System */
#include <process/phoenix.h>
#include <process/process.h>
#include <process/server.h>
#include <garbagecollector.h>
#include <threading.h>
#include <scheduler.h>
#include <heap.h>
#include <log.h>

/* Includes
 * C-Library */
#include <stddef.h>
#include <ds/list.h>
#include <ds/mstring.h>
#include <string.h>

/* Prototypes 
 * They are defined later down this file */
int PhoenixEventHandler(void *UserData, MCoreEvent_t *Event);
OsStatus_t PhoenixReap(void *UserData);

/* Globals */
MCoreEventHandler_t *GlbPhoenixEventHandler = NULL;
UUId_t GlbAshIdGenerator = 0;
List_t *GlbAshes = NULL;
UUId_t *GlbAliasMap = NULL;
UUId_t GlbPhoenixGcId = 0;

/* Initialize the Phoenix environment and 
 * start the event-handler loop, it handles all requests 
 * and nothing happens if it isn't started */
void PhoenixInit(void)
{
	/* Debug */
	LogInformation("PHNX", "Initializing environment and event handler");

	/* Reset */
	GlbAshIdGenerator = 1;

	/* Create */
	GlbAshes = ListCreate(KeyInteger, LIST_SAFE);
	GlbPhoenixGcId = GcRegister(PhoenixReap);

	/* Initialize the global alias map */
	GlbAliasMap = (UUId_t*)kmalloc(sizeof(UUId_t) * PHOENIX_MAX_ASHES);
	memset(GlbAliasMap, 0xFF, sizeof(sizeof(UUId_t) * PHOENIX_MAX_ASHES));

	/* Create event handler */
	GlbPhoenixEventHandler = EventInit("Phoenix Event Handler", PhoenixEventHandler, NULL);
}

/* Create Request
 * We simply move it on to the event handler */
void PhoenixCreateRequest(MCorePhoenixRequest_t *Request) {
	EventCreate(GlbPhoenixEventHandler, &Request->Base);
}

/* Wait for request
 * just as above we just call further the event handler */
void PhoenixWaitRequest(MCorePhoenixRequest_t *Request, size_t Timeout) {
	EventWait(&Request->Base, Timeout);
}

/* The Phoenix event handler thread
 * this routine is invoked every-time there is any
 * request pending for us */
int PhoenixEventHandler(void *UserData, MCoreEvent_t *Event)
{
	/* Variables needed for this  */
	MCorePhoenixRequest_t *Request = NULL;

	/* Unused */
	_CRT_UNUSED(UserData);

	/* Cast */
	Request = (MCorePhoenixRequest_t*)Event;

	/* Depends on request */
	switch (Request->Base.Type)
	{
		/* Spawn a new ash/process */
		case AshSpawnProcess:
		case AshSpawnServer:
		{
			/* Deep Call */
			LogInformation("PHNX", "Spawning %s", MStringRaw(Request->Path));

			if (Request->Base.Type == AshSpawnServer) {
				Request->AshId = PhoenixCreateServer(Request->Path, 
					Request->Arguments.Raw.Data, Request->Arguments.Raw.Length);
			}
			else {
				Request->AshId = PhoenixCreateProcess(Request->Path, Request->Arguments.String);
			}

			/* Sanity */
			if (Request->AshId != PHOENIX_NO_ASH)
				Request->Base.State = EventOk;
			else
				Request->Base.State = EventFailed;

		} break;

		/* Kill ash */
		case AshKill:
		{
			/* Lookup ash in our list */
			MCoreAsh_t *Ash = PhoenixGetAsh(Request->AshId);

			/* Sanity */
			if (Ash != NULL) {
				ThreadingTerminateAshThreads(Ash->Id);
				PhoenixTerminateAsh(Ash);
			}
			else {
				Request->Base.State = EventFailed;
			}
				
		} break;

		/* Panic */
		default: {
			LogDebug("PHNX", "Unhandled Event %u", (size_t)Request->Base.Type);
		} break;
	}

	/* Cleanup? */
	if (Request->Base.Cleanup != 0) {
		if (Request->Path != NULL)
			MStringDestroy(Request->Path);
		if (Request->Arguments.String != NULL)
			MStringDestroy(Request->Arguments.String);
	}

	/* Return 0 */
	return 0;
}

/* This marks an ash for termination by taking it out
 * of rotation and adding it to the cleanup list */
void PhoenixTerminateAsh(MCoreAsh_t *Ash)
{
	/* Variables needed */
	ListNode_t *pNode = NULL;
	DataKey_t Key;

	/* Lookup node */
	Key.Value = (int)Ash->Id;
	pNode = ListGetNodeByKey(GlbAshes, Key, 0);

	/* Sanity */
	if (pNode == NULL)
		return;

	/* Wake all that waits for this to finish */
	SchedulerWakeupAllThreads((uintptr_t*)pNode->Data);

	// Alert GC and destroy node
	GcSignal(GlbPhoenixGcId, pNode->Data);
	ListRemoveByNode(GlbAshes, pNode);
}

/* This function cleans up processes and
 * ashes and servers that might be queued up for
 * destruction, they can't handle all their cleanup themselves */
OsStatus_t PhoenixReap(void *UserData)
{
	// Instantiate the base-pointer
	MCoreAsh_t *Ash = (MCoreAsh_t*)UserData;

	// Clean up
	if (Ash->Type == AshBase) {
		PhoenixCleanupAsh(Ash);
	}
	else if (Ash->Type == AshProcess) {
		PhoenixCleanupProcess((MCoreProcess_t*)Ash);
	}
	else {
		//??
		return OsError;
	}

	return OsNoError;
}
