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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 * Futex Synchronization
 *
 */
#define __MODULE "FUTX"
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
#include <os/spinlock.h>
#include <memoryspace.h>
#include <scheduler.h>
#include <string.h>

#define FUTEX_HASHTABLE_CAPACITY 64

// One per memory context
typedef struct FutexItem {
    element_t    Header;
    list_t       BlockQueue;
    spinlock_t   BlockQueueSyncObject;
    _Atomic(int) Waiters;
    
    MemorySpaceContext_t * Context;
    uintptr_t                   FutexAddress;
} FutexItem_t;

// One per futex key
typedef struct FutexBucket {
    spinlock_t   SyncObject;
    list_t       Futexes;
} FutexBucket_t;

static FutexBucket_t FutexBuckets[FUTEX_HASHTABLE_CAPACITY] = { 0 };

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
    Thread_t* currentThread = CpuCoreCurrentThread(GetProcessorCore(CoreId));
    if (!currentThread) {
        return NULL;
    }

    return ThreadSchedulerHandle(currentThread);
}

static FutexBucket_t*
FutexGetBucket(
    _In_ uintptr_t FutexAddress)
{
    size_t FutexHash = GetIntegerHash(FutexAddress);
    return &FutexBuckets[FutexHash & (FUTEX_HASHTABLE_CAPACITY - 1)];
}

