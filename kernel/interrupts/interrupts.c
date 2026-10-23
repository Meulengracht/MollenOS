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
#include <memoryspace.h>
#include <spinlock.h>
#include <interrupts.h>
#include <threading.h>
#include <string.h>

static oserr_t
InterruptReleaseResources(
        _In_ SystemInterrupt_t* Interrupt);

// A dispatch chain and its shared reservation/delivery accounting.
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
 * Interrupt dispatch uses a small RCU read-side critical section. Readers
 * only update this counter and never wait for a lock. Unregistered entries
 * are placed on the retired list and reclaimed after all readers have left.
 */
static _Atomic(unsigned int) g_interruptReaders = 0;
static SystemInterrupt_t*    g_retiredInterrupts;
static _Atomic(int)          g_retiredGroupEnabled;

static void
InterruptReadEnter(void)
{
    atomic_fetch_add(&g_interruptReaders, 1);
}

static void
InterruptReadExit(void)
{
    atomic_fetch_sub(&g_interruptReaders, 1);
}

static void
InterruptRetire(
        _In_ SystemInterrupt_t* Entry)
{
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
        // The grace period has elapsed, so the table index may be handed out again.
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
            status = InterruptReleaseResources(reclaimable);
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

/* InterruptCleanupIoResources
 * Releases all kernel copies of the io-resources. */
static oserr_t
InterruptCleanupIoResources(
    _In_ SystemInterrupt_t* Interrupt)
{
    InterruptResourceTable_t* Resources = &Interrupt->KernelResources;
    oserr_t                   Status;
    int                       i;

    TRACE("InterruptCleanupIoResources(id=%u)", Interrupt->Id);
    for (i = 0; i < INTERRUPT_MAX_IO_RESOURCES; i++) {
        if (Resources->IoResources[i] != NULL) {
            Status = ReleaseKernelSystemDeviceIo(Resources->IoResources[i]);
            if (Status != OS_EOK) {
                ERROR("InterruptCleanupIoResources: ReleaseKernelSystemDeviceIo id %u, resource %d failed: %u",
                      Interrupt->Id, i, Status);
                return Status;
            }
            Resources->IoResources[i] = NULL;
        }
    }
    return OS_EOK;
}

/* InterruptResolveIoResources
 * Retrieves kernel copies of all requested io-resources, and remaps them into
 * kernel space to allow the handler to access them. */
static oserr_t
InterruptResolveIoResources(
    _In_ InterruptDescriptor_t* deviceInterrupt,
    _In_ SystemInterrupt_t*     systemInterrupt)
{
    InterruptResourceTable_t* Source      = &deviceInterrupt->ResourceTable;
    InterruptResourceTable_t* Destination = &systemInterrupt->KernelResources;
    oserr_t                   Status;
    oserr_t                   cleanupStatus;
    int                       i;

    TRACE("InterruptResolveIoResources(id=%u)", systemInterrupt->Id);
    for (i = 0; i < INTERRUPT_MAX_IO_RESOURCES; i++) {
        if (Source->IoResources[i] != NULL) {
            Status = CreateKernelSystemDeviceIo(Source->IoResources[i], &Destination->IoResources[i]);
            if (Status != OS_EOK) {
                ERROR("InterruptResolveIoResources: CreateKernelSystemDeviceIo id %u, resource %d failed: %u",
                      systemInterrupt->Id, i, Status);
                goto error;
            }
        }
    }
    
    return OS_EOK;

error:
    cleanupStatus = InterruptCleanupIoResources(systemInterrupt);
    if (cleanupStatus != OS_EOK) {
        ERROR("InterruptResolveIoResources: InterruptCleanupIoResources id %u failed: %u",
              systemInterrupt->Id, cleanupStatus);
    }
    return OS_EUNKNOWN;
}

/* InterruptCleanupMemoryResources
 * Releases all memory copies of the interrupt memory resources. */
static oserr_t
InterruptCleanupMemoryResources(
    _In_ SystemInterrupt_t* Interrupt)
{
    InterruptResourceTable_t* Resources = &Interrupt->KernelResources;
    oserr_t                   Status;
    uintptr_t                 Offset;
    size_t                    Length;
    int                       i;

    TRACE("InterruptCleanupMemoryResources(id=%u)", Interrupt->Id);
    for (i = 0; i < INTERRUPT_MAX_MEMORY_RESOURCES; i++) {
        if (Resources->MemoryResources[i].Address != 0) {
            Offset = Resources->MemoryResources[i].Address % GetMemorySpacePageSize();
            Length = Resources->MemoryResources[i].Length + Offset;

            Status = MemorySpaceUnmap(GetCurrentMemorySpace(),
                Resources->MemoryResources[i].Address, Length);
            if (Status != OS_EOK) {
                ERROR("InterruptCleanupMemoryResources: MemorySpaceUnmap id %u, resource %d, address 0x%" PRIxIN ", length %zu failed: %u",
                      Interrupt->Id, i, Resources->MemoryResources[i].Address, Length, Status);
                return Status;
            }
            Resources->MemoryResources[i].Address = 0;
        }
    }
    return OS_EOK;
}

/* InterruptResolveMemoryResources
 * Retrieves kernel copies of all requested memory-resources, and remaps them into
 * kernel space to allow the handler to access them. */
static oserr_t
InterruptResolveMemoryResources(
    _In_ InterruptDescriptor_t* deviceInterrupt,
    _In_ SystemInterrupt_t*     systemInterrupt)
{
    InterruptResourceTable_t* source      = &deviceInterrupt->ResourceTable;
    InterruptResourceTable_t* destination = &systemInterrupt->KernelResources;
    oserr_t                   oserr;
    oserr_t                   cleanupStatus;
    uintptr_t                 updatedMapping;
    uintptr_t                 offset;
    size_t                    length;
    unsigned int              pageFlags;
    unsigned int              placementFlags;
    int                       i;

    TRACE("InterruptResolveMemoryResources(id=%u)", systemInterrupt->Id);
    for (i = 0; i < INTERRUPT_MAX_MEMORY_RESOURCES; i++) {
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
                      systemInterrupt->Id, i, source->MemoryResources[i].Address, length, oserr);
                goto error;
            }
            TRACE("InterruptResolveMemoryResources: id %u, resource %d remapped from 0x%" PRIxIN " to 0x%" PRIxIN,
                  systemInterrupt->Id, i, source->MemoryResources[i].Address, updatedMapping + offset);
            destination->MemoryResources[i].Address = updatedMapping + offset;
            destination->MemoryResources[i].Length  = source->MemoryResources[i].Length;
            destination->MemoryResources[i].Flags   = source->MemoryResources[i].Flags;
        }
    }

    return OS_EOK;

