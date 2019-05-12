/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Garbage Collector
 * - Makes it possible for regular cleanup in the kernel
 *   or regular maintiance.
 */
#define __MODULE "GCIF"
#define __TRACE

#include <ds/collection.h>
#include <garbagecollector.h>
#include <semaphore.h>
#include <threading.h>
#include <mutex.h>
#include <string.h>
#include <debug.h>
#include <heap.h>

typedef struct _GcEndpoint {
    CollectionItem_t Header;
    GcHandler_t      Handler;
} GcEndpoint_t;

typedef struct _GcMessage {
    CollectionItem_t Header;
    void*            Argument;
} GcMessage_t;

// Prototype for the worker thread
static void GcWorker(void *Args);

static Mutex_t         QueueLock      = MUTEX_INIT(MUTEX_PLAIN);
static Semaphore_t     EventLock      = SEMAPHORE_INIT(0, 1);
static Collection_t    GcHandlers     = COLLECTION_INIT(KeyId);
static Collection_t    GcEvents       = COLLECTION_INIT(KeyId);
static _Atomic(UUId_t) GcIdGenerator  = ATOMIC_VAR_INIT(0);
static UUId_t          GcThreadHandle = UUID_INVALID;

void
GcInitialize(void)
{
    CreateThread("gc-worker", GcWorker, NULL, 0, UUID_INVALID, &GcThreadHandle);
}

UUId_t
GcRegister(
    _In_ GcHandler_t Handler)
{
    GcEndpoint_t* Endpoint = (GcEndpoint_t*)kmalloc(sizeof(GcEndpoint_t));
    memset(Endpoint, 0, sizeof(GcEndpoint_t));
    
    Endpoint->Header.Key.Value.Id = atomic_fetch_add(&GcIdGenerator, 1);
    Endpoint->Handler             = Handler;
    CollectionAppend(&GcHandlers, &Endpoint->Header);
    return Endpoint->Header.Key.Value.Id;
}

OsStatus_t
GcUnregister(
    _In_ UUId_t Handler)
{
    DataKey_t         Key  = { .Value.Id = Handler };
    CollectionItem_t* Node = CollectionGetNodeByKey(&GcHandlers, Key, 0);
    if (Node == NULL) {
        return OsDoesNotExist;
    }
    return CollectionRemoveByKey(&GcHandlers, Key);
}

OsStatus_t
GcSignal(
    _In_ UUId_t Handler,
    _In_ void*  Data)
{
    GcMessage_t*      Message;
    DataKey_t         Key  = { .Value.Id = Handler };
    CollectionItem_t* Node = CollectionGetNodeByKey(&GcHandlers, Key, 0);
    if (Node == NULL) {
        return OsDoesNotExist;
    }

    Message = (GcMessage_t*)kmalloc(sizeof(GcMessage_t));
    memset(Message, 0, sizeof(GcMessage_t));
    Message->Header.Key.Value.Id = Handler;
    Message->Argument            = Data;
    
    CollectionAppend(&GcEvents, &Message->Header);
    SemaphoreSignal(&EventLock, 1);
    return OsSuccess;
}

static void 
GcWorker(
    _In_Opt_ void* Args)
{
    GcEndpoint_t* Endpoint;
    GcMessage_t*  Message;
    int           Run = 1;
    _CRT_UNUSED(Args);
    
    MutexLock(&QueueLock);
    while (Run) {
        SemaphoreWait(&EventLock, &QueueLock, 0);
        
        Message = (GcMessage_t*)CollectionPopFront(&GcEvents);
        if (Message == NULL) {
            continue;
        }

        // Sanitize the handler
        Endpoint = (GcEndpoint_t*)CollectionGetNodeByKey(&GcHandlers, Message->Header.Key, 0);
        if (Endpoint != NULL) {
            Endpoint->Handler(Message->Argument);
        }
        kfree(Message);
    }
    MutexUnlock(&QueueLock);
}
