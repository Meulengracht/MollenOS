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
 */

//#define __TRACE

#include <assert.h>
#include <ddk/eventqueue.h>
#include <ddk/utils.h>
#include <ds/list.h>
#include <os/mutex.h>
#include <os/condition.h>
#include <os/threads.h>
#include <stdlib.h>
#include <time.h>

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
    int         IsRunning;
    uuid_t      NextEventId;
    uuid_t      EventThread;
    Mutex_t     EventLock;
    Condition_t EventCondition;
    list_t      Events;
} EventQueue_t;

static uuid_t __AddToEventQueue(EventQueue_t* eventQueue, EventQueueFunction function, void* context, size_t timeoutMs, size_t intervalMs);
static int    EventQueueWorker(void* context);

oserr_t CreateEventQueue(EventQueue_t** EventQueueOut)
{
    EventQueue_t*      eventQueue;
    ThreadParameters_t threadParameters;

    eventQueue = malloc(sizeof(EventQueue_t));
    if (!eventQueue) {
        return OS_EOOM;
    }

    eventQueue->IsRunning   = 1;
    eventQueue->NextEventId = 1;
    list_construct(&eventQueue->Events);
    MutexInitialize(&eventQueue->EventLock, MUTEX_PLAIN);
    ConditionInitialize(&eventQueue->EventCondition);

    ThreadParametersInitialize(&threadParameters);
    if (ThreadsCreate(&eventQueue->EventThread, &threadParameters, EventQueueWorker, eventQueue) != OS_EOK) {
        eventQueue->EventThread = UUID_INVALID;
        DestroyEventQueue(eventQueue);
        return OS_EOOM;
    }

    *EventQueueOut = eventQueue;
    return OS_EOK;
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
        ConditionSignal(&eventQueue->EventCondition);
        ThreadsJoin(eventQueue->EventThread, &unused);
        MutexDestroy(&eventQueue->EventLock);
        ConditionDestroy(&eventQueue->EventCondition);
    }
    list_clear(&eventQueue->Events, __CleanupEvent, NULL);
    free(eventQueue);
}

void QueueEvent(EventQueue_t* eventQueue, EventQueueFunction callback, void* context)
{
    __AddToEventQueue(eventQueue, callback, context, 0, 0);
}

uuid_t QueueDelayedEvent(EventQueue_t* eventQueue, EventQueueFunction callback, void* context, size_t delayMs)
{
    return __AddToEventQueue(eventQueue, callback, context, delayMs, 0);
}

uuid_t QueuePeriodicEvent(EventQueue_t* eventQueue, EventQueueFunction callback, void* context, size_t intervalMs)
{
    if (intervalMs == 0) {
        return UUID_INVALID;
    }
    return __AddToEventQueue(eventQueue, callback, context, intervalMs, intervalMs);
}

oserr_t CancelEvent(EventQueue_t* eventQueue, uuid_t eventHandle)
{
    element_t* element;
    oserr_t osStatus = OS_ENOENT;
    
    MutexLock(&eventQueue->EventLock);
    element = list_find(&eventQueue->Events, (void*)(uintptr_t)eventHandle);
    if (element) {
        struct EventQueueEvent* event = element->value;
        if (event->State != EVENT_EXECUTED) {
            event->State = EVENT_CANCELLED;
            osStatus = OS_EOK;
        }
    }
    MutexUnlock(&eventQueue->EventLock);
    return osStatus;
}

static uuid_t __AddToEventQueue(
        _In_ EventQueue_t*      eventQueue,
        _In_ EventQueueFunction function,
        _In_ void*              context,
        _In_ size_t             timeoutMs,
        _In_ size_t             intervalMs)
{
    struct EventQueueEvent* event;
    uuid_t                  eventId = UUID_INVALID;
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
    
    MutexLock(&eventQueue->EventLock);
    list_append(&eventQueue->Events, &event->Header);
    MutexUnlock(&eventQueue->EventLock);
    ConditionSignal(&eventQueue->EventCondition);

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

    ThreadsSetName("event-pump");

    MutexLock(&eventQueue->EventLock);
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
            if (ConditionTimedWait(&eventQueue->EventCondition, &eventQueue->EventLock, &timePoint, NULL) == OS_ETIMEOUT) {
                if (!eventQueue->IsRunning) {
                    break;
                }
                TRACE("EventQueueWorker timed out, invoking callback");

                // We timedout, or in other words successfully waited
                if (event->State != EVENT_CANCELLED) {
                    event->State = EVENT_EXECUTED;
                    
                    MutexUnlock(&eventQueue->EventLock);
                    event->Function(event->Context);
                    MutexLock(&eventQueue->EventLock);
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
            ConditionWait(&eventQueue->EventCondition, &eventQueue->EventLock, NULL);
        }
    }
    MutexUnlock(&eventQueue->EventLock);
    return 0;
}
