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
#include <interrupts_chain.h>
#include <ddk/types/interrupt-provider.h>
#include <heap.h>
#include <handle.h>
#include <memoryspace.h>
#include <spinlock.h>
#include <string.h>

#define INTERRUPT_GROUP_LIMIT INTERRUPT_PROVIDER_MAX_VECTORS
_Static_assert(MAX_SUPPORTED_INTERRUPTS <= UINT16_MAX,
    "Interrupt table indices must fit in descriptor IDs");

/**
 * One reserved interrupt and the fixed address/data the device writes to signal it.
 * CompletionHandle identifies an attachment when a provider retries a request.
 * Completion holds the group's use of that callback, avoiding a lookup per interrupt.
 */
struct __InterruptGroupVector {
    uuid_t                      Id;
    InterruptMessage_t          Message;
    uuid_t                      CompletionHandle;
    struct InterruptCompletion* Completion;
};

/**
 * Records which bus provider attached this device to the group. Closed prevents
 * a delayed attachment request from reopening a group that is being removed.
 */
struct __InterruptProviderLease {
    uuid_t Provider;
    uuid_t Device;
    int    Closed;
};

/**
 * Coordinates one process's interrupt reservations and optional provider callbacks.
 * Updating permits only one group operation at a time, even while the lock is
 * released. Unbinding remembers incomplete cleanup. Enabled is shared by all
 * vectors so handlers start only after the entire group is ready. EverEnabled
 * records whether late device messages might still target these reservations.
 */
struct __InterruptGroup {
    struct __InterruptGroup*        Next;
    uuid_t                          Handle;
    uuid_t                          Owner;
    uuid_t                          ControllerID;
    struct __InterruptProviderLease Provider;
    uint32_t                        Strategy;
    uint32_t                        Count;
    int                             KernelHandlers;
    int                             Updating;
    int                             Unbinding;
    int                             EverEnabled;
    _Atomic(int)                    Enabled;
    struct __InterruptGroupVector   Vectors[];
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
    
    // Mapping and waiting for handlers can take time. Reserve this operation's
    // exclusive access before unlocking, so other threads cannot alter or free
    // the group while we work without holding the list lock.
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
    } else if (group->KernelHandlers || group->Owner != InterruptCurrentOwner()) {
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
    group->Owner = InterruptCurrentOwner();
    group->KernelHandlers = kernelHandlers;
    group->Strategy = strategy;
    group->ControllerID = request->ControllerID;

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
    SystemInterrupt_t*       entry;
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
    } else if (group->Provider.Closed) {
        status = OS_EPERMISSIONS;
    } else {
        status = InterruptBind(group->Vectors[index].Id, resources);
        if (status == OS_EOK) {
            entry = InterruptGet(group->Vectors[index].Id);

            // The driver supplies only its own handler. Add the provider's
            // callback from the group so drivers need no completion API.
            // The group keeps the callback in use; this entry adds no reference.
            // Set the pointer while disabled, before any handler can run.
            entry->Completion = group->Vectors[index].Completion;
            InterruptPut(entry);
        }
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

    // Stop new handler calls for the whole group before disabling individual
    // vectors. Otherwise a later vector could still run during partial shutdown.
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
        // Some controller entries may already be enabled. The shared Enabled
        // flag is still clear, so no handler runs while we disable them again.
        (void)__DisableGroup(group);
        return status;
    }

    // Allow handler calls only after all vectors succeeded. Remember this group
    // was usable because a device message may arrive even after it is disabled.
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
    // PCI message interrupts are memory writes that can arrive after device stop.
    // Reusing a previously enabled slot could send that late write to a different
    // driver's handler. Keep the slot unavailable until reboot instead.
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
    
    // Try every vector even if an earlier one fails. Successful removals reduce
    // the work left for a retry; Unbinding prevents use of the incomplete group.
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