// Must be called with the bucket lock held
static FutexItem_t*
FutexGetNode(
        _In_ FutexBucket_t*              Bucket,
        _In_ uintptr_t                   FutexAddress,
        _In_ MemorySpaceContext_t* Context)
{
    foreach(i, &Bucket->Futexes) {
        FutexItem_t* Item = (FutexItem_t*)i->value;
        if (Item->FutexAddress == FutexAddress &&
            Item->Context      == Context) {
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
        _In_ MemorySpaceContext_t* Context)
{
    FutexItem_t* Existing;
    FutexItem_t* Item = (FutexItem_t*)kmalloc(sizeof(FutexItem_t));
    if (!Item) {
        return NULL;
    }
    
    memset(Item, 0, sizeof(FutexItem_t));
    ELEMENT_INIT(&Item->Header, 0, Item);
    list_construct(&Item->BlockQueue);
    spinlock_init(&Item->BlockQueueSyncObject, spinlock_plain);
    Item->FutexAddress = FutexAddress;
    Item->Context      = Context;
    
    spinlock_acquire(&Bucket->SyncObject);
    Existing = FutexGetNode(Bucket, FutexAddress, Context);
    if (!Existing) {
        list_append(&Bucket->Futexes, &Item->Header);
    }
    spinlock_release(&Bucket->SyncObject);
    
    if (Existing) {
        kfree(Item);
        Item = Existing;
    }
    return Item;
}

static void
FutexPerformOperation(
    _In_ _Atomic(int)* Futex,
    _In_ int           Operation)
{
    int Op  = (Operation >> 28) & 0xF;
    int Val = (Operation >> 12) & 0xFFF;
    int Old = 0;
    
    switch (Op) {
        case FUTEX_OP_SET: {
            atomic_store(Futex, Val);
        } break;
        case FUTEX_OP_ADD: {
            Old = atomic_fetch_add(Futex, Val);
        } break;
        case FUTEX_OP_OR: {
            Old = atomic_fetch_or(Futex, Val);
        } break;
        case FUTEX_OP_ANDN: {
            Old = atomic_fetch_and(Futex, ~Val);
        } break;
        case FUTEX_OP_XOR: {
            Old = atomic_fetch_xor(Futex, Val);
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
    int i;
    for (i = 0; i < FUTEX_HASHTABLE_CAPACITY; i++) {
        spinlock_init(&FutexBuckets[i].SyncObject, spinlock_plain);
        list_construct(&FutexBuckets[i].Futexes);
    }
    smp_wmb();
}

OsStatus_t
FutexWait(
    _In_ _Atomic(int)* Futex,
    _In_ int           ExpectedValue,
    _In_ int           Flags,
    _In_ size_t        Timeout)
{
    MemorySpaceContext_t * Context = NULL;
    FutexBucket_t        *              Bucket;
    FutexItem_t*                FutexItem;
    uintptr_t                   FutexAddress;
    IntStatus_t                 CpuState;
    TRACE("%u: FutexWait(f 0x%llx, t %u)", ThreadCurrentHandle(), Futex, Timeout);
    
    if (!SchedulerGetCurrentObject(ArchGetProcessorCoreId())) {
        // This is called by the ACPICA implemention indirectly through the Semaphore
        // implementation, which occurs during boot up of cores before a scheduler is running.
        // In this case we want the semaphore to act like a spinlock, which it will if we just
        // return anything else than OsTimeout.
        return OsNotSupported;
    }
    
    // Get the futex context, if the context is private
    // we can stick to the virtual address for sleeping
    // otherwise we need to lookup the physical page
    if (Flags & FUTEX_WAIT_PRIVATE) {
        Context = GetCurrentMemorySpace()->Context;
        FutexAddress = (uintptr_t)Futex;
    }
    else {
        if (GetMemorySpaceMapping(GetCurrentMemorySpace(), (uintptr_t)Futex, 
                1, &FutexAddress) != OsSuccess) {
            return OsDoesNotExist;
        }
    }

    Bucket = FutexGetBucket(FutexAddress);
    
    // Disable interrupts here to gain safe passage, as we don't want to be
    // interrupted in this 'atomic' action. However when competing with other
    // cpus here, we must take care to flush any changes and reload any changes
    CpuState = InterruptDisable();
    
    spinlock_acquire(&Bucket->SyncObject);
    FutexItem = FutexGetNode(Bucket, FutexAddress, Context);
    spinlock_release(&Bucket->SyncObject);
    
    if (!FutexItem) {
        FutexItem = FutexCreateNode(Bucket, FutexAddress, Context);
        if (!FutexItem) {
            return OsOutOfMemory;
        }
    }
    
    (void)atomic_fetch_add(&FutexItem->Waiters, 1);
    if (atomic_load(Futex) != ExpectedValue) {
        (void)atomic_fetch_sub(&FutexItem->Waiters, 1);
        InterruptRestoreState(CpuState);
        return OsInterrupted;
    }
    
    SchedulerBlock(&FutexItem->BlockQueue, Timeout);
    InterruptRestoreState(CpuState);
    ThreadingYield();

    (void)atomic_fetch_sub(&FutexItem->Waiters, 1);
    TRACE("%u: woke up", ThreadCurrentHandle());
    return SchedulerGetTimeoutReason();
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
    MemorySpaceContext_t * Context = NULL;
    FutexBucket_t        *              Bucket;
    FutexItem_t*                FutexItem;
    uintptr_t                   FutexAddress;
    IntStatus_t                 CpuState;
    TRACE("%u: FutexWaitOperation(f 0x%llx, t %u)", ThreadCurrentHandle(), Futex, Timeout);
    
    if (!SchedulerGetCurrentObject(ArchGetProcessorCoreId())) {
        // This is called by the ACPICA implemention indirectly through the Semaphore
        // implementation, which occurs during boot up of cores before a scheduler is running.
        // In this case we want the semaphore to act like a spinlock, which it will if we just
        // return anything else than OsTimeout.
        return OsNotSupported;
    }
    
    // Get the futex context, if the context is private
    // we can stick to the virtual address for sleeping
    // otherwise we need to lookup the physical page
    if (Flags & FUTEX_WAIT_PRIVATE) {
        Context = GetCurrentMemorySpace()->Context;
        FutexAddress = (uintptr_t)Futex;
    }
    else {
        if (GetMemorySpaceMapping(GetCurrentMemorySpace(), (uintptr_t)Futex, 
                1, &FutexAddress) != OsSuccess) {
            return OsDoesNotExist;
        }
    }
    
    Bucket = FutexGetBucket(FutexAddress);
    
    // Disable interrupts here to gain safe passage, as we don't want to be
    // interrupted in this 'atomic' action. However when competing with other
    // cpus here, we must take care to flush any changes and reload any changes
    CpuState = InterruptDisable();
    
    spinlock_acquire(&Bucket->SyncObject);
    FutexItem = FutexGetNode(Bucket, FutexAddress, Context);
    spinlock_release(&Bucket->SyncObject);
    
    if (!FutexItem) {
        FutexItem = FutexCreateNode(Bucket, FutexAddress, Context);
        if (!FutexItem) {
            return OsOutOfMemory;
        }
    }
    
    (void)atomic_fetch_add(&FutexItem->Waiters, 1);
    if (atomic_load(Futex) != ExpectedValue) {
        (void)atomic_fetch_sub(&FutexItem->Waiters, 1);
        InterruptRestoreState(CpuState);
        return OsInterrupted;
    }
    
    SchedulerBlock(&FutexItem->BlockQueue, Timeout);
    FutexPerformOperation(Futex2, Operation);
    FutexWake(Futex2, Count2, Flags);
    InterruptRestoreState(CpuState);
    ThreadingYield();
    
    (void)atomic_fetch_sub(&FutexItem->Waiters, 1);
    TRACE("%u: woke up", ThreadCurrentHandle());
    return SchedulerGetTimeoutReason();
}

OsStatus_t
FutexWake(
    _In_ _Atomic(int)* Futex,
    _In_ int           Count,
    _In_ int           Flags)
{
    MemorySpaceContext_t * Context = NULL;
    FutexBucket_t        *     Bucket;
    FutexItem_t*       FutexItem;
    OsStatus_t         Status = OsDoesNotExist;
    uintptr_t          FutexAddress;
    int                WaiterCount;
    int                i;
    
    // Get the futex context, if the context is private
    // we can stick to the virtual address for sleeping
    // otherwise we need to lookup the physical page
    if (Flags & FUTEX_WAKE_PRIVATE) {
        Context = GetCurrentMemorySpace()->Context;
        FutexAddress = (uintptr_t)Futex;
    }
    else {
        if (GetMemorySpaceMapping(GetCurrentMemorySpace(), (uintptr_t)Futex, 
                1, &FutexAddress) != OsSuccess) {
            return OsDoesNotExist;
        }
    }
    
    Bucket = FutexGetBucket(FutexAddress);
    
    spinlock_acquire(&Bucket->SyncObject);
    FutexItem = FutexGetNode(Bucket, FutexAddress, Context);
    spinlock_release(&Bucket->SyncObject);
    if (!FutexItem) {
        return OsDoesNotExist;
    }
    
    WaiterCount = atomic_load(&FutexItem->Waiters);
    
WakeWaiters:
    for (i = 0; i < Count; i++) {
        element_t* Front;
        
        spinlock_acquire(&FutexItem->BlockQueueSyncObject);
        Front = list_front(&FutexItem->BlockQueue);
        if (Front) {
            // This is only neccessary while the list itself is thread-safe
            // otherwise we need a new list structure that can be shared including
            // a lock.
            if (list_remove(&FutexItem->BlockQueue, Front)) {
                Front = NULL;
            }
        }
        spinlock_release(&FutexItem->BlockQueueSyncObject);
        
        if (Front) {
            Status = SchedulerQueueObject(Front->value);
            if (Status != OsSuccess) {
                break;
            }
        }
    }
    
    // Handle possible race-condition between wait/wake
    if (!WaiterCount && atomic_load(&FutexItem->Waiters) != 0) {
        WaiterCount = 1; // Only do this once!
        goto WakeWaiters;
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
    TRACE("%u: FutexWakeOperation(f 0x%llx)", ThreadCurrentHandle(), Futex);
    
    InitialValue = atomic_load(Futex);
    FutexPerformOperation(Futex2, Operation);
    Status = FutexWake(Futex, Count, Flags);
    if (FutexCompareOperation(InitialValue, Operation)) {
        Status = FutexWake(Futex2, Count2, Flags);
    }
    return Status;
}
