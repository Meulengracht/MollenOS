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
* MollenOS MCore - Generic Event System
*/

/* Includes */
#include <scheduler.h>
#include <events.h>
#include <heap.h>
#include <log.h>

/* C-Library */
#include <stddef.h>
#include <ds/list.h>

/* Prototypes */
void EventHandlerInternal(void *Args);

/* Event Init/Destruct
 * Starts or stops handling events
 * with the given callback */
MCoreEventHandler_t *EventInit(const char *Name, EventCallback Callback, void *Data)
{
	/* Allocate a new instance */
	MCoreEventHandler_t *EventHandler = 
		(MCoreEventHandler_t*)kmalloc(sizeof(MCoreEventHandler_t));

	/* Allocate lock etc */
	EventHandler->Name = MStringCreate((void*)Name, StrUTF8);
	EventHandler->Events = ListCreate(KeyInteger, LIST_SAFE);
	EventHandler->Lock = SemaphoreCreate(0);
	
	/* Save stuff */
	EventHandler->Callback = Callback;
	EventHandler->UserData = Data;

	/* Activate us! */
	EventHandler->Running = 1;

	/* Start a thread bound to this
	 * instance */
	EventHandler->ThreadId = ThreadingCreateThread((char*)Name, 
		EventHandlerInternal, EventHandler, 0);

	/* Done! */
	return EventHandler;
}

/* Event Init/Destruct
 * Starts or stops handling events
 * with the given callback */
void EventDestruct(MCoreEventHandler_t *EventHandler)
{
	/* Variables */
	ListNode_t *eNode = NULL;

	/* Step 1. Stop thread 
	 * We do this by setting it's running
	 * status to 0, and signaling it to wakeup 
	 * this will cause it to stop */
	EventHandler->Running = 0;
	SemaphoreV(EventHandler->Lock, 1);

	/* Stop 2. Cleanup */
	MStringDestroy(EventHandler->Name);
	SemaphoreDestroy(EventHandler->Lock);
	
	/* Go through list and wakeup
	 * everything that's in it, 
	 * plus we set all events to cancelled */
	_foreach(eNode, EventHandler->Events)
	{
		/* Cast */
		MCoreEvent_t *Event = (MCoreEvent_t*)eNode->Data;

		/* Update status */
		Event->State = EventCancelled;

		/* Wakeup sleepers */
		SchedulerThreadWakeAll((uintptr_t*)Event);
	}

	/* Lastly, destroy list 
	 * and cleanup the event handler */
	ListDestroy(EventHandler->Events);
	kfree(EventHandler);
}

/* Event Handler 
 * This is the 'shared' function
 * that makes sure to redirect the 
 * event to the user-specifed callback */
void EventHandlerInternal(void *Args)
{
	/* Get the event-handler for this thread */
	MCoreEventHandler_t *EventHandler = (MCoreEventHandler_t*)Args;
	MCoreEvent_t *Event = NULL;
	ListNode_t *eNode = NULL;

	/* Start the while loop */
	while (EventHandler->Running) {
		// Wait for next event
		SemaphoreP(EventHandler->Lock, 0);

		/* Sanitize our running status 
		 * before doing anything else */
		if (!EventHandler->Running)
			break;

		/* Pop from event queue */
		eNode = ListPopFront(EventHandler->Events);

		/* Sanity */
		if (eNode == NULL)
			continue;

		/* Cast */
		Event = (MCoreEvent_t*)eNode->Data;

		/* Cleanup */
		kfree(eNode);

		/* Sanity */
		if (Event == NULL)
			continue;

		/* Make sure event hasn't been cancelled 
		 * before we process it */
		if (Event->State != EventCancelled) {
			Event->State = EventInProgress;
			EventHandler->Callback(EventHandler->UserData, Event);
		}

		/* Signal Completion */
		SemaphoreV(&Event->Queue, 1);

		/* Cleanup? */
		if (Event->Cleanup != 0) {
			kfree(Event);
		}	
	}

	// Cleanup 
	EventDestruct(EventHandler);
}

/* Event Create
 * Queues up a new event for the
 * event handler to process
 * Asynchronous operation */
void EventCreate(MCoreEventHandler_t *EventHandler, MCoreEvent_t *Event)
{
	/* DataKey for list */
	DataKey_t Key;
	Key.Value = 0;

	/* Get owner and save it to request */
	Event->Owner = ThreadingGetCurrentThreadId();
	Event->State = EventPending;

	/* Reset the semaphore */
	SemaphoreConstruct(&Event->Queue, 0);

	/* Add to list */
	ListAppend(EventHandler->Events, ListCreateNode(Key, Key, Event));

	/* Signal */
	SemaphoreV(EventHandler->Lock, 1);
}

/* Event Wait
 * Waits for a specific event
 * to either complete, fail or
 * be cancelled */
void EventWait(MCoreEvent_t *Event, size_t Timeout)
{
	/* Try to acquire one piece of candy
	 * from the queue */
	SemaphoreP(&Event->Queue, Timeout);
}

/* Event Cancel
 * Cancels a specific event,
 * event might not be cancelled
 * immediately */
void EventCancel(MCoreEvent_t *Event)
{
	/* Sanity, make sure request hasn't completed */
	if (Event->State != EventPending
		&& Event->State != EventInProgress)
		return;

	/* Update status to cancelled */
	Event->State = EventCancelled;
}
