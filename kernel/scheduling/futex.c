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
 * Futex Synchronization
 *
 */

#include <ds/collection.h>
#include <scheduler.h>
#include <futex.h>

// One per memory context
typedef struct {
    CollectionItem_t            Header;
    SystemMemorySpaceContext_t* Context;
    uintptr_t                   FutexAddress;
    Collection_t                WaitQueue;
} FutexItem_t;

// One per futex key
typedef struct {
    SafeMemoryLock_t SyncObject;
    _Atomic(int)     Waiters;
    Collection_t     FutexQueue;
} FutexBucket_t;

#define FUTEX_HASHTABLE_CAPACITY 64
static FutexBucket_t* FutexBuckets = NULL;

static size_t
GetIntegerHash(size_t x)
{
#if __BITS == 32
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
#elif __BITS == 64
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    x = x ^ (x >> 31);
#endif
    return x;
}

static SchedulerObject_t*
SchedulerGetCurrentObject(
    _In_ UUId_t CoreId)
{
    return (GetProcessorCore(CoreId)->CurrentThread != NULL) ? 
        GetProcessorCore(CoreId)->CurrentThread->SchedulerObject : NULL;
}

static FutexBucket_t*
FutexGetBucket(
    _In_ _Atomic(int)* Futex)
{
    size_t FutexHash = GetIntegerHash((size_t)Futex);
    return &FutexBuckets[FutexHash & (FUTEX_HASHTABLE_CAPACITY - 1)];
}

// Must be called with the bucket lock held
static FutexItem_t*
FutexGetNode(
    _In_ FutexBucket_t*              Bucket,
    _In_ uintptr_t                   FutexAddress,
    _In_ SystemMemorySpaceContext_t* Context)
{
    foreach(Node, &Bucket->FutexQueue) {
        FutexItem_t* Item = (FutexItem_t*)Node;
        if (FutexAddress == Item->FutexAddress &&
            Item->Context == Context) {
            // found
            return Item;
        }
    }
    return NULL;
}

// Must be called with the bucket lock held
static FutexItem_t*
FutexCreateNode(
    _In_ FutexBucket_t*              Bucket,
    _In_ uintptr_t                   FutexAddress,
    _In_ SystemMemorySpaceContext_t* Context)
{
    FutexItem_t* Item = (FutexItem_t*)kmalloc(sizeof(FutexItem_t));
    memset(Item, 0, sizeof(FutexItem_t));
    CollectionConstruct(&Item->WaitQueue, KeyId);
    
    Item->FutexAddress = FutexAddress;
    Item->Context      = Context;
    CollectionAppend(&Bucket->FutexQueue, &Item->Header);
    return Item;
}

static void
FutexPerformOperation(
    _In_ _Atomic(int)* Futex,
    _In_ int           Operation)
{
    // parse operation
    int Op  = (Operation >> 28) & 0xF;
    int Val = (Operation >> 12) & 0xFFF;
    
    switch (Op) {
        case FUTEX_OP_SET: {
            atomic_store(Futex, Val);
        } break;
        case FUTEX_OP_ADD: {
            atomic_fetch_add(Futex, Val);
        } break;
        case FUTEX_OP_OR: {
            atomic_fetch_or(Futex, Val);
        } break;
        case FUTEX_OP_ANDN: {
            atomic_fetch_and(Futex, ~Val);
        } break;
        case FUTEX_OP_XOR: {
            atomic_fetch_xor(Futex, Val);
        } break;
        
        default:
            break;
    }
}

static int
FutexCompareOperation(
    _In_ int InitialValue,
    _In_ int Operation)
{
    // parse operation
    int Op  = (Operation >> 24) & 0xF;
    int Val = Operation & 0xFFF
    
    switch (Op) {
        case FUTEX_OP_CMP_EQ: {
            if (InitialValue == Val) {
                return 1;
            }
        } break;
        case FUTEX_OP_CMP_NE: {
            if (InitialValue != Val) {
                return 1;
            }
        } break;
        case FUTEX_OP_CMP_LT: {
            if (InitialValue < Val) {
                return 1;
            }
        } break;
        case FUTEX_OP_CMP_LE: {
            if (InitialValue <= Val) {
                return 1;
            }
        } break;
        case FUTEX_OP_CMP_GT: {
            if (InitialValue > Val) {
                return 1;
            }
        } break;
        case FUTEX_OP_CMP_GE: {
            if (InitialValue >= Val) {
                return 1;
            }
        } break;
        
        default:
            break;
    }
    return 0;
}

void
FutexInitialize(void)
{
    FutexBuckets = (FutexBucket_t*)kmalloc(sizeof(FutexBucket_t) * FUTEX_HASHTABLE_CAPACITY);
    memset(FutexBuckets, 0, sizeof(FutexBucket_t) * FUTEX_HASHTABLE_CAPACITY);
}

