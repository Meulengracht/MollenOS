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

#include <ds/collection.h>
#include <threading.h>
#include <events.h>
#include <heap.h>

/* Prototypes */
void EventHandlerInternal(void *Args);

/* Event Init/Destruct
 * Starts or stops handling events with the given callback */
MCoreEventHandler_t*
EventInit(
    const char *Name,
    EventCallback Callback,
    void *Data)
{
    /* Allocate a new instance */
    MCoreEventHandler_t *EventHandler = 
        (MCoreEventHandler_t*)kmalloc(sizeof(MCoreEventHandler_t));

    EventHandler->Name      = MStringCreate((void*)Name, StrUTF8);
    EventHandler->Events    = CollectionCreate(KeyInteger);
    SlimSemaphoreConstruct(&EventHandler->Lock, 0, 1000);
    
    EventHandler->Callback  = Callback;
    EventHandler->UserData  = Data;
    EventHandler->Running   = 1;
    EventHandler->ThreadId  = ThreadingCreateThread((char*)Name, 
        EventHandlerInternal, EventHandler, 0);
    return EventHandler;
}

/* Event Init/Destruct
 * Starts or stops handling events with the given callback */
void EventDestruct(MCoreEventHandler_t *EventHandler)
{
    // Variables
    CollectionItem_t *eNode = NULL;

    /* Step 1. Stop thread 
     * We do this by setting it's running
     * status to 0, and signaling it to wakeup this will cause it to stop */
    EventHandler->Running = 0;
    SlimSemaphoreSignal(&EventHandler->Lock, 1);
    MStringDestroy(EventHandler->Name);
    SlimSemaphoreDestroy(&EventHandler->Lock);
    
    /* Go through list and wakeup everything that's in it, 
     * plus we set all events to cancelled */
    _foreach(eNode, EventHandler->Events) {
        MCoreEvent_t *Event = (MCoreEvent_t*)eNode->Data;
        Event->State        = EventCancelled;
        SlimSemaphoreDestroy(&Event->Queue);
    }
    CollectionDestroy(EventHandler->Events);
    kfree(EventHandler);
}

/* Event Handler 
 * This is the 'shared' function that makes sure to redirect the 
 * event to the user-specifed callback */
void EventHandlerInternal(void *Args)
{
    /* Get the event-handler for this thread */
    MCoreEventHandler_t *EventHandler = (MCoreEventHandler_t*)Args;
    MCoreEvent_t *Event = NULL;
    CollectionItem_t *eNode = NULL;

    /* Start the while loop */
    while (EventHandler->Running) {
        // Wait for next event
        SlimSemaphoreWait(&EventHandler->Lock, 0);
        if (!EventHandler->Running) {
            break;
        }

        
        eNode = CollectionPopFront(EventHandler->Events);
        if (eNode == NULL) {
            continue;
        }

        Event = (MCoreEvent_t*)eNode->Data;
        kfree(eNode);
        if (Event == NULL) {
            continue;
        }

        /* Make sure event hasn't been cancelled 
         * before we process it */
        if (Event->State != EventCancelled) {
            Event->State = EventInProgress;
            EventHandler->Callback(EventHandler->UserData, Event);
        }
        SlimSemaphoreSignal(&Event->Queue, 1);
        if (Event->Cleanup != 0) {
            kfree(Event);
        }
    }
    EventDestruct(EventHandler);
}

/* Event Create
 * Queues up a new event for the event handler to process asynchronous operation */
void EventCreate(MCoreEventHandler_t *EventHandler, MCoreEvent_t *Event)
{
    /* DataKey for list */
    DataKey_t Key;
    Key.Value = 0;

    /* Get owner and save it to request */
    Event->Owner = ThreadingGetCurrentThreadId();
    Event->State = EventPending;

    /* Reset the semaphore */
    SlimSemaphoreConstruct(&Event->Queue, 0, 1000);
    CollectionAppend(EventHandler->Events, CollectionCreateNode(Key, Event));
    SlimSemaphoreSignal(&EventHandler->Lock, 1);
}

/* Event Wait
 * Waits for a specific event to either complete, fail or be cancelled */
void EventWait(MCoreEvent_t *Event, size_t Timeout)
{
    SlimSemaphoreWait(&Event->Queue, Timeout);
}

/* Event Cancel
 * Cancels a specific event, event might not be cancelled immediately */
void EventCancel(MCoreEvent_t *Event)
{
    if (Event->State != EventPending && Event->State != EventInProgress) {
        return;  
    }
    Event->State = EventCancelled;
}
