/* MollenOS
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Event Queue Support Definitions & Structures
 * - This header describes the base event-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */
#include <ds/collection.h>
#include <os/eventqueue.h>
#include <os/mollenos.h>
#include <threads.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#define EVENT_QUEUED    0
#define EVENT_EXECUTED  1
#define EVENT_CANCELLED 2

typedef struct _EventQueueEvent {
    CollectionItem_t   Header;
    EventQueueFunction Function;
    void*              Context;
    size_t             Timeout;
    size_t             Interval;
    int                State;
} EventQueueEvent_t;

typedef struct _EventQueue {
    int           IsRunning;
    UUId_t        NextEventId;
    thrd_t        EventThread;
    mtx_t         EventLock;
    cnd_t         EventCondition;
    Collection_t* Events;
} EventQueue_t;

static UUId_t AddToEventQueue(EventQueue_t* EventQueue, EventQueueFunction Function, void* Context, size_t TimeoutMs, size_t IntervalMs);
static int    EventQueueWorker(void* Context);

void CreateEventQueue(EventQueue_t** EventQueueOut)
{
    EventQueue_t* EventQueue = malloc(sizeof(EventQueue_t));
    assert(EventQueue != NULL);
    
    EventQueue->Events       = CollectionCreate(KeyId);
    EventQueue->IsRunning    = 1;
    EventQueue->NextEventId  = 1;
    
    if (thrd_create(&EventQueue->EventThread, EventQueueWorker, EventQueue) != thrd_success) {
        EventQueue->EventThread = UUID_INVALID;
        DestroyEventQueue(EventQueue);
        return;
    }
    
    mtx_init(&EventQueue->EventLock, mtx_plain);
    cnd_init(&EventQueue->EventCondition);
    *EventQueueOut = EventQueue;
}

void DestroyEventQueue(EventQueue_t* EventQueue)
{
    int Unused;

    // Kill the thread, then cleanup resources
    if (EventQueue->EventThread != UUID_INVALID) {
        EventQueue->IsRunning = 0;
        cnd_signal(&EventQueue->EventCondition);
        thrd_join(EventQueue->EventThread, &Unused);
        mtx_destroy(&EventQueue->EventLock);
        cnd_destroy(&EventQueue->EventCondition);
    }
    CollectionDestroy(EventQueue->Events);
    free(EventQueue);
}

void QueueEvent(EventQueue_t* EventQueue, EventQueueFunction Callback, void* Context)
{
    AddToEventQueue(EventQueue, Callback, Context, 0, 0);
}

UUId_t QueueDelayedEvent(EventQueue_t* EventQueue, EventQueueFunction Callback, void* Context, size_t DelayMs)
{
    return AddToEventQueue(EventQueue, Callback, Context, DelayMs, 0);
}

UUId_t QueuePeriodicEvent(EventQueue_t* EventQueue, EventQueueFunction Callback, void* Context, size_t IntervalMs)
{
    if (IntervalMs == 0) {
        return UUID_INVALID;
    }
    return AddToEventQueue(EventQueue, Callback, Context, IntervalMs, IntervalMs);
}

OsStatus_t CancelEvent(EventQueue_t* EventQueue, UUId_t EventHandle)
{
    DataKey_t          Key = { .Value.Id = EventHandle };
    EventQueueEvent_t* Event;
    OsStatus_t         Status = OsDoesNotExist;
    
    mtx_lock(&EventQueue->EventLock);
    Event = (EventQueueEvent_t*)CollectionGetNodeByKey(EventQueue->Events, Key, 0);
    if (Event != NULL && Event->State != EVENT_EXECUTED) {
        Event->State = EVENT_CANCELLED;
        Status = OsSuccess;
    }
    mtx_unlock(&EventQueue->EventLock);
    return Status;
}

