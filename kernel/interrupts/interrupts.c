/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Interrupt Interface
 * - Contains the shared kernel interrupt interface
 *   that is generic and can be shared/used by all systems
 */

#define __MODULE "INIF"
//#define __TRACE

#include <arch/interrupts.h>
#include <arch/platform.h>
#include <arch/thread.h>
#include <arch/utils.h>
#include <assert.h>
#include <component/cpu.h>
#include <component/ic.h>
#include <ddk/interrupt.h>
#include <deviceio.h>
#include <debug.h>
#include <heap.h>
#include <handle.h>
#include <memoryspace.h>
#include <spinlock.h>
#include <interrupts.h>
#include <interrupts_chain.h>
#include <interrupts_resources.h>
#include <threading.h>
#include <string.h>

// All handlers sharing one interrupt table slot, plus the counts used to decide
// whether the slot can be reserved and whether hardware delivery is needed.
typedef struct InterruptTableEntry {
    _Atomic(SystemInterrupt_t*) Descriptor;
    int                         Penalty;
    int                         Sharable;
    unsigned int                EnabledCount;
} InterruptTableEntry_t;

static InterruptTableEntry_t g_interruptTable[MAX_SUPPORTED_INTERRUPTS] = { { 0 } };
static Spinlock_t            g_interruptTableLock                       = OS_SPINLOCK_INIT;
static _Atomic(uuid_t)       g_nextInterruptId                          = 0;

/*
 * Count interrupt handlers walking the table so they do not need its lock.
 * Removing an entry first takes it out of the table, then delays freeing it
 * until every walk that could have seen it has finished. Resource cleanup also
 * waits for this count before removing mappings used by running callbacks.
 */
static _Atomic(unsigned int) g_interruptReaders = 0;
static SystemInterrupt_t*    g_retiredInterrupts;
static _Atomic(int)          g_retiredGroupEnabled;

static void
InterruptReadEnter(void)
{
    // Count this walk before loading any table pointer. Cleanup must see that
    // the walk could still be using an entry that it is about to remove.
    atomic_fetch_add(&g_interruptReaders, 1);
}

static void
InterruptReadExit(void)
{
    // Both the device handler and its completion must finish before leaving;
    // cleanup treats a zero count as permission to remove their mappings.
    atomic_fetch_sub(&g_interruptReaders, 1);
}

static void
InterruptRetire(
        _In_ SystemInterrupt_t* Entry)
{
    // A running handler may still hold this entry even after table removal.
    // Queue it for later cleanup; the caller already holds the table lock.
    Entry->RetiredLink = g_retiredInterrupts;
    g_retiredInterrupts = Entry;
}

// Must be called with g_interruptTableLock held.
static void
InterruptReleasePenaltyLocked(
        _In_ SystemInterrupt_t* Entry)
{
    InterruptTableEntry_t* tableEntry = &g_interruptTable[LOWORD(Entry->Id)];

    if (!Entry->HasPenalty) {
        return;
    }
    Entry->HasPenalty = 0;
    if (tableEntry->Penalty > 0) {
        tableEntry->Penalty--;
    }
    if (tableEntry->Penalty == 0) {
        tableEntry->Sharable = 0;
    }
}

static void
InterruptReclaimRetired(void)
{
    SystemInterrupt_t* retired;
    SystemInterrupt_t* next;
    SystemInterrupt_t* reclaimable = NULL;
    oserr_t            status;

    // Never reclaim an entry while an interrupt handler can still reference it.
    if (atomic_load(&g_interruptReaders) != 0) {
        return;
    }

    SpinlockAcquireIrq(&g_interruptTableLock);
    if (atomic_load(&g_interruptReaders) != 0) {
        SpinlockReleaseIrq(&g_interruptTableLock);
        return;
    }

    // Keep descriptors with an outstanding InterruptGet() reference on the
    // retired list. A reference protects the descriptor's identity fields;
    // handler mappings are protected separately by the dispatch reader count.
    retired = g_retiredInterrupts;
    g_retiredInterrupts = NULL;
    while (retired != NULL) {
        next = retired->RetiredLink;
        // No table walk still uses this removed entry, so its reservation no
        // longer needs to count against new users of the slot.
        InterruptReleasePenaltyLocked(retired);
        if (atomic_load(&retired->References) == 0) {
            retired->RetiredLink = reclaimable;
            reclaimable = retired;
        } else {
            retired->RetiredLink = g_retiredInterrupts;
            g_retiredInterrupts = retired;
        }
        retired = next;
    }
    SpinlockReleaseIrq(&g_interruptTableLock);

    while (reclaimable != NULL) {
        next = reclaimable->RetiredLink;
        if (reclaimable->Owner != UUID_INVALID) {
            status = InterruptReleaseResources(reclaimable->Id, &reclaimable->KernelResources);
            if (status != OS_EOK) {
                ERROR("InterruptReclaimRetired: InterruptReleaseResources id %u failed: %u",
                      reclaimable->Id, status);
            }
        }
        kfree(reclaimable);
        reclaimable = next;
    }
}

oserr_t
InterruptIncreasePenalty(
    _In_ int Source)
{
    // Sanitize the requested source bounds
    if (Source < 0 || Source >= MAX_SUPPORTED_INTERRUPTS) {
        WARNING("InterruptIncreasePenalty: invalid source %d", Source);
        return INTERRUPT_NONE;
    }
    SpinlockAcquireIrq(&g_interruptTableLock);
    g_interruptTable[Source].Penalty++;
    SpinlockReleaseIrq(&g_interruptTableLock);
    return OS_EOK;
}

oserr_t
InterruptDecreasePenalty(
    _In_ int Source)
{
    // Sanitize the requested source bounds
    if (Source < 0 || Source >= MAX_SUPPORTED_INTERRUPTS) {
        WARNING("InterruptDecreasePenalty: invalid source %d", Source);
        return INTERRUPT_NONE;
    }
    SpinlockAcquireIrq(&g_interruptTableLock);
    g_interruptTable[Source].Penalty--;
    SpinlockReleaseIrq(&g_interruptTableLock);
    return OS_EOK;
}

int
InterruptGetPenalty(
    _In_ int Source)
{
    int penalty;

    // Sanitize the requested source bounds
    if (Source < 0 || Source >= MAX_SUPPORTED_INTERRUPTS) {
        WARNING("InterruptGetPenalty: invalid source %d", Source);
        return INTERRUPT_NONE;
    }

    SpinlockAcquireIrq(&g_interruptTableLock);
    penalty = g_interruptTable[Source].Penalty;
    if (g_interruptTable[Source].Sharable == 0 && penalty > 0) {
        penalty = INTERRUPT_NONE;
    }
    SpinlockReleaseIrq(&g_interruptTableLock);
    return penalty;
}

