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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Event Queue Support Definitions & Structures
 * - This header describes the base event-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

//#define __TRACE

#include <assert.h>
#include <ddk/eventqueue.h>
#include <ddk/utils.h>
#include <ds/list.h>
#include <os/mollenos.h>
#include <stdlib.h>
#include <threads.h>

#define EVENT_QUEUED    0
#define EVENT_EXECUTED  1
#define EVENT_CANCELLED 2

struct EventQueueEvent {
    element_t          Header;
    EventQueueFunction Function;
    void*              Context;
    size_t             Timeout;
    size_t             Interval;
    int                State;
};

typedef struct EventQueue {
    int    IsRunning;
    UUId_t NextEventId;
    thrd_t EventThread;
    mtx_t  EventLock;
    cnd_t  EventCondition;
    list_t Events;
} EventQueue_t;

static UUId_t __AddToEventQueue(EventQueue_t* eventQueue, EventQueueFunction function, void* context, size_t timeoutMs, size_t intervalMs);
static int    EventQueueWorker(void* context);

OsStatus_t CreateEventQueue(EventQueue_t** EventQueueOut)
{
    EventQueue_t* eventQueue = malloc(sizeof(EventQueue_t));
    if (!eventQueue) {
        return OsOutOfMemory;
    }

    eventQueue->IsRunning   = 1;
    eventQueue->NextEventId = 1;
    list_construct(&eventQueue->Events);
    mtx_init(&eventQueue->EventLock, mtx_plain);
    cnd_init(&eventQueue->EventCondition);
    
    if (thrd_create(&eventQueue->EventThread, EventQueueWorker, eventQueue) != thrd_success) {
        eventQueue->EventThread = UUID_INVALID;
        DestroyEventQueue(eventQueue);
        return OsOutOfMemory;
    }

    *EventQueueOut = eventQueue;
    return OsSuccess;
}

static void __CleanupEvent(element_t* element, void* context)
{
    _CRT_UNUSED(context);
    free(element->value);
}

void DestroyEventQueue(EventQueue_t* eventQueue)
{
    int unused;

    // Kill the thread, then cleanup resources
    if (eventQueue->EventThread != UUID_INVALID) {
        eventQueue->IsRunning = 0;
        cnd_signal(&eventQueue->EventCondition);
        thrd_join(eventQueue->EventThread, &unused);
        mtx_destroy(&eventQueue->EventLock);
        cnd_destroy(&eventQueue->EventCondition);
    }
    list_clear(&eventQueue->Events, __CleanupEvent, NULL);
    free(eventQueue);
}

void QueueEvent(EventQueue_t* eventQueue, EventQueueFunction callback, void* context)
{
    __AddToEventQueue(eventQueue, callback, context, 0, 0);
}

UUId_t QueueDelayedEvent(EventQueue_t* eventQueue, EventQueueFunction callback, void* context, size_t delayMs)
{
    return __AddToEventQueue(eventQueue, callback, context, delayMs, 0);
}

UUId_t QueuePeriodicEvent(EventQueue_t* eventQueue, EventQueueFunction callback, void* context, size_t intervalMs)
{
    if (intervalMs == 0) {
        return UUID_INVALID;
    }
    return __AddToEventQueue(eventQueue, callback, context, intervalMs, intervalMs);
}

OsStatus_t CancelEvent(EventQueue_t* eventQueue, UUId_t eventHandle)
{
    element_t* element;
    OsStatus_t osStatus = OsDoesNotExist;
    
    mtx_lock(&eventQueue->EventLock);
    element = list_find(&eventQueue->Events, (void*)(uintptr_t)eventHandle);
    if (element) {
        struct EventQueueEvent* event = element->value;
        if (event->State != EVENT_EXECUTED) {
            event->State = EVENT_CANCELLED;
            osStatus = OsSuccess;
        }
    }
    mtx_unlock(&eventQueue->EventLock);
    return osStatus;
}

