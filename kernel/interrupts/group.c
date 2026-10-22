/**
 * Copyright 2026, Philip Meulengracht
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
 * Reserve, bind, and release interrupt vectors as a group.
 */

#include <interrupts_group.h>
#include <interrupts.h>
#include <heap.h>
#include <memoryspace.h>
#include <spinlock.h>
#include <string.h>

#define INTERRUPT_GROUP_LIMIT 2048U
#define INTERRUPT_GROUP_TAG 1023U

_Static_assert(MAX_SUPPORTED_INTERRUPTS <= INTERRUPT_GROUP_TAG,
    "Group handles must not name interrupt table entries");

/** @brief One reservation and its immutable device message. */
struct __InterruptGroupVector {
    uuid_t             Id;
    InterruptMessage_t Message;
};

/** @brief Group lifecycle state and reservations owned by the interrupt core. */
struct __InterruptGroup {
    struct __InterruptGroup*     Next;
    uuid_t                       Handle;
    uuid_t                       Owner;
    uint32_t                     Strategy;
    uint32_t                     Count;
    int                          KernelHandlers;
    int                          Updating;
    int                          Unbinding;
    int                          EverEnabled;
    _Atomic(int)                 Enabled;
    struct __InterruptGroupVector Vectors[];
};

static Spinlock_t               g_groupLock = OS_SPINLOCK_INIT;
static struct __InterruptGroup* g_groups;
static uint32_t                 g_groupGeneration;

static struct __InterruptGroup*
__FindGroup(
    _In_ uuid_t handle)
{
    struct __InterruptGroup* group = g_groups;
    while (group != NULL && group->Handle != handle) {
        group = group->Next;
    }
    return group;
}

// Mark the group busy before dropping the registry lock. This keeps release
// from freeing it while an operation maps resources or waits for handlers.
static oserr_t
__BeginGroup(
    _In_  uuid_t                   handle,
    _Out_ struct __InterruptGroup** groupOut)
{
    struct __InterruptGroup* group;
    
    if (InterruptGetActiveStatus()) {
        return OS_EINVALPARAMS;
    }
    
    SpinlockAcquireIrq(&g_groupLock);
    group = __FindGroup(handle);
    if (group == NULL) {
        SpinlockReleaseIrq(&g_groupLock);
        return OS_ENOENT;
    }
    
    if (group->Updating) {
        SpinlockReleaseIrq(&g_groupLock);
        return OS_EBUSY;
    }
    group->Updating = 1;
    SpinlockReleaseIrq(&g_groupLock);
    
    *groupOut = group;
    return OS_EOK;
}

static void
__EndGroup(
    _In_ struct __InterruptGroup* group)
{
    SpinlockAcquireIrq(&g_groupLock);
    group->Updating = 0;
    SpinlockReleaseIrq(&g_groupLock);
}