static UUId_t AddToEventQueue(EventQueue_t* EventQueue, EventQueueFunction Function, void* Context, size_t TimeoutMs, size_t IntervalMs)
{
    EventQueueEvent_t* Event = (EventQueueEvent_t*)malloc(sizeof(EventQueueEvent_t));
    assert(Event != NULL);
    
    assert(EventQueue != NULL);
    assert(Function != NULL);

    memset(Event, 0, sizeof(EventQueueEvent_t));
    Event->Header.Key.Value.Id = EventQueue->NextEventId++;

    Event->Function = Function;
    Event->Context  = Context;
    Event->Timeout  = TimeoutMs;
    Event->Interval = IntervalMs;
    
    mtx_lock(&EventQueue->EventLock);
    CollectionAppend(EventQueue->Events, &Event->Header);
    mtx_unlock(&EventQueue->EventLock);
    cnd_signal(&EventQueue->EventCondition);
    return Event->Header.Key.Value.Id;
}

static EventQueueEvent_t* GetNearestEventQueueDeadline(EventQueue_t* EventQueue)
{
    EventQueueEvent_t* Nearest = NULL;
    foreach(Node, EventQueue->Events) {
        EventQueueEvent_t* Event = (EventQueueEvent_t*)Node;
        if (Nearest == NULL) {
            Nearest = Event;
        }
        else {
            if (Event->Timeout < Nearest->Timeout) {
                Nearest = Event;
            }
        }
    }
    return Nearest;
}

static int EventQueueWorker(void* Context)
{
    EventQueueEvent_t* Event;
    EventQueue_t*   EventQueue = (EventQueue_t*)Context;
    struct timespec TimePoint;
    struct timespec InterruptedAt;
    struct timespec TimeSpent;
    SetCurrentThreadName("event-pump");

    mtx_lock(&EventQueue->EventLock);
    while (EventQueue->IsRunning) {
        Event = GetNearestEventQueueDeadline(EventQueue);
        if (Event != NULL) {
            timespec_get(&TimePoint, TIME_UTC);
            TimePoint.tv_nsec += Event->Timeout * NSEC_PER_MSEC;
            if (TimePoint.tv_nsec > NSEC_PER_SEC) {
                TimePoint.tv_nsec -= NSEC_PER_SEC;
                TimePoint.tv_sec++;
            }

            if (cnd_timedwait(&EventQueue->EventCondition, &EventQueue->EventLock, &TimePoint) == thrd_timedout) {
                // We timedout, or in other words successfully waited
                if (Event->State != EVENT_CANCELLED) {
                    Event->State = EVENT_EXECUTED;
                    
                    mtx_unlock(&EventQueue->EventLock);
                    Event->Function(Event->Context);
                    mtx_lock(&EventQueue->EventLock);
                    if (Event->Interval != 0) {
                        Event->Timeout = Event->Interval;
                    }
                }
                if (Event->State == EVENT_CANCELLED || !Event->Interval) {
                    CollectionRemoveByNode(EventQueue->Events, &Event->Header);
                    CollectionDestroyNode(EventQueue->Events, &Event->Header);
                }
            }
            else {
                if (Event->State != EVENT_CANCELLED) {
                    // We were interrupted due to added events, calculate sleep time and subtract. Then
                    // start over
                    timespec_get(&InterruptedAt, TIME_UTC);
                    timespec_diff(&InterruptedAt, &TimePoint, &TimeSpent);
                    size_t Milliseconds = TimeSpent.tv_sec * MSEC_PER_SEC;
                    Milliseconds       += TimeSpent.tv_nsec / NSEC_PER_MSEC;
                    Event->Timeout     -= Milliseconds;
                }
                else {
                    CollectionRemoveByNode(EventQueue->Events, &Event->Header);
                    CollectionDestroyNode(EventQueue->Events, &Event->Header);
                }
                continue;
            }
        }
        else {
            // Wait for event to be added
            cnd_wait(&EventQueue->EventCondition, &EventQueue->EventLock);
        }
    }
    mtx_unlock(&EventQueue->EventLock);
    return 0;
}