OsStatus_t
FutexWait(
    _In_ _Atomic(int)* Futex,
    _In_ int           ExpectedValue,
    _In_ int           Flags,
    _In_ size_t        Timeout)
{
    SystemMemorySpaceContext_t* Context = NULL;
    SchedulerObject_t* Object     = SchedulerGetCurrentObject(ArchGetProcessorCoreId());
    FutexBucket_t*     FutexQueue = FutexGetBucket(Futex);
    FutexItem_t*       FutexItem;
    
    // Get the futex queue
    if (!(Flags & FUTEX_WAIT_PRIVATE)) {
        Context = GetCurrentMemorySpace()->Context;
    }

    // Setup information for the current running object
    Object->TimeLeft      = Timeout;
    Object->Timeout       = 0;
    Object->InterruptedAt = 0;
        
    dslock(&FutexQueue->SyncObject);
    FutexItem = FutexGetNode(FutexQueue, (uintptr_t)Futex, Context);
    if (!FutexItem) {
        FutexItem = FutexCreateNode(FutexQueue, (uintptr_t)Futex, Context);
    }
    Object->WaitQueueHandle = &(FutexItem->WaitQueue);
    
    CollectionAppend(&FutexItem->WaitQueue, &Object->Header);
    if (!atomic_load(Futex) != ExpectedValue) {
        (void)CollectionRemoveByNode(&FutexItem->WaitQueue, &Object->Header);
        dsunlock(&FutexQueue->SyncObject);
        return OsError;
    }
    Object->State = SchedulerObjectStateBlocked;
    dsunlock(&FutexQueue->SyncObject);
    ThreadingYield();
    return (Object->Timeout == 1) ? OsTimeout : OsSuccess;
}

OsStatus_t
FutexWaitOperation(
    _In_ _Atomic(int)* Futex,
    _In_ int           ExpectedValue,
    _In_ _Atomic(int)* Futex2,
    _In_ int           Count2,
    _In_ int           Operation,
    _In_ int           Flags,
    _In_ size_t        Timeout)
{
    SystemMemorySpaceContext_t* Context = NULL;
    SchedulerObject_t* Object     = SchedulerGetCurrentObject(ArchGetProcessorCoreId());
    FutexBucket_t*     FutexQueue = FutexGetBucket(Futex);
    FutexItem_t*       FutexItem;
    
    // Get the futex queue
    if (!(Flags & FUTEX_WAIT_PRIVATE)) {
        Context = GetCurrentMemorySpace()->Context;
    }
    
    // Setup information for the current running object
    Object->TimeLeft      = Timeout;
    Object->Timeout       = 0;
    Object->InterruptedAt = 0;
    
    dslock(&FutexQueue->SyncObject);
    FutexItem = FutexGetNode(FutexQueue, (uintptr_t)Futex, Context);
    if (!FutexItem) {
        FutexItem = FutexCreateNode(FutexQueue, (uintptr_t)Futex, Context);
    }
    Object->WaitQueueHandle = &(FutexItem->WaitQueue);
    
    CollectionAppend(&FutexItem->WaitQueue, &Object->Header);
    if (!atomic_load(Futex) != ExpectedValue) {
        (void)CollectionRemoveByNode(&FutexItem->WaitQueue, &Object->Header);
        dsunlock(&FutexQueue->SyncObject);
        return OsError;
    }
    Object->State = SchedulerObjectStateBlocked;
    FutexPerformOperation(Futex2, Operation);
    FutexWake(Futex2, Count2, Flags);
    dsunlock(&FutexQueue->SyncObject);
    ThreadingYield();
    return (Object->Timeout == 1) ? OsTimeout : OsSuccess;
}

OsStatus_t
FutexWake(
    _In_ _Atomic(int)* Futex,
    _In_ int           Count,
    _In_ int           Flags)
{
    SystemMemorySpaceContext_t* Context = NULL;
    FutexBucket_t*     FutexQueue = FutexGetBucket(Futex);
    FutexItem_t*       FutexItem;
    OsStatus_t         Status;
    int                i;
    
    // Get the futex queue
    if (!(Flags & FUTEX_WAIT_PRIVATE)) {
        Context = GetCurrentMemorySpace()->Context;
    }
    
    dslock(&FutexQueue->SyncObject);
    FutexItem = FutexGetNode(FutexQueue, (uintptr_t)Futex, Context);
    dsunlock(&FutexQueue->SyncObject);
    
    if (!FutexItem) {
        return OsDoesNotExist;
    }
    
    for (i = 0; i < Count; i++) {
        Status = SchedulerUnblock(&FutexQueue->WaitQueue);
        if (Status != OsSuccess) {
            break;
        }
    }
    return Status;
}

OsStatus_t
FutexWakeOperation(
    _In_ _Atomic(int)* Futex,
    _In_ int           Count,
    _In_ _Atomic(int)* Futex2,
    _In_ int           Count2,
    _In_ int           Operation,
    _In_ int           Flags)
{
    OsStatus_t Status;
    int        InitialValue;
    
    InitialValue = atomic_load(Futex);
    FutexPerformOperation(Futex2, Operation);
    Status = FutexWake(Futex, Count, Flags);
    if (FutexCompareOperation(InitialValue, Operation)) {
        Status = FutexWake(Futex2, Count2, Flags);
    }
    return Status;
}