static oserr_t
__ValidateGroupRequest(
    _In_ const InterruptGroupRequest_t* request,
    _In_ int                           kernelHandlers)
{
    if (InterruptGetActiveStatus()) {
        return OS_EINVALPARAMS;
    }

    if (kernelHandlers != 0 && kernelHandlers != 1) {
        return OS_EINVALPARAMS;
    }
    
    if (request->Minimum == 0 ||
        request->Minimum > request->Optimal ||
        request->Minimum > INTERRUPT_GROUP_LIMIT) {
        return OS_EINVALPARAMS;
    }
    
    if (request->Strategies == 0 ||
        (request->Strategies & ~INTERRUPT_STRATEGY_ANY) != 0) {
        return OS_EINVALPARAMS;
    }
    
    if ((request->Flags & ~INTERRUPT_EXCLUSIVE) != 0) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

// This is safe only before handlers or hardware have been attached.
static void
__DiscardReservedGroup(
    _In_ struct __InterruptGroup* group)
{
    uint32_t i;
    for (i = 0; i < group->Count; i++) {
        InterruptRelease(group->Vectors[i].Id, 0);
    }
    kfree(group);
}

oserr_t
InterruptGroupCheckOwner(
    _In_ uuid_t handle)
{
    struct __InterruptGroup* group;
    oserr_t                  status;

    SpinlockAcquireIrq(&g_groupLock);
    group = __FindGroup(handle);
    if (group == NULL) {
        status = OS_ENOENT;
    } else if (group->KernelHandlers || group->Owner != GetCurrentMemorySpaceHandle()) {
        status = OS_EPERMISSIONS;
    } else {
        status = OS_EOK;
    }
    SpinlockReleaseIrq(&g_groupLock);
    return status;
}

static oserr_t
__ReserveGroup(
    _In_  const InterruptGroupRequest_t* request,
    _In_  int                            kernelHandlers,
    _In_  uint32_t                       strategy,
    _Out_ struct __InterruptGroup**      groupOut)
{
    struct __InterruptGroup*       group;
    uint32_t                       capacity;
    uint32_t                       available;
    uint32_t                       ordinal;
    InterruptDescriptor_t          descriptor;
    struct __InterruptGroupVector* vector;
    SystemInterrupt_t*             entry;
    unsigned int                   flags = kernelHandlers ? INTERRUPT_KERNEL : 0;
    oserr_t                        status = OS_EBUSY;
    
    // Only MSI-X supports multiple vectors.
    capacity = strategy == INTERRUPT_STRATEGY_MSIX ? request->Optimal : 1;
    if (capacity > INTERRUPT_GROUP_LIMIT) {
        capacity = INTERRUPT_GROUP_LIMIT;
    }

    // MSI and INTx provide one vector. Skip them when the request needs more.
    if (strategy != INTERRUPT_STRATEGY_MSIX && request->Minimum != 1) {
        return OS_ENOTSUPPORTED;
    }

    if (strategy != INTERRUPT_STRATEGY_INTx) {
        status = InterruptGetMessageCapacity(request->ControllerID, kernelHandlers, &available);
        if (status != OS_EOK) {
            return status;
        }
        if (available == 0) {
            return OS_ENOTSUPPORTED;
        }

        // Use the vectors the backend has, as long as the request's minimum fits.
        if (capacity > available) {
            capacity = available;
        }
        
        if (capacity < request->Minimum) {
            return OS_EBUSY;
        }
        
        flags |= INTERRUPT_MSI | INTERRUPT_EXCLUSIVE;
    } else {
        if (request->Line.Line < 0) {
            return OS_ENOENT;
        }
        flags |= request->Flags;
    }

    group = kmalloc(sizeof(struct __InterruptGroup) + (capacity * sizeof(struct __InterruptGroupVector)));
    if (group == NULL) {
        return OS_EOOM;
    }
    memset(group, 0, sizeof(struct __InterruptGroup) + (capacity * sizeof(struct __InterruptGroupVector)));
    
    // Keep dispatch closed until the caller binds handlers and enables the group.
    atomic_init(&group->Enabled, 0);
    group->Owner = GetCurrentMemorySpaceHandle();
    group->KernelHandlers = kernelHandlers;
    group->Strategy = strategy;

    // A candidate can be taken after enumeration. If it is busy, try the next one.
    for (ordinal = 0; group->Count < capacity; ordinal++) {
        descriptor = (InterruptDescriptor_t) {0};
        vector = &group->Vectors[group->Count];
        
        if (strategy == INTERRUPT_STRATEGY_INTx) {
            descriptor.Line = request->Line.Line;
            descriptor.Pin = request->Line.Pin;
            descriptor.AcpiConform = request->Line.AcpiConform;
            status = InterruptReserve(&descriptor, flags, &vector->Id);
        } else {
            if (ordinal >= available) {
                break;
            }
            
            status = InterruptReserveMessage(
                request->ControllerID,
                ordinal,
                flags,
                &vector->Id,
                &vector->Message
            );
        }

        // InterruptReserve claims the vector atomically. Another group may get
        // there first, so a busy result sends us to the next candidate.
        if (status == OS_EBUSY && strategy != INTERRUPT_STRATEGY_INTx) {
            continue;
        }
        if (status != OS_EOK) {
            goto fail;
        }
        
        entry = InterruptGet(vector->Id);
        entry->GroupEnabled = &group->Enabled;
        InterruptPut(entry);
        
        // Cleanup uses Count, so update it only after a reservation succeeds.
        group->Count++;
    }
    if (group->Count < request->Minimum) {
        status = OS_EBUSY;
        goto fail;
    }

    *groupOut = group;
    return OS_EOK;

fail:
    // Nothing is bound or enabled yet. Unregister waits for active dispatch
    // readers before the group memory is freed.
    __DiscardReservedGroup(group);
    return status;
}

oserr_t
InterruptGroupCreate(
    _In_  const InterruptGroupRequest_t* request,
    _In_  int                            kernelHandlers,
    _Out_ InterruptGroupResult_t*        result)
{
    const uint32_t strategies[] = {
        INTERRUPT_STRATEGY_MSIX,
        INTERRUPT_STRATEGY_MSI,
        INTERRUPT_STRATEGY_INTx
    };
    struct __InterruptGroup* group = NULL;
    oserr_t                  status = OS_ENOTSUPPORTED;
    
    if (request == NULL || result == NULL) {
        return OS_EINVALPARAMS;
    }

    status = __ValidateGroupRequest(request, kernelHandlers);
    if (status != OS_EOK) {
        return status;
    }

    // Try the requested strategies in order. Keep going when one is busy or unsupported.
    for (unsigned int i = 0; i < sizeof(strategies) / sizeof(strategies[0]); i++) {
        if (!(request->Strategies & strategies[i])) {
            continue;
        }
        status = __ReserveGroup(request, kernelHandlers, strategies[i], &group);
        if (status == OS_EOK) {
            break;
        }
        if (status != OS_ENOTSUPPORTED && status != OS_EBUSY) {
            return status;
        }
    }
    if (group == NULL) {
        return status;
    }

    SpinlockAcquireIrq(&g_groupLock);
    // A handle ending in 0x3ff cannot identify a descriptor. Keep every group
    // generation unique, even if returning the handle to the caller later fails.
    if (g_groupGeneration == (UINT32_MAX >> 10U)) {
        SpinlockReleaseIrq(&g_groupLock);
        __DiscardReservedGroup(group);
        return OS_EOOM;
    }
    
    // Mark this as a group handle so it cannot be mistaken for a descriptor ID.
    group->Handle = (++g_groupGeneration << 10U) | INTERRUPT_GROUP_TAG;
    
    group->Next = g_groups;
    g_groups = group;
    SpinlockReleaseIrq(&g_groupLock);
    
    result->Handle = group->Handle;
    result->Count = group->Count;
    result->Strategy = group->Strategy;
    result->AddressLow = (uint32_t)group->Vectors[0].Message.Address;
    result->AddressHigh = (uint32_t)(group->Vectors[0].Message.Address >> 32);
    result->Data = group->Vectors[0].Message.Data;
    return OS_EOK;
}

oserr_t
InterruptGroupBind(
    _In_ uuid_t                    handle,
    _In_ uint32_t                  index,
    _In_ InterruptResourceTable_t* resources)
{
    struct __InterruptGroup* group;
    oserr_t                  status;

    status = __BeginGroup(handle, &group);
    if (status != OS_EOK) {
        return status;
    }

    if (index >= group->Count) {
        status = OS_EINVALPARAMS;
    } else if (atomic_load(&group->Enabled)) {
        status = OS_EBUSY;
    } else if (group->Unbinding) {
        status = OS_EBUSY;
    } else {
        status = InterruptBind(group->Vectors[index].Id, resources);
    }
    
    __EndGroup(group);
    return status;
}

oserr_t
InterruptGroupGetMessage(
    _In_  uuid_t              handle,
    _In_  uint32_t            index,
    _Out_ InterruptMessage_t* message)
{
    struct __InterruptGroup* group;
    oserr_t                  status;
    
    if (message == NULL) {
        return OS_EINVALPARAMS;
    }
    
    status = __BeginGroup(handle, &group);
    if (status != OS_EOK) {
        return status;
    }
    
    if (index >= group->Count) {
        status = OS_EINVALPARAMS;
    } else if (group->Strategy == INTERRUPT_STRATEGY_INTx) {
        status = OS_ENOTSUPPORTED;
    } else {
        if (group->Unbinding) {
            status = OS_EBUSY;
        } else if (group->Vectors[index].Id == UUID_INVALID) {
            status = OS_ENOENT;
        } else {
            *message = group->Vectors[index].Message;
        }
    }
    
    __EndGroup(group);
    return status;
}

static oserr_t
__DisableGroup(
    _In_ struct __InterruptGroup* group)
{
    oserr_t current;
    oserr_t status = OS_EOK;

    // Stop dispatch for the whole group before disabling individual vectors.
    atomic_store(&group->Enabled, 0);
    for (uint32_t i = 0; i < group->Count; i++) {
        if (group->Vectors[i].Id == UUID_INVALID) {
            continue;
        }
        
        current = InterruptSetEnabled(group->Vectors[i].Id, 0);
        if (current != OS_EOK) {
            // Try the remaining vectors too, then return the last error.
            status = current;
        }
    }
    return status;
}

static oserr_t
__ValidateGroupBindings(
    _In_ struct __InterruptGroup* group)
{
    SystemInterrupt_t* entry;

    // Check every handler first. A missing binding must not leave a partly
    // enabled group.
    for (uint32_t i = 0; i < group->Count; i++) {
        entry = InterruptGet(group->Vectors[i].Id);
        if (entry == NULL) {
            return OS_EINVALPARAMS;
        }
        
        if (!entry->Bound || entry->Unbinding) {
            InterruptPut(entry);
            return OS_EINVALPARAMS;
        }
        
        InterruptPut(entry);
    }
    return OS_EOK;
}

static oserr_t
__EnableGroup(
    _In_ struct __InterruptGroup* group)
{
    oserr_t status;

    if (atomic_load(&group->Enabled)) {
        return OS_EOK;
    }
    
    if (group->Unbinding) {
        return OS_EBUSY;
    }

    // All handlers must be ready before enabling the first vector.
    status = __ValidateGroupBindings(group);
    for (uint32_t i = 0; status == OS_EOK && i < group->Count; i++) {
        status = InterruptSetEnabled(group->Vectors[i].Id, 1);
    }

    if (status != OS_EOK) {
        // Some controller entries may already be enabled. Leave the shared
        // dispatch gate closed while disabling them again.
        (void)__DisableGroup(group);
        return status;
    }

    group->EverEnabled = 1;
    atomic_store(&group->Enabled, 1);
    return OS_EOK;
}

oserr_t
InterruptGroupSetEnabled(
    _In_ uuid_t handle,
    _In_ int    enable)
{
    struct __InterruptGroup* group;
    oserr_t                  status;

    if (enable != 0 && enable != 1) {
        return OS_EINVALPARAMS;
    }
    
    status = __BeginGroup(handle, &group);
    if (status != OS_EOK) {
        return status;
    }
    
    if (!enable) {
        status = __DisableGroup(group);
    } else {
        status = __EnableGroup(group);
    }
    
    __EndGroup(group);
    return status;
}

static int
__ShouldKeepGroupVector(
    _In_ const struct __InterruptGroup* group)
{
    // Keep used PCI message vectors reserved because a late device write may arrive.
    return !group->KernelHandlers &&
        group->EverEnabled &&
        group->Strategy != INTERRUPT_STRATEGY_INTx;
}

static oserr_t
__UnbindGroup(
    _In_ struct __InterruptGroup* group)
{
    oserr_t current;
    oserr_t status = OS_EOK;

    group->Unbinding = 1;
    
    // Go through all vectors and unbind them.
    for (uint32_t i = 0; i < group->Count; i++) {
        if (group->Vectors[i].Id == UUID_INVALID) {
            continue;
        }
        current = InterruptUnbind(group->Vectors[i].Id);
        if (current != OS_EOK) {
            // Keep going so we still try to remove the other mappings.
            status = current;
        }
    }
    
    // On failure, keep the group blocked from binding or enabling. The caller
    // can retry unbind or release to finish cleanup.
    if (status == OS_EOK) {
        group->Unbinding = 0;
    }
    return status;
}

oserr_t
InterruptGroupUnbind(
    _In_ uuid_t handle)
{
    struct __InterruptGroup* group;
    oserr_t                  status;

    status = __BeginGroup(handle, &group);
    if (status != OS_EOK) {
        return status;
    }

    if (atomic_load(&group->Enabled)) {
        status = OS_EBUSY;
    } else {
        status = __UnbindGroup(group);
    }
    
    __EndGroup(group);
    return status;
}

oserr_t
InterruptGroupRelease(
    _In_ uuid_t handle)
{
    struct __InterruptGroup*  group;
    struct __InterruptGroup** link;
    oserr_t                   status;

    status = __BeginGroup(handle, &group);
    if (status != OS_EOK) {
        return status;
    }
    
    status = __DisableGroup(group);
    if (status == OS_EOK) {
        status = __UnbindGroup(group);
    }
    if (status != OS_EOK) {
        __EndGroup(group);
        return status;
    }

    // The vectors are disabled and unmapped. Release their reservations now.
    group->Unbinding = 1;
    for (uint32_t i = 0; i < group->Count; i++) {
        if (group->Vectors[i].Id == UUID_INVALID) {
            continue;
        }
        
        status = InterruptRelease(
            group->Vectors[i].Id,
            __ShouldKeepGroupVector(group)
        );
        
        if (status != OS_EOK) {
            __EndGroup(group);
            return status;
        }
        group->Vectors[i].Id = UUID_INVALID;
    }

    SpinlockAcquireIrq(&g_groupLock);
    link = &g_groups;
    // Remove it only after every vector is safe to release or retain.
    while (*link != group) {
        link = &(*link)->Next;
    }
    *link = group->Next;
    SpinlockReleaseIrq(&g_groupLock);
    
    kfree(group);
    return OS_EOK;
}
