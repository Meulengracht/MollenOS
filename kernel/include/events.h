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

#ifndef _MCORE_EVENTS_H_
#define _MCORE_EVENTS_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <ds/collection.h>

/* Includes
 * - System */
#include <semaphore.h>

/* This describes the current state
 * of an event, this means the sender
 * can check the status all the sender
 * wants*/
typedef enum _EventState {
    EventPending,
    EventInProgress,
    EventOk,
    EventFailed,
    EventCancelled
} EventState_t;

/* Generic format used for the
 * different enums.. they are all ints anyway */
typedef int Enum_t;

/* Generic Event/Request 
 * Describes the BASE of an event that might occur, 
 * it's important to 'inherit' this */
typedef struct _MCoreEvent {
    UUId_t              Owner;
    Enum_t              Type;
    EventState_t        State;
    Semaphore_t         Queue;
    int                 Cleanup;
} MCoreEvent_t;

/* Define the default event callback
 * it passes on the event that has been
 * sent */
typedef int (*EventCallback)(void*, MCoreEvent_t*);

/* Generic Event Handler 
 * This describes an event handler 
 * The event handler keeps track of 
 * events and locks */
typedef struct _MCoreEventHandler {
    MString_t           *Name;
    UUId_t               ThreadId;
    int                  Running;
    Collection_t        *Events;
    Semaphore_t         *Lock;
    EventCallback        Callback;
    void                *UserData;
} MCoreEventHandler_t;

/* Event Init/Destruct 
 * Starts or stops handling events 
 * with the given callback */
KERNELAPI MCoreEventHandler_t *EventInit(const char *Name, EventCallback Callback, void *Data);
KERNELAPI void EventDestruct(MCoreEventHandler_t *EventHandler);

/* Event Create 
 * Queues up a new event for the
 * event handler to process 
 * Asynchronous operation */
KERNELAPI void EventCreate(MCoreEventHandler_t *EventHandler, MCoreEvent_t *Event);

/* Event Wait 
 * Waits for a specific event 
 * to either complete, fail or 
 * be cancelled */
KERNELAPI void EventWait(MCoreEvent_t *Event, size_t Timeout);

/* Event Cancel 
 * Cancels a specific event, 
 * event might not be cancelled 
 * immediately */
KERNELAPI void EventCancel(MCoreEvent_t *Event);

#endif //!_MCORE_EVENTS_H_
