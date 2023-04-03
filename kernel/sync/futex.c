/**
 * Copyright 2022, Philip Meulengracht
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

#include <arch/interrupts.h>
#include <arch/thread.h>
#include <arch/utils.h>
#include <component/cpu.h>
#include <ds/list.h>
#include <debug.h>
#include <ddk/barrier.h>
#include <futex.h>
#include <heap.h>
#include <spinlock.h>
#include <memoryspace.h>
#include <scheduler.h>
#include <string.h>

#define FUTEX_HASHTABLE_CAPACITY 64

// One per memory context
typedef struct FutexItem {
    element_t    Header;
    list_t       BlockQueue;
    Spinlock_t   BlockQueueSyncObject;
    _Atomic(int) Waiters;
    
    struct MSContext* Context;
    uintptr_t         FutexAddress;
} FutexItem_t;

// One per futex key
typedef struct FutexBucket {
    Spinlock_t SyncObject;
    list_t     Futexes;
} FutexBucket_t;

static FutexBucket_t FutexBuckets[FUTEX_HASHTABLE_CAPACITY] = { 0 };

static size_t
GetIntegerHash(
        _In_ size_t x)
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
        _In_ uuid_t coreId)
{
    Thread_t* currentThread = CpuCoreCurrentThread(GetProcessorCore(coreId));
    if (!currentThread) {
        return NULL;
    }

    return ThreadSchedulerHandle(currentThread);
}

static FutexBucket_t*
__GetBucket(
        _In_ uintptr_t futexAddress)
{
    return &FutexBuckets[GetIntegerHash(futexAddress) & (FUTEX_HASHTABLE_CAPACITY - 1)];
}

// Must be called with the bucket lock held
static FutexItem_t*
FutexGetNode(
        _In_ FutexBucket_t*    bucket,
        _In_ uintptr_t         futexAddress,
        _In_ struct MSContext* context)
{
    foreach(i, &bucket->Futexes) {
        FutexItem_t* Item = (FutexItem_t*)i->value;
        if (Item->FutexAddress == futexAddress &&
            Item->Context == context) {
            return Item;
        }
    }
    return NULL;
}

static FutexItem_t*
FutexGetNodeLocked(
        _In_ FutexBucket_t*    bucket,
        _In_ uintptr_t         futexAddress,
        _In_ struct MSContext* context)
{
    FutexItem_t* item;

    SpinlockAcquireIrq(&bucket->SyncObject);
    item = FutexGetNode(bucket, futexAddress, context);
    SpinlockReleaseIrq(&bucket->SyncObject);

    return item;
}

// Must be called with the bucket lock held
static FutexItem_t*
FutexCreateNode(
        _In_ FutexBucket_t*    bucket,
        _In_ uintptr_t         futexAddress,
        _In_ struct MSContext* context)
{
    FutexItem_t* existing;
    FutexItem_t* item = (FutexItem_t*)kmalloc(sizeof(FutexItem_t));
    if (!item) {
        return NULL;
    }
    
    memset(item, 0, sizeof(FutexItem_t));
    ELEMENT_INIT(&item->Header, 0, item);
    list_construct(&item->BlockQueue);
    SpinlockConstruct(&item->BlockQueueSyncObject);
    item->FutexAddress = futexAddress;
    item->Context      = context;

    SpinlockAcquireIrq(&bucket->SyncObject);
    existing = FutexGetNode(bucket, futexAddress, context);
    if (!existing) {
        list_append(&bucket->Futexes, &item->Header);
    }
    SpinlockReleaseIrq(&bucket->SyncObject);
    
    if (existing) {
        kfree(item);
        item = existing;
    }
    return item;
}

static void
FutexPerformOperation(
    _In_ _Atomic(int)* futex,
    _In_ int           futexOperation)
{
    int operation = (futexOperation >> 28) & 0xF;
    int value     = (futexOperation >> 12) & 0xFFF;
    
    switch (operation) {
        case FUTEX_OP_SET: {
            atomic_store(futex, value);
        } break;
        case FUTEX_OP_ADD: {
            (void)atomic_fetch_add(futex, value);
        } break;
        case FUTEX_OP_OR: {
            (void)atomic_fetch_or(futex, value);
        } break;
        case FUTEX_OP_ANDN: {
            (void)atomic_fetch_and(futex, ~value);
        } break;
        case FUTEX_OP_XOR: {
            (void)atomic_fetch_xor(futex, value);
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
    int Op  = (Operation >> 24) & 0xF;
    int Val = Operation & 0xFFF;
    
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
    for (int i = 0; i < FUTEX_HASHTABLE_CAPACITY; i++) {
        SpinlockConstruct(&FutexBuckets[i].SyncObject);
        list_construct(&FutexBuckets[i].Futexes);
    }
}

oserr_t
FutexWait(
        _In_ OSAsyncContext_t* asyncContext,
        _In_ _Atomic(int)*     futex,
        _In_ int               expectedValue,
        _In_ int               flags,
        _In_ _Atomic(int)*     futex2,
        _In_ int               count,
        _In_ int               operation,
        _In_ OSTimestamp_t*    deadline)
{
    struct MSContext* context = NULL;
    FutexBucket_t*    futexBucket;
    FutexItem_t*      futexItem;
    uintptr_t         futexAddress;
    irqstate_t        irqState;
    oserr_t           oserr;
    TRACE("FutexWait(async=%i, futex=0x%llx, deadline=%llu:%li)",
          asyncContext != NULL, futex,
          deadline != NULL ? deadline->Seconds : 0,
          deadline != NULL ? deadline->Nanoseconds : 0);
    
    if (!SchedulerGetCurrentObject(ArchGetProcessorCoreId())) {
        // This is called by the ACPICA implemention indirectly through the Semaphore
        // implementation, which occurs during boot up of cores before a scheduler is running.
        // In this case we want the semaphore to act like a spinlock, which it will if we just
        // return anything else than OsTimeout.
        return OS_ENOTSUPPORTED;
    }
    
    // Get the futex context, if the context is private
    // we can stick to the virtual address for sleeping
    // otherwise we need to look up the physical page
    if (flags & FUTEX_FLAG_PRIVATE) {
        context      = GetCurrentMemorySpace()->Context;
        futexAddress = (uintptr_t)futex;
    } else {
        if (GetMemorySpaceMapping(GetCurrentMemorySpace(), (uintptr_t)futex,
                1, &futexAddress) != OS_EOK) {
            return OS_ENOENT;
        }
    }

    futexBucket = __GetBucket(futexAddress);
    
    // Disable interrupts here to gain safe passage, as we don't want to be
    // interrupted in this 'atomic' action. However, when competing with other
    // cpus here, we must take care to flush any changes and reload any changes
    irqState = InterruptDisable();

    futexItem = FutexGetNodeLocked(futexBucket, futexAddress, context);
    if (!futexItem) {
        futexItem = FutexCreateNode(futexBucket, futexAddress, context);
        if (!futexItem) {
            return OS_EOOM;
        }
    }

    (void)atomic_fetch_add(&futexItem->Waiters, 1);
CheckValue:
    if (atomic_load(futex) != expectedValue) {
        (void)atomic_fetch_sub(&futexItem->Waiters, 1);
        InterruptRestoreState(irqState);
        return OS_EINTERRUPTED;
    }

    // Are we running in a supported async syscall context? Then we can fork
    // the thread that were supposed to wait.
    if (asyncContext != NULL) {
        oserr = ThreadFork(asyncContext);
        if (oserr != OS_EFORKED) {
            // If fork returned an actual error, then we are aborting the operation,
            // and we should remember to do clenaup
            if (oserr != OS_EOK) {
                (void)atomic_fetch_sub(&futexItem->Waiters, 1);
            }
            // In either case, we must restore irqs for the primary thread
            InterruptRestoreState(irqState);
            return OS_EFORKED;
        }

        // If fork returned OS_EFORKED then continue operation, but we *MUST* perform an
        // additional value check here, as we've done a thread switch at this point. Make sure
        // we don't end up in a fork loop, so NULL the asyncContext
        asyncContext = NULL;
        goto CheckValue;
    }

    oserr = SchedulerBlock(&futexItem->BlockQueue, deadline);
    if (oserr == OS_EOK) {
        if (flags & FUTEX_FLAG_OP) {
            FutexPerformOperation(futex2, operation);
            FutexWake(futex2, count, flags);
        }
        InterruptRestoreState(irqState);
        ArchThreadYield();
        oserr = SchedulerGetTimeoutReason();
    }

    (void)atomic_fetch_sub(&futexItem->Waiters, 1);
    return oserr;
}

oserr_t
FutexWake(
    _In_ _Atomic(int)* Futex,
    _In_ int           Count,
    _In_ int           Flags)
{
    struct MSContext* Context = NULL;
    FutexBucket_t*    Bucket;
    FutexItem_t*      FutexItem;
    oserr_t           Status = OS_ENOENT;
    uintptr_t         FutexAddress;
    int               WaiterCount;
    int               i;
    
    // Get the futex context, if the context is private
    // we can stick to the virtual address for sleeping
    // otherwise we need to look up the physical page
    if (Flags & FUTEX_FLAG_PRIVATE) {
        Context = GetCurrentMemorySpace()->Context;
        FutexAddress = (uintptr_t)Futex;
    }
    else {
        if (GetMemorySpaceMapping(GetCurrentMemorySpace(), (uintptr_t)Futex, 
                1, &FutexAddress) != OS_EOK) {
            return OS_ENOENT;
        }
    }
    
    Bucket = __GetBucket(FutexAddress);

    FutexItem = FutexGetNodeLocked(Bucket, FutexAddress, Context);
    if (!FutexItem) {
        return OS_ENOENT;
    }
    
    WaiterCount = atomic_load(&FutexItem->Waiters);
    
WakeWaiters:
    for (i = 0; i < Count; i++) {
        element_t* Front;

        SpinlockAcquireIrq(&FutexItem->BlockQueueSyncObject);
        Front = list_front(&FutexItem->BlockQueue);
        if (Front) {
            // This is only neccessary while the list itself is thread-safe
            // otherwise we need a new list structure that can be shared including
            // a lock.
            if (list_remove(&FutexItem->BlockQueue, Front)) {
                Front = NULL;
            }
        }
        SpinlockReleaseIrq(&FutexItem->BlockQueueSyncObject);
        
        if (Front) {
            Status = SchedulerQueueObject(Front->value);
            if (Status != OS_EOK) {
                break;
            }
        }
        else {
            break;
        }
    }
    
    // Handle possible race-condition between wait/wake
    if (!WaiterCount && atomic_load(&FutexItem->Waiters) != 0) {
        WaiterCount = 1; // Only do this once!
        goto WakeWaiters;
    }
    return Status;
}

oserr_t
FutexWakeOperation(
    _In_ _Atomic(int)* Futex,
    _In_ int           Count,
    _In_ _Atomic(int)* Futex2,
    _In_ int           Count2,
    _In_ int           Operation,
    _In_ int           Flags)
{
    oserr_t oserr;
    int     initialValue;

    initialValue = atomic_load(Futex);
    FutexPerformOperation(Futex2, Operation);
    oserr = FutexWake(Futex, Count, Flags);
    if (FutexCompareOperation(initialValue, Operation)) {
        oserr = FutexWake(Futex2, Count2, Flags);
    }
    return oserr;
}