int
InterruptGetLeastLoaded(
    _In_ int interruptVectors[],
    _In_ int count)
{
    int SelectedPenality = INTERRUPT_NONE;
    int SelectedIrq      = INTERRUPT_NONE;
    int Penalty;
    int i;

    TRACE("InterruptGetLeastLoaded(count=%d)", count);

    // Iterate all the available irqs
    // that the device-supports
    for (i = 0; i < count; i++) {
        if (interruptVectors[i] == INTERRUPT_NONE) {
            break;
        }

        // Calculate count
        Penalty = InterruptGetPenalty(interruptVectors[i]);

        // Sanitize status, if -1 then its not usable
        if (Penalty == INTERRUPT_NONE) {
            continue;
        }
        TRACE("InterruptGetLeastLoaded: selected vector %d, penalty %d", SelectedIrq, SelectedPenality);

        // Store the lowest penalty
        if (SelectedIrq == INTERRUPT_NONE || Penalty < SelectedPenality) {
            SelectedIrq = interruptVectors[i];
            SelectedPenality = Penalty;
        }
    }
    return SelectedIrq;
}

// Release the kernel I/O descriptions and mappings created for a callback.
static oserr_t
__InterruptCleanupIoResources(
    _In_    uuid_t                    id,
    _InOut_ InterruptResourceTable_t* Resources)
{
    oserr_t Status;

    TRACE("InterruptCleanupIoResources(id=%u)", id);
    // Clear an entry only after release succeeds. A retry then skips resources
    // already released and still knows which resource failed.
    for (int i = 0; i < INTERRUPT_MAX_IO_RESOURCES; i++) {
        if (Resources->IoResources[i] != NULL) {
            Status = ReleaseKernelSystemDeviceIo(Resources->IoResources[i]);
            if (Status != OS_EOK) {
                ERROR("InterruptCleanupIoResources: ReleaseKernelSystemDeviceIo id %u, resource %d failed: %u",
                      id, i, Status);
                return Status;
            }
            Resources->IoResources[i] = NULL;
        }
    }
    return OS_EOK;
}

// Give the callback kernel-accessible descriptions and mappings for its I/O.
static oserr_t
__InterruptResolveIoResources(
    _In_    uuid_t                    id,
    _In_    InterruptResourceTable_t* Source,
    _InOut_ InterruptResourceTable_t* Destination)
{
    oserr_t Status;
    oserr_t cleanupStatus;

    TRACE("InterruptResolveIoResources(id=%u)", id);
    // The callback runs at kernel addresses and cannot use the provider's I/O
    // description pointers directly. Record each kernel resource as it is made
    // so an error later in the list can still be cleaned up.
    for (int i = 0; i < INTERRUPT_MAX_IO_RESOURCES; i++) {
        if (Source->IoResources[i] != NULL) {
            Status = CreateKernelSystemDeviceIo(Source->IoResources[i], &Destination->IoResources[i]);
            if (Status != OS_EOK) {
                ERROR("InterruptResolveIoResources: CreateKernelSystemDeviceIo id %u, resource %d failed: %u",
                      id, i, Status);
                goto error;
            }
        }
    }
    
    return OS_EOK;

error:
    cleanupStatus = __InterruptCleanupIoResources(id, Destination);
    if (cleanupStatus != OS_EOK) {
        ERROR("InterruptResolveIoResources: InterruptCleanupIoResources id %u failed: %u",
              id, cleanupStatus);
    }
    return OS_EUNKNOWN;
}

// Remove the kernel mappings recorded for the callback's memory resources.
static oserr_t
__InterruptCleanupMemoryResources(
    _In_    uuid_t                    id,
    _InOut_ InterruptResourceTable_t* Resources)
{
    oserr_t   Status;
    uintptr_t Offset;
    size_t    Length;

    TRACE("InterruptCleanupMemoryResources(id=%u)", id);
    // Mapping begins at a page boundary even when the requested buffer does
    // not. Include that initial offset to unmap the full range we created.
    for (int i = 0; i < INTERRUPT_MAX_MEMORY_RESOURCES; i++) {
        if (Resources->MemoryResources[i].Address != 0) {
            Offset = Resources->MemoryResources[i].Address % GetMemorySpacePageSize();
            Length = Resources->MemoryResources[i].Length + Offset;

            Status = MemorySpaceUnmap(GetCurrentMemorySpace(),
                Resources->MemoryResources[i].Address, Length);
            if (Status != OS_EOK) {
                ERROR("InterruptCleanupMemoryResources: MemorySpaceUnmap id %u, resource %d, address 0x%" PRIxIN ", length %zu failed: %u",
                      id, i, Resources->MemoryResources[i].Address, Length, Status);
                return Status;
            }
            // Preserve failed entries above; only successful unmaps may be
            // forgotten, otherwise a retry would lose track of live mappings.
            Resources->MemoryResources[i].Address = 0;
        }
    }
    return OS_EOK;
}

