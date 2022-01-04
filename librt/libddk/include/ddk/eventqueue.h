/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Event Queue Support Definitions & Structures
 * - This header describes the base event-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __OS_EVENTQUEUE_H__
#define __OS_EVENTQUEUE_H__

#include <os/osdefs.h>

typedef struct EventQueue EventQueue_t;
typedef void(*EventQueueFunction)(void*);

/* CreateEventQueue
 * Creates a new event queue that can be used to queue up events based on intervals and timeouts. */
CRTDECL(OsStatus_t, CreateEventQueue(EventQueue_t** eventQueueOut));

/* DestroyEventQueue
 * Stops the event queue handler, and cleans up resources. */
CRTDECL(void, DestroyEventQueue(EventQueue_t * eventQueue));

/* QueueEvent
 * Queue up a single shot event that should fire as immediate as possible */
CRTDECL(void, QueueEvent(EventQueue_t * eventQueue, EventQueueFunction callback, void* context));

/* QueueDelayedEvent
 * Queue up a single shot event that should fire after the given delay. A delay of 0 will act as immediate. */
CRTDECL(UUId_t, QueueDelayedEvent(EventQueue_t * eventQueue, EventQueueFunction callback, void* context, size_t delayMs));

/* QueuePeriodicEvent
 * Queue up a periodic event that should fire after the given delay. An interval of 0 is invalid and will not be queue up. */
CRTDECL(UUId_t, QueuePeriodicEvent(EventQueue_t * eventQueue, EventQueueFunction callback, void* context, size_t intervalMs));

/* CancelEvent
 * Marks an event to be cancelled, this will cancel the next time the event would fire, and all subsequent occasions of that
 * event. It will not cancel an event in progress. */
CRTDECL(OsStatus_t, CancelEvent(EventQueue_t * eventQueue, UUId_t eventHandle));

#endif //!__OS_EVENTQUEUE_H__
