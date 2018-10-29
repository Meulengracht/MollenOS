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
 * MollenOS Garbage Collector
 * - Makes it possible for regular cleanup in the kernel
 *   or regular maintiance.
 */
#define __MODULE "GCIF"
#define __TRACE

#include <ds/collection.h>
#include <garbagecollector.h>
#include <criticalsection.h>
#include <semaphore_slim.h>
#include <threading.h>
#include <debug.h>

// Prototype for the worker thread
void GcWorker(void *Args);

/* Globals 
 * Needed for state-keeping */
static SlimSemaphore_t GlbGcEventLock;
static Collection_t GcHandlers      = COLLECTION_INIT(KeyInteger);
static Collection_t GcEvents        = COLLECTION_INIT(KeyInteger);
static _Atomic(UUId_t) GcIdGenerator= ATOMIC_VAR_INIT(0);

/* GcConstruct
 * Constructs the gc data-systems, but does not start the actual collection */
void
GcConstruct(void)
{
    // Create data-structures
    SlimSemaphoreConstruct(&GlbGcEventLock, 0, 1000);
}

/* GcInitialize
 * Initializes the garbage-collector system */
void
GcInitialize(void)
{
    // Debug information
    TRACE("GcInitialize()");
    ThreadingCreateThread("gc-worker", GcWorker, NULL, 0);
}

/* GcRegister
 * Registers a new gc-handler that will be run
 * when new work is available, returns the unique id
 * for the new handler */
UUId_t
GcRegister(
    _In_ GcHandler_t Handler)
{
    DataKey_t Key;
    Key.Value = (int)atomic_fetch_add(&GcIdGenerator, 1);
    CollectionAppend(&GcHandlers, CollectionCreateNode(Key, (void*)Handler));
    return (UUId_t)Key.Value;
}

/* GcUnregister
 * Removes a previously registed handler by its id */
OsStatus_t
GcUnregister(
    _In_ UUId_t Handler)
{
    DataKey_t Key;
    Key.Value = (int)Handler;
    if (CollectionGetDataByKey(&GcHandlers, Key, 0) == NULL) {
        return OsError;
    }
    return CollectionRemoveByKey(&GcHandlers, Key);
}

/* GcSignal
 * Signals new garbage for the specified handler */
OsStatus_t
GcSignal(
    _In_ UUId_t Handler,
    _In_ void*  Data)
{
    DataKey_t Key;
    Key.Value = (int)Handler;

    // Sanitize the status of the gc
    if (CollectionGetDataByKey(&GcHandlers, Key, 0) == NULL) {
        return OsError;
    }
    CollectionAppend(&GcEvents, CollectionCreateNode(Key, Data));
    SlimSemaphoreSignal(&GlbGcEventLock, 1);
    return OsSuccess;
}

/* GcWorker
 * The event-handler thread */
void 
GcWorker(
    _In_Opt_ void* Args)
{
    CollectionItem_t*   eNode;
    GcHandler_t         Handler;
    int                 Run = 1;

    // Unused arg
    _CRT_UNUSED(Args);
    while (Run) {
        // Wait for next event
        SlimSemaphoreWait(&GlbGcEventLock, 0);

        eNode = CollectionPopFront(&GcEvents);
        if (eNode == NULL) {
            continue;
        }

        // Sanitize the handler
        if ((Handler = (GcHandler_t)CollectionGetDataByKey(&GcHandlers, eNode->Key, 0)) == NULL) {
            CollectionDestroyNode(&GcEvents, eNode);
            continue;
        }
        Handler(eNode->Data);
        CollectionDestroyNode(&GcEvents, eNode);
    }
}