// Map the callback's existing buffers at addresses it can use inside the kernel.
static oserr_t
__InterruptResolveMemoryResources(
    _In_    uuid_t                    id,
    _In_    InterruptResourceTable_t* Source,
    _InOut_ InterruptResourceTable_t* Destination)
{
    InterruptResourceTable_t* source = Source;
    InterruptResourceTable_t* destination = Destination;
    oserr_t                   oserr;
    oserr_t                   cleanupStatus;
    uintptr_t                 updatedMapping;
    uintptr_t                 offset;
    size_t                    length;
    unsigned int              pageFlags;
    unsigned int              placementFlags;

    TRACE("InterruptResolveMemoryResources(id=%u)", id);
    // Map the same memory at kernel-accessible addresses; this does not make
    // independent copies of the buffer contents. Restore each buffer's offset
    // within its first page so the callback sees the intended start address.
    for (int i = 0; i < INTERRUPT_MAX_MEMORY_RESOURCES; i++) {
        if (source->MemoryResources[i].Address != 0) {
            offset         = source->MemoryResources[i].Address % GetMemorySpacePageSize();
            length         = source->MemoryResources[i].Length + offset;
            pageFlags      = MAPPING_COMMIT | MAPPING_PERSISTENT;
            placementFlags = MAPPING_VIRTUAL_GLOBAL | MAPPING_PHYSICAL_FIXED;
            if (source->MemoryResources[i].Flags & INTERRUPT_RESOURCE_DISABLE_CACHE) {
                pageFlags |= MAPPING_NOCACHE;
            }

            oserr = MemorySpaceCloneMapping(
                GetCurrentMemorySpace(),
                GetCurrentMemorySpace(),
                source->MemoryResources[i].Address,
                &updatedMapping,
                length,
                pageFlags,
                placementFlags
            );
            if (oserr != OS_EOK) {
                ERROR("InterruptResolveMemoryResources: MemorySpaceCloneMapping id %u, resource %d, address 0x%" PRIxIN ", length %zu failed: %u",
                      id, i, source->MemoryResources[i].Address, length, oserr);
                goto error;
            }
            TRACE("InterruptResolveMemoryResources: id %u, resource %d remapped from 0x%" PRIxIN " to 0x%" PRIxIN,
                  id, i, source->MemoryResources[i].Address, updatedMapping + offset);
            destination->MemoryResources[i].Address = updatedMapping + offset;
            destination->MemoryResources[i].Length  = source->MemoryResources[i].Length;
            destination->MemoryResources[i].Flags   = source->MemoryResources[i].Flags;
        }
    }

    return OS_EOK;

error:
    cleanupStatus = __InterruptCleanupMemoryResources(id, Destination);
    if (cleanupStatus != OS_EOK) {
        ERROR("InterruptResolveMemoryResources: InterruptCleanupMemoryResources id %u failed: %u",
              id, cleanupStatus);
    }
    return OS_EUNKNOWN;
}

oserr_t
InterruptResolveResources(
    _In_    uuid_t                    id,
    _In_    InterruptResourceTable_t* Source,
    _InOut_ InterruptResourceTable_t* Destination)
{
    unsigned int PlacementFlags;
    unsigned int PageFlags;
    oserr_t      Status;
    oserr_t      cleanupStatus;
    uintptr_t    Virtual;
    uintptr_t    Offset;
    size_t       Length;

    TRACE("InterruptResolveResources(id=%u, handler=0x%" PRIxIN ")",
          id, (uintptr_t)Source->Handler);

    // Map the same code window for child handlers and provider callbacks.
    // Preserve the entry point's page offset; callers must keep the callback
    // within this window and use the supplied function table for external calls.
    Offset         = ((uintptr_t)Source->Handler) % GetMemorySpacePageSize();
    Length         = GetMemorySpacePageSize() + Offset;
    PageFlags      = MAPPING_COMMIT | MAPPING_EXECUTABLE | MAPPING_READONLY;
    PlacementFlags = MAPPING_VIRTUAL_GLOBAL;
    
    Status         = MemorySpaceCloneMapping(
        GetCurrentMemorySpace(),
        GetCurrentMemorySpace(),
        (vaddr_t) Source->Handler,
        &Virtual,
        Length,
        PageFlags,
        PlacementFlags
    );
    if (Status != OS_EOK) {
        ERROR("InterruptResolveResources: MemorySpaceCloneMapping handler 0x%" PRIxIN ", length %zu failed: %u",
              (uintptr_t)Source->Handler, Length, Status);
        return Status;
    }
    Virtual += Offset;

    TRACE("InterruptResolveResources: handler remapped from 0x%" PRIxIN " to 0x%" PRIxIN,
          (uintptr_t)Source->Handler, Virtual);
    // Record the code mapping before mapping data. Release uses Handler as the
    // marker that cleanup is still required, including after a later failure.
    Destination->Handler = (InterruptHandler_t)Virtual;

    Status = __InterruptResolveIoResources(id, Source, Destination);
    if (Status != OS_EOK) {
        ERROR("InterruptResolveResources: InterruptResolveIoResources id %u failed: %u",
              id, Status);
        goto error;
    }

    Status = __InterruptResolveMemoryResources(id, Source, Destination);
    if (Status != OS_EOK) {
        ERROR("InterruptResolveResources: InterruptResolveMemoryResources id %u failed: %u",
              id, Status);
        goto error;
    }
    return OS_EOK;

error:
    // Cleanup can fail too. Return the original mapping error while keeping
    // any remaining mappings in Destination for an explicit release retry.
    cleanupStatus = InterruptReleaseResources(id, Destination);
    if (cleanupStatus != OS_EOK) {
        ERROR("InterruptResolveResources: InterruptReleaseResources id %u failed: %u",
              id, cleanupStatus);
    }
    return Status;
}

/* InterruptReleaseResources
 * Releases mappings owned by a child handler or provider completion. */
oserr_t
InterruptReleaseResources(
    _In_    uuid_t                    id,
    _InOut_ InterruptResourceTable_t* Resources)
{
    InterruptResourceTable_t* resources = Resources;
    oserr_t                   osStatus;
    uintptr_t                 Offset;
    size_t                    Length;

    TRACE("InterruptReleaseResources(id=%u)", id);
    // Code is mapped first and released last, so a cleared Handler means this
    // table has nothing left to release, including on repeated cleanup calls.
    if ((uintptr_t)resources->Handler == 0) {
        return OS_EOK;
    }

    // Release data mappings first and the handler last. Keeping Handler set
    // until the end lets a failed cleanup retry the remaining resources.
    osStatus = __InterruptCleanupIoResources(id, Resources);
    if (osStatus != OS_EOK) {
        return osStatus;
    }
    
    osStatus = __InterruptCleanupMemoryResources(id, Resources);
    if (osStatus != OS_EOK) {
        return osStatus;
    }
    
    Offset = ((uintptr_t)resources->Handler) % GetMemorySpacePageSize();
    Length = GetMemorySpacePageSize() + Offset;
    
    osStatus = MemorySpaceUnmap(GetCurrentMemorySpace(),
        (uintptr_t)resources->Handler, Length);
    if (osStatus != OS_EOK) {
        ERROR("InterruptReleaseResources: MemorySpaceUnmap handler id %u, address 0x%" PRIxIN ", length %zu failed: %u",
              id, (uintptr_t)resources->Handler, Length, osStatus);
        return osStatus;
    }
    
    resources->Handler = NULL;
    return OS_EOK;
}

static int
__MatchesMessageVector(
    _In_ const InterruptDescriptor_t* descriptor,
    _In_ unsigned int                 flags,
    _In_ uuid_t                       index)
{
    if (flags & INTERRUPT_VECTOR) {
        for (int i = 0; i < INTERRUPT_MAXVECTORS; i++) {
            if (descriptor->Vectors[i] == INTERRUPT_NONE) {
                break;
            }
            if ((uuid_t)descriptor->Vectors[i] == index) {
                return 1;
            }
        }
        return 0;
    }
    return descriptor->Line == INTERRUPT_NONE || (uuid_t)descriptor->Line == index;
}