static oserr_t
__ReleaseGroup(
    _In_ struct __InterruptGroup* group,
    _In_ int                      reap)
{
    struct __InterruptGroup** link;
    oserr_t                   status;
    uint32_t                  i;
    
    // Stop new handler calls before touching any vector. Ordinary release expects
    // the caller to have stopped device writes. After driver death, keep message
    // slots unavailable for reuse because the device may still send interrupts.
    atomic_store(&group->Enabled, 0);
    if (!reap) {
        status = __DisableGroup(group);
        if (status == OS_EOK) {
            status = __UnbindGroup(group);
        }
        if (status != OS_EOK) {
            __EndGroup(group);
            return status;
        }
    }

    // Reaping also disables and unmaps each vector here. Keep the group and
    // its completion references if any step fails, so another worker can retry.
    group->Unbinding = 1;
    for (i = 0; i < group->Count; i++) {
        if (group->Vectors[i].Id == UUID_INVALID) {
            continue;
        }
        
        if (reap) {
            status = InterruptReap(group->Vectors[i].Id, __ShouldKeepGroupVector(group));
        } else {
            status = InterruptRelease(group->Vectors[i].Id, __ShouldKeepGroupVector(group));
        }
        
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
    
    // Every vector has waited for running handlers to finish, including vectors
    // removed on an earlier attempt. Only now may we drop the group's uses of
    // provider callbacks and allow the provider to release their mappings.
    for (i = 0; i < group->Count; i++) {
        InterruptCompletionPut(group->Vectors[i].Completion);
    }

    kfree(group);
    return OS_EOK;
}

oserr_t
InterruptGroupRelease(
    _In_ uuid_t handle)
{
    struct __InterruptGroup* group;
    oserr_t                  status;

    status = __BeginGroup(handle, &group);
    if (status != OS_EOK) {
        return status;
    }

    if (group->Provider.Provider != UUID_INVALID && !group->Provider.Closed) {
        // The provider still owns hardware capable of writing this message.
        // It must block those writes and close its attachment before group release.
        __EndGroup(group);
        return OS_EBUSY;
    }
    return __ReleaseGroup(group, 0);
}

oserr_t
InterruptGroupReap(
    _In_ uuid_t handle)
{
    struct __InterruptGroup* group;
    oserr_t                  status;

    status = __BeginGroup(handle, &group);
    if (status != OS_EOK) {
        return status;
    }

    // Process identity is shared by all native threads. A worker exiting must
    // not look like driver death, and a cleanup job may move between workers.
    if (group->KernelHandlers || LookupHandleOfType(group->Owner, HandleTypeMSContext) != NULL) {
        __EndGroup(group);
        return OS_EBUSY;
    }
    
    // The driver is gone and cannot confirm what its hardware may still send.
    // Treat its message slots as previously used, even if enable was not recorded.
    group->EverEnabled = 1;
    status = __ReleaseGroup(group, 1);
    
    // ENOENT denotes an absent group only, never unfinished vector cleanup.
    return status == OS_ENOENT ? OS_EUNKNOWN : status;
}

static oserr_t
__ProviderGroupIdle(
    _In_ struct __InterruptGroup* group)
{
    SystemInterrupt_t* entry;
    uint32_t          i;
    int               bound;

    if (atomic_load(&group->Enabled) || group->Unbinding) {
        return OS_EBUSY;
    }

    // Disabled alone is not enough: a handler entry may still point to the
    // completion. Unbind waits for running handlers and removes that pointer
    // before clearing Bound. Failed cleanup leaves Bound set and blocks detach.
    for (i = 0; i < group->Count; i++) {
        entry = InterruptGet(group->Vectors[i].Id);
        if (entry == NULL) {
            return OS_EBUSY;
        }
        bound = entry->Bound;

        InterruptPut(entry);
        if (bound) {
            return OS_EBUSY;
        }
    }
    return OS_EOK;
}

static oserr_t
__ProviderAttachGroup(
    _InOut_ struct __InterruptGroup*                     group,
    _In_    const struct InterruptProviderAttachRequest* request)
{
    struct __InterruptGroupVector* vector;
    oserr_t                        status;
    uint32_t                       i;

    if (group->Provider.Closed) {
        return OS_ENOENT;
    }
    
    if (group->ControllerID != request->ControllerID || group->Count != request->Count) {
        return OS_EINVALPARAMS;
    }

    if (group->Provider.Provider != UUID_INVALID) {
        // Returning messages to the provider may have failed after attachment
        // succeeded. Accept the same request even if the driver has since bound
        // or enabled its handlers. Compare input handles only; Message is output.
        // Do not add references again or change callbacks that could be running.
        for (i = 0; i < group->Count; i++) {
            if (group->Vectors[i].CompletionHandle != request->Vectors[i].Completion) {
                return OS_EBUSY;
            }
        }
        goto messages;
    }
    
    status = __ProviderGroupIdle(group);
    if (status != OS_EOK) {
        return status;
    }
    
    // No handlers are bound and Updating excludes other group operations. Take
    // one reference per callback before recording the provider's attachment.
    // If any callback is unavailable, undo all earlier references below.
    for (i = 0; i < group->Count; i++) {
        vector = &group->Vectors[i];
        if (request->Vectors[i].Completion != UUID_INVALID) {
            status = InterruptCompletionAcquire(
                request->Vectors[i].Completion,
                &vector->Completion
            );
            if (status != OS_EOK) {
                goto rollback;
            }
        }
    }

    // All callbacks are ready. Record the attachment before returning messages
    // that the provider will use to program hardware. Later driver binds take
    // their completion pointers from this group, including after an unbind.
    for (i = 0; i < group->Count; i++) {
        group->Vectors[i].CompletionHandle = request->Vectors[i].Completion;
    }

    group->Provider.Provider = InterruptCurrentOwner();
    group->Provider.Device = request->Device;

messages:
    // Return the kernel's reserved message for each vector. Never accept an
    // address or data value from the provider as the identity of a reservation.
    for (i = 0; i < group->Count; i++) {
        vector = &group->Vectors[i];
        request->Vectors[i].Message.AddressLow = (uint32_t)vector->Message.Address;
        request->Vectors[i].Message.AddressHigh = (uint32_t)(vector->Message.Address >> 32U);
        request->Vectors[i].Message.Data = vector->Message.Data;
    }
    return OS_EOK;

rollback:
    // The failing element acquired nothing. Undo only earlier successful uses;
    // no provider identity has been recorded, so a fresh attachment can retry.
    while (i != 0) {
        i--;
        InterruptCompletionPut(group->Vectors[i].Completion);
        group->Vectors[i].Completion = NULL;
    }
    return status;
}

static oserr_t
__ProviderCloseGroup(
    _InOut_ struct __InterruptGroup* group,
    _In_    uuid_t                   device)
{
    uint32_t i;
    oserr_t  status;

    if (group->Provider.Closed) {
        return OS_EOK;
    }

    status = __ProviderGroupIdle(group);
    if (status != OS_EOK) {
        return status;
    }
    
    // Close can arrive before an earlier attachment request has been processed.
    // Remember closure in the group even if nothing was attached, so that delayed
    // request cannot restart a device whose shutdown has already been requested.
    group->Provider.Provider = InterruptCurrentOwner();
    group->Provider.Device = device;
    group->Provider.Closed = 1;
    
    for (i = 0; i < group->Count; i++) {
        InterruptCompletionPut(group->Vectors[i].Completion);
        group->Vectors[i].Completion = NULL;
    }
    return OS_EOK;
}

static oserr_t
__BeginProviderGroup(
    _In_  uuid_t                   handle,
    _In_  uuid_t                   principal,
    _In_  uuid_t                   device,
    _Out_ struct __InterruptGroup** groupOut)
{
    struct __InterruptGroup* group;
    oserr_t                  status;

    if (principal == UUID_INVALID || device == UUID_INVALID) {
        return OS_EPERMISSIONS;
    }

    if (handle == UUID_INVALID) {
        return OS_EINVALPARAMS;
    }
    
    status = __BeginGroup(handle, &group);
    if (status != OS_EOK) {
        return status;
    }
    
    // Principal is the driver's process identity, obtained by the provider from
    // the message transport's verified sender information. Compare it with the
    // recorded owner: knowing a group handle alone does not authorize attachment.
    // This provider API currently supports MSI-X groups only. Once attached,
    // both the calling provider and its device identity must continue to match.
    if (group->KernelHandlers || group->Owner != principal) {
        status = OS_EPERMISSIONS;
    } else if (group->Strategy != INTERRUPT_STRATEGY_MSIX) {
        status = OS_ENOTSUPPORTED;
    } else if (group->Provider.Provider != UUID_INVALID &&
               (group->Provider.Provider != InterruptCurrentOwner() ||
                group->Provider.Device != device)) {
        status = OS_EPERMISSIONS;
    } else {
        *groupOut = group;
        return OS_EOK;
    }
    __EndGroup(group);
    return status;
}

oserr_t
InterruptGroupProviderAttach(
    _In_ const struct InterruptProviderAttachRequest* request)
{
    struct __InterruptGroup* group;
    oserr_t                  status;

    if (request->Count == 0 || request->Count > INTERRUPT_PROVIDER_MAX_VECTORS) {
        return OS_EINVALPARAMS;
    }

    if (request->Vectors == NULL) {
        return OS_EINVALPARAMS;
    }
    
    status = __BeginProviderGroup(
        request->Allocation,
        request->Principal,
        request->Device,
        &group
    );
    if (status != OS_EOK) {
        return status;
    }
    
    status = __ProviderAttachGroup(group, request);
    __EndGroup(group);
    return status;
}

oserr_t
InterruptGroupProviderCheckEnabled(
    _In_ uuid_t handle,
    _In_ uuid_t principal,
    _In_ uuid_t device)
{
    struct __InterruptGroup* group;
    oserr_t                  status;

    status = __BeginProviderGroup(handle, principal, device, &group);
    if (status != OS_EOK) {
        return status;
    }

    // This is a current-state check, not a reservation of that state. The driver
    // can disable again after __EndGroup allows other operations to proceed.
    status = !group->Provider.Closed && group->Provider.Provider != UUID_INVALID &&
        atomic_load(&group->Enabled) ? OS_EOK : OS_EBUSY;
    __EndGroup(group);
    return status;
}

oserr_t
InterruptGroupProviderClose(
    _In_ uuid_t handle,
    _In_ uuid_t principal,
    _In_ uuid_t device)
{
    struct __InterruptGroup* group;
    oserr_t                  status;

    status = __BeginProviderGroup(handle, principal, device, &group);
    if (status != OS_EOK) {
        return status;
    }
    
    status = __ProviderCloseGroup(group, device);
    __EndGroup(group);
    return status;
}