error:
    cleanupStatus = InterruptCleanupMemoryResources(systemInterrupt);
    if (cleanupStatus != OS_EOK) {
        ERROR("InterruptResolveMemoryResources: InterruptCleanupMemoryResources id %u failed: %u",
              systemInterrupt->Id, cleanupStatus);
    }
    return OS_EUNKNOWN;
}

static oserr_t
InterruptResolveResources(
    _In_ InterruptDescriptor_t* deviceInterrupt,
    _In_ SystemInterrupt_t* systemInterrupt)
{
    InterruptResourceTable_t* Source      = &deviceInterrupt->ResourceTable;
    InterruptResourceTable_t* Destination = &systemInterrupt->KernelResources;
    unsigned int              PlacementFlags;
    unsigned int              PageFlags;
    oserr_t                   Status;
    oserr_t                   cleanupStatus;
    uintptr_t                 Virtual;
    uintptr_t                 Offset;
    size_t                    Length;

    TRACE("InterruptResolveResources(id=%u, handler=0x%" PRIxIN ")",
          systemInterrupt->Id, (uintptr_t)Source->Handler);

    // Calculate metrics we need to create the mappings
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
    Destination->Handler = (InterruptHandler_t)Virtual;

    Status = InterruptResolveIoResources(deviceInterrupt, systemInterrupt);
    if (Status != OS_EOK) {
        ERROR("InterruptResolveResources: InterruptResolveIoResources id %u failed: %u",
              systemInterrupt->Id, Status);
        goto error;
    }

    Status = InterruptResolveMemoryResources(deviceInterrupt, systemInterrupt);
    if (Status != OS_EOK) {
        ERROR("InterruptResolveResources: InterruptResolveMemoryResources id %u failed: %u",
              systemInterrupt->Id, Status);
        goto error;
    }
    return OS_EOK;

error:
    cleanupStatus = InterruptReleaseResources(systemInterrupt);
    if (cleanupStatus != OS_EOK) {
        ERROR("InterruptResolveResources: InterruptReleaseResources id %u failed: %u",
              systemInterrupt->Id, cleanupStatus);
    }
    return Status;
}

/* InterruptReleaseResources
 * Releases previously allocated resources for the system interrupt. */