static oserr_t
__ReserveInterrupt(
    _InOut_ InterruptDescriptor_t*       descriptor,
    _In_    unsigned int                 flags,
    _In_    SystemInterruptController_t* controller,
    _In_    uint32_t                     ordinal,
    _Out_   uuid_t*                      idOut)
{
    SystemInterrupt_t*     entry;
    InterruptTableEntry_t* slot;
    uuid_t                 index;
    oserr_t                status;
    oserr_t                releaseStatus;

    TRACE("__ReserveInterrupt(line=%d, flags=0x%x, controller=%p, ordinal=%u)",
          descriptor->Line, flags, controller, ordinal);
    // Start out by reclaiming retired interrupts so we have the most
    // resources available to us when doing the reservation.
    InterruptReclaimRetired();

    entry = kmalloc(sizeof(SystemInterrupt_t));
    if (entry == NULL) {
        ERROR("__ReserveInterrupt: descriptor allocation failed");
        return OS_EOOM;
    }

    memset(entry, 0, sizeof(SystemInterrupt_t));
    
    if (controller != NULL) {
        status = InterruptControllerAllocateMessage(controller, ordinal, flags, &entry->Routing);
    } else {
        status = InterruptControllerResolve(descriptor, flags, &entry->Routing);
    }
    if (status != OS_EOK) {
        kfree(entry);
        return status;
    }
    
    index = entry->Routing.TableIndex;
    if (index >= MAX_SUPPORTED_INTERRUPTS || (index & INTERRUPT_GROUP_TAG) == INTERRUPT_GROUP_TAG) {
        ERROR("__ReserveInterrupt: controller returned invalid table index %u", index);
        status = OS_EINVALPARAMS;
        goto rollback;
    }
    
    if (controller != NULL && !__MatchesMessageVector(descriptor, flags, index)) {
        TRACE("__ReserveInterrupt: table index %u does not match the requested vectors", index);
        status = OS_EBUSY;
        goto rollback;
    }
    
    atomic_init(&entry->References, 0);
    atomic_init(&entry->Enabled, 0);
    atomic_init(&entry->Link, NULL);
    entry->Id = (atomic_fetch_add(&g_nextInterruptId, 1) << 16U) | index;
    entry->Owner = (flags & INTERRUPT_KERNEL) ? UUID_INVALID : InterruptCurrentOwner();
    entry->Thread = ThreadCurrentHandle();
    entry->Flags = flags;
    entry->Context = descriptor->Context;

    SpinlockAcquireIrq(&g_interruptTableLock);
    
    slot = &g_interruptTable[index];
    // Retired handlers may still be running. Their reservation must also prevent
    // an exclusive claimant from taking the same line before they finish.
    if (slot->Penalty > 0 && ((flags & INTERRUPT_EXCLUSIVE) || !slot->Sharable)) {
        SpinlockReleaseIrq(&g_interruptTableLock);
        TRACE("__ReserveInterrupt: table index %u is already claimed", index);
        status = OS_EBUSY;
        goto rollback;
    }
    
    if (slot->Penalty == 0) {
        slot->Sharable = !(flags & INTERRUPT_EXCLUSIVE);
    }
    slot->Penalty++;
    
    entry->HasPenalty = 1;
    atomic_store(&entry->Link, atomic_load(&slot->Descriptor));
    atomic_store(&slot->Descriptor, entry);
    
    // Store the id while we got the lock for consistency
    *idOut = entry->Id;
    SpinlockReleaseIrq(&g_interruptTableLock);

    if (controller != NULL) {
        descriptor->Line = entry->Routing.Line;
        descriptor->MsiAddress = entry->Routing.Message.Address;
        descriptor->MsiValue = entry->Routing.Message.Data;
    }
    TRACE("__ReserveInterrupt: reserved id %u, index %u, owner %u", *idOut, index, entry->Owner);
    return OS_EOK;

rollback:
    // No handler or device has received this resource yet. The platform must
    // release it if another caller reserves the handler-table slot first.
    releaseStatus = InterruptControllerRelease(&entry->Routing, 0);
    if (releaseStatus != OS_EOK) {
        ERROR("__ReserveInterrupt: InterruptControllerRelease index %u failed during rollback: %u",
              index, releaseStatus);
    }
    assert(releaseStatus == OS_EOK);
    (void)releaseStatus;
    kfree(entry);
    return status;
}

oserr_t
InterruptGetMessageCapacity(
    _In_  uuid_t    domain,
    _In_  int       kernelHandlers,
    _Out_ uint32_t* capacityOut)
{
    SystemInterruptController_t* controller;
    oserr_t                      status;

    TRACE("InterruptGetMessageCapacity(domain=%u, kernel=%d)", domain, kernelHandlers);
    if (capacityOut == NULL || (kernelHandlers != 0 && kernelHandlers != 1)) {
        WARNING("InterruptGetMessageCapacity: missing output or invalid kernel flag %d", kernelHandlers);
        return OS_EINVALPARAMS;
    }

    status = InterruptMessageControllerLookup(domain, kernelHandlers, &controller);
    if (status != OS_EOK) {
        return status;
    }
    *capacityOut = InterruptMessageControllerCapacity(controller);
    TRACE("InterruptGetMessageCapacity: domain %u capacity %u", domain, *capacityOut);
    return OS_EOK;
}

oserr_t
InterruptReserveMessage(
    _In_  uuid_t              domain,
    _In_  uint32_t            ordinal,
    _In_  unsigned int        flags,
    _Out_ uuid_t*             idOut,
    _Out_ InterruptMessage_t* messageOut)
{
    SystemInterruptController_t* controller;
    InterruptDescriptor_t        descriptor = { 0 };
    oserr_t                      status;

    TRACE("InterruptReserveMessage(domain=%u, ordinal=%u, flags=0x%x)", domain, ordinal, flags);
    if (idOut == NULL || messageOut == NULL) {
        WARNING("InterruptReserveMessage: missing reservation or message output");
        return OS_EINVALPARAMS;
    }

    *idOut = UUID_INVALID;
    if (flags & (INTERRUPT_SOFT | INTERRUPT_VECTOR)) {
        WARNING("InterruptReserveMessage: incompatible flags 0x%x", flags);
        return OS_EINVALPARAMS;
    }
    
    status = InterruptMessageControllerLookup(domain, !!(flags & INTERRUPT_KERNEL), &controller);
    if (status != OS_EOK) {
        return status;
    }
    
    descriptor.Line = INTERRUPT_NONE;
    descriptor.Pin = INTERRUPT_NONE;
    flags |= INTERRUPT_MSI | INTERRUPT_EXCLUSIVE;
    status = __ReserveInterrupt(&descriptor, flags, controller, ordinal, idOut);
    if (status != OS_EOK) {
        return status;
    }
    messageOut->Address = descriptor.MsiAddress;
    messageOut->Data = (uint32_t)descriptor.MsiValue;
    return OS_EOK;
}