static UUId_t __AddToEventQueue(
        _In_ EventQueue_t*      eventQueue,
        _In_ EventQueueFunction function,
        _In_ void*              context,
        _In_ size_t             timeoutMs,
        _In_ size_t             intervalMs)
{
    struct EventQueueEvent* event;
    UUId_t                  eventId = UUID_INVALID;
    TRACE("__AddToEventQueue(eventQueue=0x%" PRIxIN ", timeoutMs=%" PRIuIN ", intervalMs=%" PRIuIN,
          eventQueue, timeoutMs, intervalMs);

    if (!eventQueue || !function) {
        goto exit;
    }

    event = (struct EventQueueEvent*)malloc(sizeof(struct EventQueueEvent));
    if (!event) {
        goto exit;
    }

    eventId = eventQueue->NextEventId++;

    ELEMENT_INIT(&event->Header, (uintptr_t)eventId, event);
    event->State    = EVENT_QUEUED;
    event->Function = function;
    event->Context  = context;
    event->Timeout  = timeoutMs;
    event->Interval = intervalMs;
    
    mtx_lock(&eventQueue->EventLock);
    list_append(&eventQueue->Events, &event->Header);
    mtx_unlock(&eventQueue->EventLock);
    cnd_signal(&eventQueue->EventCondition);

exit:
    TRACE("__AddToEventQueue returns=%u", eventId);
    return eventId;
}

static struct EventQueueEvent* __GetNearestDeadline(
        _In_ EventQueue_t* eventQueue)
{
    struct EventQueueEvent* nearest = NULL;
    TRACE("__GetNearestDeadline(eventQueue=0x%" PRIxIN ")");

    foreach(element, &eventQueue->Events) {
        struct EventQueueEvent* event = element->value;
        if (nearest == NULL) {
            nearest = event;
        }
        else {
            if (event->Timeout < nearest->Timeout) {
                nearest = event;
            }
        }
    }

    TRACE("__GetNearestDeadline returns=%" PRIuIN " ms",
          nearest ? nearest->Timeout : 0);
    return nearest;
}

static int EventQueueWorker(void* context)
{
    struct EventQueueEvent* event;
    EventQueue_t*           eventQueue = (EventQueue_t*)context;
    struct timespec         timePoint;
    struct timespec         interruptedAt;
    struct timespec         timeSpent;

    SetCurrentThreadName("event-pump");

    mtx_lock(&eventQueue->EventLock);
    while (eventQueue->IsRunning) {
        event = __GetNearestDeadline(eventQueue);
        if (event) {
            timespec_get(&timePoint, TIME_UTC);

            timePoint.tv_nsec += (long)event->Timeout * NSEC_PER_MSEC;
            if (timePoint.tv_nsec > NSEC_PER_SEC) {
                timePoint.tv_nsec -= NSEC_PER_SEC;
                timePoint.tv_sec++;
            }

            TRACE("EventQueueWorker waiting");
            if (cnd_timedwait(&eventQueue->EventCondition, &eventQueue->EventLock, &timePoint) == thrd_timedout) {
                if (!eventQueue->IsRunning) {
                    break;
                }
                TRACE("EventQueueWorker timed out, invoking callback");

                // We timedout, or in other words successfully waited
                if (event->State != EVENT_CANCELLED) {
                    event->State = EVENT_EXECUTED;
                    
                    mtx_unlock(&eventQueue->EventLock);
                    event->Function(event->Context);
                    mtx_lock(&eventQueue->EventLock);
                    if (event->Interval != 0) {
                        event->Timeout = event->Interval;
                    }
                }

                if (event->State == EVENT_CANCELLED || !event->Interval) {
                    list_remove(&eventQueue->Events, &event->Header);
                    __CleanupEvent(&event->Header, NULL);
                }
            }
            else {
                if (event->State != EVENT_CANCELLED) {
                    size_t ms;
                    // We were interrupted due to added events, calculate sleep time and subtract. Then
                    // start over
                    timespec_get(&interruptedAt, TIME_UTC);
                    timespec_diff(&interruptedAt, &timePoint, &timeSpent);

                    ms = timeSpent.tv_sec * MSEC_PER_SEC;
                    ms += timeSpent.tv_nsec / NSEC_PER_MSEC;
                    TRACE("EventQueueWorker interrupted, deducting %" PRIuIN "ms", ms);

                    event->Timeout -= ms;
                }
                else {
                    list_remove(&eventQueue->Events, &event->Header);
                    __CleanupEvent(&event->Header, NULL);
                }
                continue;
            }
        }
        else {
            // Wait for event to be added
            cnd_wait(&eventQueue->EventCondition, &eventQueue->EventLock);
        }
    }
    mtx_unlock(&eventQueue->EventLock);
    return 0;
}