static oserr_t
InterruptReleaseResources(
    _In_ SystemInterrupt_t* Interrupt)
{
    InterruptResourceTable_t* resources = &Interrupt->KernelResources;
    oserr_t                   osStatus;
    uintptr_t                 Offset;
    size_t                    Length;

    TRACE("InterruptReleaseResources(id=%u)", Interrupt->Id);
    // Sanitize a handler is present, if not, no resources are present
    if ((uintptr_t)resources->Handler == 0) {
        return OS_EOK;
    }

    // Release data mappings first and the handler last. Keeping Handler set
    // until the end lets a failed cleanup retry the remaining resources.
    osStatus = InterruptCleanupIoResources(Interrupt);
    if (osStatus != OS_EOK) {
        return osStatus;
    }
    
    osStatus = InterruptCleanupMemoryResources(Interrupt);
    if (osStatus != OS_EOK) {
        return osStatus;
    }
    
    Offset = ((uintptr_t)resources->Handler) % GetMemorySpacePageSize();
    Length = GetMemorySpacePageSize() + Offset;
    
    osStatus = MemorySpaceUnmap(GetCurrentMemorySpace(),
        (uintptr_t)resources->Handler, Length);
    if (osStatus != OS_EOK) {
        ERROR("InterruptReleaseResources: MemorySpaceUnmap handler id %u, address 0x%" PRIxIN ", length %zu failed: %u",
              Interrupt->Id, (uintptr_t)resources->Handler, Length, osStatus);
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
    entry->Owner = (flags & INTERRUPT_KERNEL) ? UUID_INVALID : GetCurrentMemorySpaceHandle();
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

// Ensures an interrupt can only make locked updates. This returns
// an reference to a registered interrupt
static oserr_t
__BeginInterruptUpdate(
    _In_  uuid_t              id,
    _Out_ SystemInterrupt_t** entryOut)
{
    SystemInterrupt_t* entry;
    uint16_t           index = LOWORD(id);

    if (id == UUID_INVALID || index >= MAX_SUPPORTED_INTERRUPTS) {
        WARNING("__BeginInterruptUpdate: invalid interrupt id %u", id);
        return OS_EINVALPARAMS;
    }

    SpinlockAcquireIrq(&g_interruptTableLock);

    // Find the entry in the list of the interrupt table
    entry = atomic_load(&g_interruptTable[index].Descriptor);
    while (entry != NULL && entry->Id != id) {
        entry = atomic_load(&entry->Link);
    }
    
    // If we didn't find the entry, or the rights were not correct
    // then we return bad result
    if (entry == NULL || (!(entry->Flags & INTERRUPT_KERNEL) &&
                         entry->Owner != GetCurrentMemorySpaceHandle())) {
        SpinlockReleaseIrq(&g_interruptTableLock);
        TRACE("__BeginInterruptUpdate: id %u not found for the current owner", id);
        return OS_ENOENT;
    }
    
    // If the entry is in the process of updating, try again later
    if (entry->Updating) {
        SpinlockReleaseIrq(&g_interruptTableLock);
        TRACE("__BeginInterruptUpdate: id %u is already being updated", id);
        return OS_EBUSY;
    }
    
    entry->Updating = 1;
    atomic_fetch_add(&entry->References, 1);
    SpinlockReleaseIrq(&g_interruptTableLock);
    
    *entryOut = entry;
    return OS_EOK;
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
    while (atomic_load(&g_interruptReaders) != 0) {
        ArchThreadYield();
    }
    InterruptReclaimRetired();
}

static oserr_t
__ValidateInterruptResources(
    _In_ InterruptResourceTable_t* resources)
{
    FastInterruptMemoryResource_t* memory;
    unsigned int                   i;

    if (resources == NULL || resources->Handler == NULL) {
        WARNING("__ValidateInterruptResources: missing resource table or handler");
        return OS_EINVALPARAMS;
    }
    
    for (i = 0; i < INTERRUPT_MAX_MEMORY_RESOURCES; i++) {
        memory = &resources->MemoryResources[i];
        if (memory->Address == 0 && memory->Length == 0 && memory->Flags == 0) {
            continue;
        }
        if (memory->Address == 0 || memory->Length == 0) {
            WARNING("__ValidateInterruptResources: resource %u has address 0x%" PRIxIN ", length %zu",
                    i, memory->Address, memory->Length);
            return OS_EINVALPARAMS;
        }
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
    InterruptDescriptor_t request = { 0 };
    SystemInterrupt_t     mapped = { 0 };
    SystemInterrupt_t*    entry;
    oserr_t               status;

    TRACE("InterruptBind(id=%u, resources=%p)", id, resources);
    if (InterruptGetActiveStatus()) {
        WARNING("InterruptBind: id %u cannot bind in interrupt context", id);
        return OS_EINVALPARAMS;
    }

    // Interrupt syscalls can be invoked outside libddk (but hopefully not)
    // so we also must validate certain resource aspects here
    status = __ValidateInterruptResources(resources);
    if (status != OS_EOK) {
        return status;
    }
    
    // Do a locked update of the reserved interrupt
    status = __BeginInterruptUpdate(id, &entry);
    if (status != OS_EOK) {
        return status;
    }
    
    if (entry->Bound || atomic_load(&entry->Enabled) || entry->ControllerReleased) {
        WARNING("InterruptBind: id %u is already bound, enabled or permanently retired", id);
        // always release the reference
        __EndInterruptUpdate(entry);
        return OS_EBUSY;
    }
    
    request.ResourceTable = *resources;
    mapped.Id = id;
    mapped.KernelResources.HandleResource = resources->HandleResource;
    
    if (!(entry->Flags & INTERRUPT_KERNEL)) {
        status = InterruptResolveResources(&request, &mapped);
        if (status != OS_EOK) {
            ERROR("InterruptBind: InterruptResolveResources id %u failed: %u", id, status);
            if (mapped.KernelResources.Handler != NULL) {
                // Preserve partial mappings so Unbind can retry their cleanup.
                entry->KernelResources = mapped.KernelResources;
                entry->Bound = 1;
                entry->Unbinding = 1;
                WARNING("InterruptBind: id %u retains mappings for cleanup retry", id);
            }
            __EndInterruptUpdate(entry);
            return status;
        }
    }
    
    entry->KernelResources = mapped.KernelResources;
    entry->Handler = resources->Handler;
    entry->Bound = 1;

    // Release the reference, so we publish the changes
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

        // Publish the fully mapped handler before hardware can deliver to it.
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
    status = __BeginInterruptUpdate(id, &entry);
    if (status != OS_EOK) {
        return status;
    }
    
    // Update the interrupt state of the line
    SpinlockAcquireIrq(&g_interruptTableLock);
    status = __SetInterruptEnabledLocked(entry, enable);
    SpinlockReleaseIrq(&g_interruptTableLock);
    if (!enable || status != OS_EOK) {
        // When disabling, ensure we synchronize the state
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

    status = __BeginInterruptUpdate(id, &entry);
    if (status != OS_EOK) {
        return status;
    }
    
    if (atomic_load(&entry->Enabled)) {
        WARNING("InterruptUnbind: id %u is still enabled", id);
        __EndInterruptUpdate(entry);
        return OS_EBUSY;
    }
    
    __SynchronizeInterrupts();
    if (entry->Bound && !(entry->Flags & INTERRUPT_KERNEL)) {
        // A failed unmap can leave only part of the table valid. Keep cleanup
        // retryable, but reject Enable until every mapping has been released.
        entry->Unbinding = 1;
        status = InterruptReleaseResources(entry);
        if (status != OS_EOK) {
            ERROR("InterruptUnbind: InterruptReleaseResources id %u failed, cleanup remains retryable: %u",
                  id, status);
            __EndInterruptUpdate(entry);
            return status;
        }
    }
    memset(&entry->KernelResources, 0, sizeof(entry->KernelResources));
    entry->Handler = NULL;
    entry->Bound = 0;
    entry->Unbinding = 0;
    __EndInterruptUpdate(entry);
    return OS_EOK;
}

/**
 * @brief Installs a kernel source using the same reservation and binding steps.
 * Boot timers and CPU messages already supply complete descriptors, so this
 * helper performs the sequence for them and returns their interrupt identity.
 */
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
oserr_t
InterruptRelease(
    _In_ uuid_t id,
    _In_ int    retire)
{
    SystemInterrupt_t*     entry;
    SystemInterrupt_t*     previous = NULL;
    SystemInterrupt_t*     iterator;
    InterruptTableEntry_t* slot;
    oserr_t                status;

    TRACE("InterruptRelease(id=%u, retire=%d)", id, retire);
    status = __BeginInterruptUpdate(id, &entry);
    if (status != OS_EOK) {
        return status;
    }

    if (retire && (!(entry->Flags & INTERRUPT_MSI) || entry->Bound || atomic_load(&entry->Enabled))) {
        WARNING("InterruptRelease: id %u cannot retire a non-message, bound or enabled reservation", id);
        __EndInterruptUpdate(entry);
        return OS_EINVALPARAMS;
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
        
        status = InterruptReleaseResources(entry);
        if (status != OS_EOK) {
            ERROR("InterruptRelease: InterruptReleaseResources id %u failed, reservation retained: %u", id, status);
            __EndInterruptUpdate(entry);
            return status;
        }
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
        // The group can now be removed. Keep the disabled handler-table slot
        // reserved so a late device message cannot reach another handler.
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

    // The interrupt path deliberately does not acquire the table lock.
    // Entering the RCU read-side critical section before loading Descriptor
    // keeps every descriptor, handler, and context used below alive until
    // the complete dispatch walk has finished.
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
    // The parent owns CPU entry/exit and hardware acknowledgement. Its read
    // section also keeps child reservations alive across a pending snapshot.
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
    InterruptsAcknowledge(interruptSource, tableIndex);
    return CpuCoreExitInterrupt(context, initialPriority);
}