oserr_t
InterruptReserve(
    _InOut_ InterruptDescriptor_t* descriptor,
    _In_    unsigned int           flags,
    _Out_   uuid_t*                idOut)
{
    SystemInterruptController_t* controller;
    uint32_t                     ordinal;
    uint32_t                     capacity;
    oserr_t                      status;

    TRACE("InterruptReserve(descriptor=%p, flags=0x%x)", descriptor, flags);
    if (descriptor == NULL || idOut == NULL) {
        WARNING("InterruptReserve: missing descriptor or output");
        return OS_EINVALPARAMS;
    }
    
    *idOut = UUID_INVALID;
    if (!(flags & INTERRUPT_MSI)) {
        return __ReserveInterrupt(descriptor, flags, NULL, 0, idOut);
    }
    if (flags & INTERRUPT_SOFT) {
        WARNING("InterruptReserve: MSI cannot use software interrupt flags 0x%x", flags);
        return OS_EINVALPARAMS;
    }

    status = InterruptMessageControllerLookup(UUID_INVALID, !!(flags & INTERRUPT_KERNEL), &controller);
    if (status != OS_EOK) {
        return status;
    }
    
    capacity = InterruptMessageControllerCapacity(controller);
    if (capacity == 0) {
        TRACE("InterruptReserve: controller %u has no message vectors", controller->Id);
        return OS_ENOTSUPPORTED;
    }
    
    flags |= INTERRUPT_EXCLUSIVE;
    for (ordinal = 0; ordinal < capacity; ordinal++) {
        status = __ReserveInterrupt(descriptor, flags, controller, ordinal, idOut);
        if (status != OS_EBUSY) {
            return status;
        }
    }
    TRACE("InterruptReserve: all %u message vectors are unavailable", capacity);
    return OS_EBUSY;
}

// Starts one exclusive update and retains the entry while the table lock is free.
static oserr_t
__BeginInterruptUpdate(
    _In_  uuid_t              id,
    _In_  int                 reap,
    _Out_ SystemInterrupt_t** entryOut)
{
    SystemInterrupt_t* entry;
    uint16_t           index = LOWORD(id);

    if (id == UUID_INVALID || index >= MAX_SUPPORTED_INTERRUPTS) {
        WARNING("__BeginInterruptUpdate: invalid interrupt id %u", id);
        return OS_EINVALPARAMS;
    }

    SpinlockAcquireIrq(&g_interruptTableLock);

    // The low bits select a table slot, which can contain several shared-line
    // handlers. Compare the full ID to find this exact reservation.
    entry = atomic_load(&g_interruptTable[index].Descriptor);
    while (entry != NULL && entry->Id != id) {
        entry = atomic_load(&entry->Link);
    }
    
    // Ordinary process callers can change only their own reservation. Report
    // another owner's entry as absent, just like an unknown handle.
    if (entry == NULL || (!reap && !(entry->Flags & INTERRUPT_KERNEL) &&
                         entry->Owner != InterruptCurrentOwner())) {
        SpinlockReleaseIrq(&g_interruptTableLock);
        TRACE("__BeginInterruptUpdate: id %u not found for the current owner", id);
        return OS_ENOENT;
    }
    
    // Reaping only cleans up a dead process's entries. Do not transfer ownership
    // thread: a later retry may run on a different thread or service process.
    if (reap && ((entry->Flags & INTERRUPT_KERNEL) ||
        LookupHandleOfType(entry->Owner, HandleTypeMSContext) != NULL)) {
        SpinlockReleaseIrq(&g_interruptTableLock);
        return OS_EBUSY;
    }

    // If the entry is in the process of updating, try again later
    if (entry->Updating) {
        SpinlockReleaseIrq(&g_interruptTableLock);
        TRACE("__BeginInterruptUpdate: id %u is already being updated", id);
        return OS_EBUSY;
    }
    
    // Updating excludes another mutation; References keeps the entry allocated.
    // Both are needed because mapping and waiting for handlers happen unlocked.
    entry->Updating = 1;
    atomic_fetch_add(&entry->References, 1);
    SpinlockReleaseIrq(&g_interruptTableLock);
    
    *entryOut = entry;
    return OS_EOK;
}

uuid_t
InterruptCurrentOwner(void)
{
    MSContextSecurity_t* security = MemorySpaceGetSecurityContext();

    // Threads have separate address spaces for their local data, but their
    // security context identifies the whole process. Jobs can move between
    // threads without changing who owns an interrupt or a completion binding.
    return security != NULL ? security->Handle : UUID_INVALID;
}

// Releases the reference on an interrupt and ends the "updating"
// state
static void
__EndInterruptUpdate(
    _In_ SystemInterrupt_t* entry)
{
    SpinlockAcquireIrq(&g_interruptTableLock);
    entry->Updating = 0;
    SpinlockReleaseIrq(&g_interruptTableLock);
    
    InterruptPut(entry);
}

static void
__SynchronizeInterrupts(void)
{
    // Call after preventing new calls to the affected handler. Existing calls
    // may still be in the device handler or its completion on another CPU.
    // Do not wait from an interrupt handler: it would wait for itself to finish.
    while (atomic_load(&g_interruptReaders) != 0) {
        ArchThreadYield();
    }
    InterruptReclaimRetired();
}

oserr_t
InterruptValidateResources(
    _In_ InterruptResourceTable_t* resources)
{
    FastInterruptMemoryResource_t* memory;

    if (resources == NULL || resources->Handler == NULL) {
        WARNING("__ValidateInterruptResources: missing resource table or handler");
        return OS_EINVALPARAMS;
    }
    
    for (unsigned int i = 0; i < INTERRUPT_MAX_MEMORY_RESOURCES; i++) {
        memory = &resources->MemoryResources[i];
        // A completely empty slot is optional. Partially filled slots are errors
        // because the mapper would otherwise silently skip an intended resource.
        if (memory->Address == 0 && memory->Length == 0 && memory->Flags == 0) {
            continue;
        }

        if (memory->Address == 0 || memory->Length == 0) {
            WARNING("__ValidateInterruptResources: resource %u has address 0x%" PRIxIN ", length %zu",
                    i, memory->Address, memory->Length);
            return OS_EINVALPARAMS;
        }

        // Check before adding length to address; integer wraparound could turn
        // an invalid range into an apparently small valid one.
        if (memory->Length > UINTPTR_MAX - memory->Address) {
            WARNING("__ValidateInterruptResources: resource %u address range overflows", i);
            return OS_EINVALPARAMS;
        }

        if (memory->Flags & ~INTERRUPT_RESOURCE_DISABLE_CACHE) {
            WARNING("__ValidateInterruptResources: resource %u has unsupported flags 0x%x", i, memory->Flags);
            return OS_EINVALPARAMS;
        }
    }
    return OS_EOK;
}

// Binds resources to a previously allocated interrupt vector
oserr_t
InterruptBind(
    _In_ uuid_t                    id,
    _In_ InterruptResourceTable_t* resources)
{
    InterruptResourceTable_t mapped = { 0 };
    SystemInterrupt_t*       entry;
    oserr_t                  status;

    TRACE("InterruptBind(id=%u, resources=%p)", id, resources);
    if (InterruptGetActiveStatus()) {
        WARNING("InterruptBind: id %u cannot bind in interrupt context", id);
        return OS_EINVALPARAMS;
    }

    // The kernel cannot rely on libddk checking its inputs: a process can invoke
    // a syscall directly. Apply the same resource rules to every caller here.
    status = InterruptValidateResources(resources);
    if (status != OS_EOK) {
        return status;
    }
    
    // Prevent a concurrent enable or release while resources are being mapped.
    status = __BeginInterruptUpdate(id, 0, &entry);
    if (status != OS_EOK) {
        return status;
    }
    
    if (entry->Bound || atomic_load(&entry->Enabled) || entry->ControllerReleased) {
        WARNING("InterruptBind: id %u is already bound, enabled or permanently retired", id);
        // always release the reference
        __EndInterruptUpdate(entry);
        return OS_EBUSY;
    }
    
    mapped.HandleResource = resources->HandleResource;
    
    if (!(entry->Flags & INTERRUPT_KERNEL)) {
        status = InterruptResolveResources(id, resources, &mapped);
        if (status != OS_EOK) {
            ERROR("InterruptBind: InterruptResolveResources id %u failed: %u", id, status);
            if (mapped.Handler != NULL) {
                // Bound also means resources still need cleanup, not just that
                // a handler is usable. Unbinding prevents Enable from calling
                // this incomplete handler; Unbind can retry the remaining work.
                entry->KernelResources = mapped;
                entry->Bound = 1;
                entry->Unbinding = 1;
                WARNING("InterruptBind: id %u retains mappings for cleanup retry", id);
            }
            
            __EndInterruptUpdate(entry);
            return status;
        }
    }
    
    entry->KernelResources = mapped;
    entry->Handler = resources->Handler;
    entry->Bound = 1;

    // Finish the update only after the whole handler is recorded. Binding keeps
    // delivery disabled; a separate enable operation permits calls to it.
    __EndInterruptUpdate(entry);
    return OS_EOK;
}

static oserr_t
__SetInterruptEnabledLocked(
    _In_ SystemInterrupt_t* entry,
    _In_ int                enable)
{
    InterruptTableEntry_t* slot = &g_interruptTable[LOWORD(entry->Id)];
    oserr_t                status;

    if (atomic_load(&entry->Enabled) == enable) {
        return OS_EOK;
    }

    if (enable) {
        if (!entry->Bound || entry->Unbinding || entry->ControllerReleased) {
            WARNING("__SetInterruptEnabledLocked: id %u is unbound, awaiting cleanup or permanently retired",
                    entry->Id);
            return OS_EINVALPARAMS;
        }

        // Make the prepared handler available before enabling hardware, which
        // may deliver immediately. Only the first enabled user starts a shared
        // line; disabling one user must not stop interrupts for the others.
        atomic_store(&entry->Enabled, 1);
        if (slot->EnabledCount == 0) {
            status = InterruptControllerSetEnabled(&entry->Routing, 1);
            if (status != OS_EOK) {
                ERROR("__SetInterruptEnabledLocked: InterruptControllerSetEnabled id %u, enable 1 failed: %u",
                      entry->Id, status);
                atomic_store(&entry->Enabled, 0);
                return status;
            }
        }
        slot->EnabledCount++;
    } else {
        if (slot->EnabledCount == 1) {
            status = InterruptControllerSetEnabled(&entry->Routing, 0);
            if (status != OS_EOK) {
                ERROR("__SetInterruptEnabledLocked: InterruptControllerSetEnabled id %u, enable 0 failed: %u",
                      entry->Id, status);
                return status;
            }
        }
        atomic_store(&entry->Enabled, 0);
        slot->EnabledCount--;
    }
    return OS_EOK;
}

oserr_t
InterruptSetEnabled(
    _In_ uuid_t id,
    _In_ int    enable)
{
    SystemInterrupt_t* entry;
    oserr_t            status;

    TRACE("InterruptSetEnabled(id=%u, enable=%d)", id, enable);
    if ((enable != 0 && enable != 1) || InterruptGetActiveStatus()) {
        WARNING("InterruptSetEnabled: id %u has invalid enable %d or interrupt context", id, enable);
        return OS_EINVALPARAMS;
    }
    
    // Get a locked reference to the interrupt handle
    status = __BeginInterruptUpdate(id, 0, &entry);
    if (status != OS_EOK) {
        return status;
    }
    
    // Update the interrupt state of the line
    SpinlockAcquireIrq(&g_interruptTableLock);
    status = __SetInterruptEnabledLocked(entry, enable);
    SpinlockReleaseIrq(&g_interruptTableLock);
    if (!enable || status != OS_EOK) {
        // Clearing Enabled stops new calls but does not finish a call already
        // running on another CPU. Wait before the caller can clean up resources.
        __SynchronizeInterrupts();
    }
    
    // Release the reference again
    __EndInterruptUpdate(entry);
    return status;
}

// Unbinding releases resources without releasing the interrupt reservation
oserr_t
InterruptUnbind(
    _In_ uuid_t id)
{
    SystemInterrupt_t* entry;
    oserr_t            status;

    TRACE("InterruptUnbind(id=%u)", id);
    if (InterruptGetActiveStatus()) {
        WARNING("InterruptUnbind: id %u cannot unbind in interrupt context", id);
        return OS_EINVALPARAMS;
    }

    status = __BeginInterruptUpdate(id, 0, &entry);
    if (status != OS_EOK) {
        return status;
    }
    
    if (atomic_load(&entry->Enabled)) {
        WARNING("InterruptUnbind: id %u is still enabled", id);
        __EndInterruptUpdate(entry);
        return OS_EBUSY;
    }
    
    // The group may share a callback with other vectors. Wait for this entry's
    // device handler and completion before removing its pointers or mappings.
    __SynchronizeInterrupts();
    if (entry->Bound && !(entry->Flags & INTERRUPT_KERNEL)) {
        // A failed unmap can leave only part of the table valid. Keep cleanup
        // retryable, but reject Enable until every mapping has been released.
        entry->Unbinding = 1;
        status = InterruptReleaseResources(entry->Id, &entry->KernelResources);
        if (status != OS_EOK) {
            ERROR("InterruptUnbind: InterruptReleaseResources id %u failed, cleanup remains retryable: %u",
                  id, status);
            __EndInterruptUpdate(entry);
            return status;
        }
    }

    memset(&entry->KernelResources, 0, sizeof(entry->KernelResources));
    entry->Handler = NULL;
    
    // This entry borrows the callback from its group. Clearing the pointer does
    // not drop the group's reference; a later bind may use the same completion.
    entry->Completion = NULL;
    entry->Bound = 0;
    entry->Unbinding = 0;
    __EndInterruptUpdate(entry);
    return OS_EOK;
}

uuid_t
InterruptRegister(
    _In_ InterruptDescriptor_t* descriptor,
    _In_ unsigned int           flags)
{
    uuid_t  id;
    oserr_t status;
    oserr_t cleanupStatus;

    TRACE("InterruptRegister(descriptor=%p, flags=0x%x)", descriptor, flags);
    status = InterruptReserve(descriptor, flags, &id);
    if (status != OS_EOK) {
        ERROR("InterruptRegister: InterruptReserve failed: %u", status);
        return UUID_INVALID;
    }
    
    status = InterruptBind(id, &descriptor->ResourceTable);
    if (status != OS_EOK) {
        ERROR("InterruptRegister: InterruptBind id %u failed: %u", id, status);
        goto error;
    }

    status = InterruptSetEnabled(id, 1);
    if (status != OS_EOK) {
        ERROR("InterruptRegister: InterruptSetEnabled id %u failed: %u", id, status);
        goto error;
    }
    return id;

error:
    cleanupStatus = InterruptUnregister(id);
    if (cleanupStatus != OS_EOK) {
        ERROR("InterruptRegister: InterruptUnregister id %u failed during rollback: %u", id, cleanupStatus);
    }
    return UUID_INVALID;
}

/**
 * @brief Releases one reservation without disturbing other enabled users.
 * Entries removed by a handler remain on the retired list until dispatch exits;
 * normal callers wait before returning so their backing resources can be freed.
 */
static oserr_t
__ReleaseInterrupt(
    _In_ uuid_t id,
    _In_ int    retire,
    _In_ int    reap)
{
    SystemInterrupt_t*     entry;
    SystemInterrupt_t*     previous = NULL;
    SystemInterrupt_t*     iterator;
    InterruptTableEntry_t* slot;
    oserr_t                status;

    TRACE("InterruptRelease(id=%u, retire=%d)", id, retire);
    status = __BeginInterruptUpdate(id, reap, &entry);
    if (status != OS_EOK) {
        return status;
    }

    if (retire) {
        if (!(entry->Flags & INTERRUPT_MSI)) {
            WARNING("InterruptRelease: id %u cannot retire a non-message reservation", id);
            __EndInterruptUpdate(entry);
            return OS_EINVALPARAMS;
        }

        // A live owner must stop and unbind before retiring its message. For
        // a dead owner, this function performs those steps on the retry path.
        if (!reap && (entry->Bound || atomic_load(&entry->Enabled))) {
            WARNING("InterruptRelease: id %u cannot retire a bound or enabled reservation", id);
            __EndInterruptUpdate(entry);
            return OS_EINVALPARAMS;
        }
    }

    if (InterruptGetActiveStatus() && !(entry->Flags & INTERRUPT_KERNEL)) {
        // User mappings require ordinary execution context for cleanup. Kernel
        // sources can still retire themselves from their own interrupt handler.
        WARNING("InterruptRelease: id %u cannot release user mappings in interrupt context", id);
        __EndInterruptUpdate(entry);
        return OS_EBUSY;
    }
    
    SpinlockAcquireIrq(&g_interruptTableLock);
    status = __SetInterruptEnabledLocked(entry, 0);
    if (status != OS_EOK) {
        SpinlockReleaseIrq(&g_interruptTableLock);
        ERROR("InterruptRelease: disabling id %u failed: %u", id, status);
        __EndInterruptUpdate(entry);
        return status;
    }
    SpinlockReleaseIrq(&g_interruptTableLock);
    
    if (entry->Bound && !(entry->Flags & INTERRUPT_KERNEL)) {
        // Keep the disabled reservation until cleanup succeeds. A
        // failed unmap must not turn into an orphaned allocation or mapping.
        __SynchronizeInterrupts();
        entry->Unbinding = 1;
        
        status = InterruptReleaseResources(entry->Id, &entry->KernelResources);
        if (status != OS_EOK) {
            ERROR("InterruptRelease: InterruptReleaseResources id %u failed, reservation retained: %u", id, status);
            __EndInterruptUpdate(entry);
            return status;
        }
        entry->Completion = NULL;
        entry->Bound = 0;
        entry->Unbinding = 0;
    }
    
    if (!InterruptGetActiveStatus()) {
        __SynchronizeInterrupts();
    }

    if (!entry->ControllerReleased) {
        status = InterruptControllerRelease(&entry->Routing, retire);
        if (status != OS_EOK) {
            ERROR("InterruptRelease: InterruptControllerRelease id %u failed, reservation retained: %u", id, status);
            __EndInterruptUpdate(entry);
            return status;
        }
        entry->ControllerReleased = 1;
    }
    
    if (retire) {
        // Keep this slot disabled and unavailable for reuse so late messages
        // cannot reach a different handler. Replace the pointer into the group
        // with a permanent disabled flag before the group itself can be freed.
        entry->GroupEnabled = &g_retiredGroupEnabled;
        TRACE("InterruptRelease: id %u permanently retired", id);
        __EndInterruptUpdate(entry);
        return OS_EOK;
    }

    SpinlockAcquireIrq(&g_interruptTableLock);
    slot = &g_interruptTable[LOWORD(id)];
    iterator = atomic_load(&slot->Descriptor);
    while (iterator != entry) {
        previous = iterator;
        iterator = atomic_load(&iterator->Link);
    }
    if (previous == NULL) {
        atomic_store(&slot->Descriptor, atomic_load(&entry->Link));
    } else {
        atomic_store(&previous->Link, atomic_load(&entry->Link));
    }
    
    // A table walk may have loaded the old link before we removed it. Delay
    // freeing the entry until those walks finish instead of freeing it here.
    InterruptRetire(entry);
    SpinlockReleaseIrq(&g_interruptTableLock);
    
    InterruptPut(entry);
    if (!InterruptGetActiveStatus()) {
        __SynchronizeInterrupts();
    }
    TRACE("InterruptRelease: id %u released", id);
    return OS_EOK;
}

oserr_t
InterruptRelease(
    _In_ uuid_t id,
    _In_ int    retire)
{
    return __ReleaseInterrupt(id, retire, 0);
}

oserr_t
InterruptReap(
    _In_ uuid_t id,
    _In_ int    retire)
{
    // Reuse normal release's disable, handler wait, and cleanup order. The reap
    // flag permits cleanup for a dead owner and removes bindings it left behind.
    return __ReleaseInterrupt(id, retire, 1);
}

oserr_t
InterruptUnregister(
    _In_ uuid_t id)
{
    return InterruptRelease(id, 0);
}

SystemInterrupt_t*
InterruptGet(
        _In_ uuid_t Source)
{
    SystemInterrupt_t* Iterator;
    uint16_t           TableIndex = LOWORD(Source);

    if (TableIndex >= MAX_SUPPORTED_INTERRUPTS) {
        return NULL;
    }

    SpinlockAcquireIrq(&g_interruptTableLock);
    Iterator = atomic_load(&g_interruptTable[TableIndex].Descriptor);
    while (Iterator != NULL) {
        if (Iterator->Id == Source) {
            atomic_fetch_add(&Iterator->References, 1);
            SpinlockReleaseIrq(&g_interruptTableLock);
            return Iterator;
        }
        Iterator = atomic_load(&Iterator->Link);
    }
    SpinlockReleaseIrq(&g_interruptTableLock);
    return NULL;
}

void
InterruptPut(
        _In_ SystemInterrupt_t* Interrupt)
{
    if (Interrupt == NULL) {
        return;
    }

    atomic_fetch_sub(&Interrupt->References, 1);
    InterruptReclaimRetired();
}

void
InterruptSetActiveStatus(
    _In_ int Active)
{
    SystemCpuCore_t* core = CpuCoreCurrent();
    SystemCpuState_t cpuState = CpuCoreState(core);
    cpuState &= ~(CpuStateInterruptActive);
    if (Active) {
        cpuState |= CpuStateInterruptActive;
    }
    CpuCoreSetState(core, cpuState);
}

int
InterruptGetActiveStatus(void)
{
    SystemCpuState_t State = CpuCoreState(CpuCoreCurrent());
    return (State & CpuStateInterruptActive) == 0 ? 0 : 1;
}

static inline bool
__IsSystemInterruptEnabled(
    _In_ SystemInterrupt_t* interrupt)
{
    // A vector can be ready before the rest of its group. Require both flags
    // so no handler runs while group enable is only partly complete.
    if (!atomic_load(&interrupt->Enabled)) {
        return false;
    }
    if (interrupt->GroupEnabled != NULL && !atomic_load(interrupt->GroupEnabled)) {
        return false;
    }
    return true;
}

static int
__HandleInterrupt(
    _In_ int tableIndex)
{
    int                interruptSource = INTERRUPT_NONE;
    irqstatus_t        interruptStatus;
    SystemInterrupt_t* entry;

    // Count this table walk before reading pointers. Normal removal and unbinding
    // then wait for the whole walk, including completion calls, before cleanup.
    // This avoids taking the table lock on every interrupt.
    InterruptReadEnter();
    entry = atomic_load(&g_interruptTable[tableIndex].Descriptor);
    while (entry != NULL) {
        // A disabled reservation may share a line with enabled devices. Keep
        // its line available for acknowledgement, but never call its handler.
        interruptSource = entry->Routing.Source;
        if (!__IsSystemInterruptEnabled(entry)) {
            entry = atomic_load(&entry->Link);
            continue;
        }
        
        if (entry->Flags & INTERRUPT_KERNEL) {
            interruptStatus = entry->Handler(GetFastInterruptTable(), entry->Context);
        } else {
            interruptStatus = entry->KernelResources.Handler(GetFastInterruptTable(), &entry->KernelResources);
        }

        // The provider may need to acknowledge upstream hardware even when the
        // device reports no work, for example for a delayed notification. Run
        // its completion before leaving the reader count, regardless of result.
        // Only the device's result decides whether to try the next shared handler.
        InterruptCompletionInvoke(entry->Completion);

        if (interruptStatus == IRQSTATUS_HANDLED) {
            interruptSource = entry->Routing.Source;
            break;
        }
        entry = atomic_load(&entry->Link);
    }
    InterruptReadExit();
    return interruptSource;
}

void
InterruptHandleDirect(
    _In_ uint32_t tableIndex)
{
    // An upstream interrupt handler has already entered interrupt context and
    // will acknowledge its controller. Dispatch only the requested child here;
    // entering or acknowledging again would duplicate the parent's work.
    assert(InterruptGetActiveStatus());
    assert(tableIndex < MAX_SUPPORTED_INTERRUPTS);
    (void)__HandleInterrupt((int)tableIndex);
}

Context_t*
InterruptHandle(
    _In_ Context_t* context,
    _In_ int        tableIndex)
{
    uint32_t initialPriority = InterruptsGetPriority();
    int      interruptSource;

    InterruptsSetPriority(tableIndex);
    CpuCoreEnterInterrupt(context, initialPriority);
    
    interruptSource = __HandleInterrupt(tableIndex);

    // Finish the device handler and provider completion before acknowledging
    // the CPU-facing controller, which may then allow the next interrupt.
    InterruptsAcknowledge(interruptSource, tableIndex);
    return CpuCoreExitInterrupt(context, initialPriority);
}
