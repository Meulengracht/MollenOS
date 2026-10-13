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
            if (InterruptReleaseResources(reclaimable) != OS_EOK) {
                ERROR(" > failed to cleanup interrupt resources");
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
    int i;

    TRACE("InterruptGetLeastLoaded(Count %" PRIiIN ")", count);

    // Iterate all the available irqs
    // that the device-supports
    for (i = 0; i < count; i++) {
        if (interruptVectors[i] == INTERRUPT_NONE) {
            break;
        }

        // Calculate count
        int Penalty = InterruptGetPenalty(interruptVectors[i]);

        // Sanitize status, if -1 then its not usable
        if (Penalty == INTERRUPT_NONE) {
            continue;
        }

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
    oserr_t                   Status    = OS_EOK;

    for (int i = 0; i < INTERRUPT_MAX_IO_RESOURCES; i++) {
        if (Resources->IoResources[i] != NULL) {
            Status = ReleaseKernelSystemDeviceIo(Resources->IoResources[i]);
            if (Status != OS_EOK) {
                ERROR(" > failed to cleanup system copy of io-resource");
                break;
            }
            Resources->IoResources[i] = NULL;
        }
    }
    return Status;
}

/* InterruptResolveIoResources
 * Retrieves kernel copies of all requested io-resources, and remaps them into
 * kernel space to allow the handler to access them. */
static oserr_t
InterruptResolveIoResources(
    _In_ InterruptDescriptor_t* deviceInterrupt,
    _In_ SystemInterrupt_t* systemInterrupt)
{
    InterruptResourceTable_t* Source      = &deviceInterrupt->ResourceTable;
    InterruptResourceTable_t* Destination = &systemInterrupt->KernelResources;
    oserr_t                   Status      = OS_EOK;

    for (int i = 0; i < INTERRUPT_MAX_IO_RESOURCES; i++) {
        if (Source->IoResources[i] != NULL) {
            Status = CreateKernelSystemDeviceIo(Source->IoResources[i], &Destination->IoResources[i]);
            if (Status != OS_EOK) {
                ERROR(" > failed to create system copy of io-resource");
                break;
            }
        }
    }
    
    if (Status != OS_EOK) {
        (void)InterruptCleanupIoResources(systemInterrupt);
        return OS_EUNKNOWN;
    }
    return Status;
}

/* InterruptCleanupMemoryResources
 * Releases all memory copies of the interrupt memory resources. */
static oserr_t
InterruptCleanupMemoryResources(
    _In_ SystemInterrupt_t* Interrupt)
{
    InterruptResourceTable_t* Resources = &Interrupt->KernelResources;
    oserr_t                   Status    = OS_EOK;

    for (int i = 0; i < INTERRUPT_MAX_MEMORY_RESOURCES; i++) {
        if (Resources->MemoryResources[i].Address != 0) {
            uintptr_t Offset    = Resources->MemoryResources[i].Address % GetMemorySpacePageSize();
            size_t Length       = Resources->MemoryResources[i].Length + Offset;

            Status = MemorySpaceUnmap(GetCurrentMemorySpace(),
                Resources->MemoryResources[i].Address, Length);
            if (Status != OS_EOK) {
                ERROR(" > failed to remove interrupt resource mapping");
                break;
            }
            Resources->MemoryResources[i].Address = 0;
        }
    }
    return Status;
}

/* InterruptResolveMemoryResources
 * Retrieves kernel copies of all requested memory-resources, and remaps them into
 * kernel space to allow the handler to access them. */
static oserr_t
InterruptResolveMemoryResources(
    _In_ InterruptDescriptor_t* deviceInterrupt,
    _In_ SystemInterrupt_t* systemInterrupt)
{
    InterruptResourceTable_t* source      = &deviceInterrupt->ResourceTable;
    InterruptResourceTable_t* destination = &systemInterrupt->KernelResources;
    oserr_t                   oserr = OS_EOK;
    uintptr_t                 updatedMapping;

    for (int i = 0; i < INTERRUPT_MAX_MEMORY_RESOURCES; i++) {
        if (source->MemoryResources[i].Address != 0) {
            uintptr_t    offset         = source->MemoryResources[i].Address % GetMemorySpacePageSize();
            size_t       length         = source->MemoryResources[i].Length + offset;
            unsigned int pageFlags      = MAPPING_COMMIT | MAPPING_PERSISTENT;
            unsigned int placementFlags = MAPPING_VIRTUAL_GLOBAL | MAPPING_PHYSICAL_FIXED;
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
                ERROR(" > failed to clone interrupt resource mapping");
                break;
            }
            TRACE(" > remapped resource to 0x%" PRIxIN " from 0x%" PRIxIN "", updatedMapping + offset, source->MemoryResources[i]);
            destination->MemoryResources[i].Address = updatedMapping + offset;
            destination->MemoryResources[i].Length  = source->MemoryResources[i].Length;
            destination->MemoryResources[i].Flags   = source->MemoryResources[i].Flags;
        }
    }

    if (oserr != OS_EOK) {
        (void)InterruptCleanupMemoryResources(systemInterrupt);
        return OS_EUNKNOWN;
    }
    return oserr;
}

/**
 * Maps the neccessary fast-interrupt resources into kernel space
 * and allowing the interrupt handler to access the requested memory spaces.
 * @param deviceInterrupt
 * @param systemInterrupt
 * @return
 */
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
    uintptr_t                 Virtual;
    uintptr_t                 Offset;
    size_t                    Length;

    TRACE("InterruptResolveResources()");

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
        ERROR(" > failed to clone interrupt handler mapping");
        return Status;
    }
    Virtual += Offset;

    TRACE(" > remapped irq-handler to 0x%" PRIxIN " from 0x%" PRIxIN "", Virtual, (uintptr_t)Source->Handler);
    Destination->Handler = (InterruptHandler_t)Virtual;

    TRACE(" > remapping io-resources");

    Status = InterruptResolveIoResources(deviceInterrupt, systemInterrupt);
    if (Status != OS_EOK) {
        ERROR(" > failed to remap interrupt io resources");
        (void)InterruptReleaseResources(systemInterrupt);
        return Status;
    }

    TRACE(" > remapping memory-resources");
    Status = InterruptResolveMemoryResources(deviceInterrupt, systemInterrupt);
    if (Status != OS_EOK) {
        ERROR(" > failed to remap interrupt memory resources");
        (void)InterruptReleaseResources(systemInterrupt);
        return Status;
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
    if (osStatus == OS_EOK) {
        resources->Handler = NULL;
    }
    return osStatus;
}

/**
 * @brief Reserves a table entry before any handler is installed.
 * Keeping disabled reservations in the existing table reuses its sharing and
 * lifetime rules. Dispatch ignores these entries until Enable publishes them.
 */
oserr_t
InterruptReserve(
    _InOut_ InterruptDescriptor_t* descriptor,
    _In_    unsigned int           flags,
    _Out_   uuid_t*                idOut)
{
    SystemInterrupt_t* entry;
    InterruptTableEntry_t* slot;
    uuid_t index;
    oserr_t status;

    if (descriptor == NULL || idOut == NULL) {
        return OS_EINVALPARAMS;
    }
    *idOut = UUID_INVALID;
    InterruptReclaimRetired();
    status = InterruptResolve(descriptor, flags, &index);
    if (status != OS_EOK) {
        return status;
    }
    if (index >= MAX_SUPPORTED_INTERRUPTS) {
        return OS_EINVALPARAMS;
    }
    entry = kmalloc(sizeof(*entry));
    if (entry == NULL) {
        return OS_EOOM;
    }
    memset(entry, 0, sizeof(*entry));
    atomic_init(&entry->References, 0);
    atomic_init(&entry->Enabled, 0);
    atomic_init(&entry->Link, NULL);
    entry->Id = (atomic_fetch_add(&g_nextInterruptId, 1) << 16U) | index;
    entry->Owner = (flags & INTERRUPT_KERNEL) ? UUID_INVALID : GetCurrentMemorySpaceHandle();
    entry->Thread = ThreadCurrentHandle();
    entry->Flags = flags;
    entry->Line = descriptor->Line;
    entry->Source = descriptor->Line;
    entry->Pin = descriptor->Pin;
    entry->AcpiConform = descriptor->AcpiConform;
    entry->Context = descriptor->Context;

    SpinlockAcquireIrq(&g_interruptTableLock);
    slot = &g_interruptTable[index];
    // Retired handlers may still be running. Their reservation must also prevent
    // an exclusive claimant from taking the same line before they finish.
    if (slot->Penalty > 0 && ((flags & INTERRUPT_EXCLUSIVE) || !slot->Sharable)) {
        SpinlockReleaseIrq(&g_interruptTableLock);
        kfree(entry);
        return OS_EBUSY;
    }
    if (slot->Penalty == 0) {
        slot->Sharable = !(flags & INTERRUPT_EXCLUSIVE);
    }
    slot->Penalty++;
    entry->HasPenalty = 1;
    atomic_store(&entry->Link, atomic_load(&slot->Descriptor));
    atomic_store(&slot->Descriptor, entry);
    // Capture the result while publication is locked. Once unlocked, another
    // thread in the owning process can release this reservation by its ID.
    *idOut = entry->Id;
    SpinlockReleaseIrq(&g_interruptTableLock);
    return OS_EOK;
}

/**
 * @brief Holds one entry while an operation maps memory or waits for handlers.
 * Updating excludes other changes without keeping interrupts disabled during
 * those potentially lengthy operations. The reference prevents early freeing.
 */
static oserr_t
__BeginInterruptUpdate(
    _In_  uuid_t              id,
    _Out_ SystemInterrupt_t** entryOut)
{
    SystemInterrupt_t* entry;
    uint16_t index = LOWORD(id);

    if (id == UUID_INVALID || index >= MAX_SUPPORTED_INTERRUPTS) {
        return OS_EINVALPARAMS;
    }
    SpinlockAcquireIrq(&g_interruptTableLock);
    entry = atomic_load(&g_interruptTable[index].Descriptor);
    while (entry != NULL && entry->Id != id) {
        entry = atomic_load(&entry->Link);
    }
    if (entry == NULL || (!(entry->Flags & INTERRUPT_KERNEL) &&
                         entry->Owner != GetCurrentMemorySpaceHandle())) {
        SpinlockReleaseIrq(&g_interruptTableLock);
        return OS_ENOENT;
    }
    if (entry->Updating) {
        SpinlockReleaseIrq(&g_interruptTableLock);
        return OS_EBUSY;
    }
    entry->Updating = 1;
    atomic_fetch_add(&entry->References, 1);
    SpinlockReleaseIrq(&g_interruptTableLock);
    *entryOut = entry;
    return OS_EOK;
}

/** Releases the update claim only after all changes are visible to the next caller. */
static void
__EndInterruptUpdate(
    _In_ SystemInterrupt_t* entry)
{
    SpinlockAcquireIrq(&g_interruptTableLock);
    entry->Updating = 0;
    SpinlockReleaseIrq(&g_interruptTableLock);
    InterruptPut(entry);
}

/** Waits outside the table lock so other CPUs can complete their handlers. */
static void
__SynchronizeInterrupts(void)
{
    while (atomic_load(&g_interruptReaders) != 0) {
        ArchThreadYield();
    }
    InterruptReclaimRetired();
}

/**
 * @brief Checks memory descriptions before any mapping is created.
 * Drivers can bypass libddk and call the syscall directly, so empty ranges,
 * wrapping addresses and unknown mapping flags must also be rejected here.
 */
static oserr_t
__ValidateInterruptResources(
    _In_ InterruptResourceTable_t* resources)
{
    FastInterruptMemoryResource_t* memory;
    unsigned int i;

    if (resources == NULL || resources->Handler == NULL) {
        return OS_EINVALPARAMS;
    }
    for (i = 0; i < INTERRUPT_MAX_MEMORY_RESOURCES; i++) {
        memory = &resources->MemoryResources[i];
        if (memory->Address == 0 && memory->Length == 0 && memory->Flags == 0) {
            continue;
        }
        if (memory->Address == 0 || memory->Length == 0) {
            return OS_EINVALPARAMS;
        }
        if (memory->Length > UINTPTR_MAX - memory->Address ||
            (memory->Flags & ~INTERRUPT_RESOURCE_DISABLE_CACHE)) {
            return OS_EINVALPARAMS;
        }
    }
    return OS_EOK;
}

/**
 * @brief Maps resources into a disabled reservation without allowing delivery.
 * A temporary descriptor owns partial mappings until all of them succeed, so
 * failure leaves the reservation empty and available for a later retry.
 */
oserr_t
InterruptBind(
    _In_ uuid_t                    id,
    _In_ InterruptResourceTable_t* resources)
{
    InterruptDescriptor_t request = {0};
    SystemInterrupt_t mapped = {0};
    SystemInterrupt_t* entry;
    oserr_t status;

    if (InterruptGetActiveStatus()) {
        return OS_EINVALPARAMS;
    }
    status = __ValidateInterruptResources(resources);
    if (status != OS_EOK) {
        return status;
    }
    status = __BeginInterruptUpdate(id, &entry);
    if (status != OS_EOK) {
        return status;
    }
    if (entry->Bound || atomic_load(&entry->Enabled)) {
        __EndInterruptUpdate(entry);
        return OS_EBUSY;
    }
    request.ResourceTable = *resources;
    mapped.KernelResources.HandleResource = resources->HandleResource;
    if (!(entry->Flags & INTERRUPT_KERNEL)) {
        status = InterruptResolveResources(&request, &mapped);
    }
    if (status == OS_EOK) {
        entry->KernelResources = mapped.KernelResources;
        entry->Handler = resources->Handler;
        entry->Bound = 1;
    } else if (mapped.KernelResources.Handler != NULL) {
        // Mapping failed and cleanup also failed. Preserve the remaining
        // mappings in the reservation so Unbind can retry instead of leaking.
        entry->KernelResources = mapped.KernelResources;
        entry->Bound = 1;
        entry->Unbinding = 1;
    }
    __EndInterruptUpdate(entry);
    return status;
}

/**
 * @brief Changes line delivery while the table lock protects the enabled count.
 * Only the first enabled user unmasks a shared line; only the last masks it.
 * A failed hardware operation leaves the previous delivery state intact.
 */
static oserr_t
__SetInterruptEnabledLocked(
    _In_ SystemInterrupt_t* entry,
    _In_ int                enable)
{
    InterruptTableEntry_t* slot = &g_interruptTable[LOWORD(entry->Id)];
    oserr_t status;

    if (atomic_load(&entry->Enabled) == enable) {
        return OS_EOK;
    }
    if (enable) {
        if (!entry->Bound || entry->Unbinding) {
            return OS_EINVALPARAMS;
        }
        // Publish the fully mapped handler before hardware can deliver to it.
        atomic_store(&entry->Enabled, 1);
        if (slot->EnabledCount == 0) {
            status = InterruptConfigure(entry, 1);
            if (status != OS_EOK) {
                atomic_store(&entry->Enabled, 0);
                return status;
            }
        }
        slot->EnabledCount++;
    } else {
        if (slot->EnabledCount == 1) {
            status = InterruptConfigure(entry, 0);
            if (status != OS_EOK) {
                return status;
            }
        }
        atomic_store(&entry->Enabled, 0);
        slot->EnabledCount--;
    }
    return OS_EOK;
}

/**
 * @brief Enables a bound entry or synchronously stops its handler.
 * Waiting after disabling also covers a handler which observed Enabled before
 * it was cleared. Resources remain bound until Unbind explicitly releases them.
 */
oserr_t
InterruptSetEnabled(
    _In_ uuid_t id,
    _In_ int    enable)
{
    SystemInterrupt_t* entry;
    oserr_t status;

    if ((enable != 0 && enable != 1) || InterruptGetActiveStatus()) {
        return OS_EINVALPARAMS;
    }
    status = __BeginInterruptUpdate(id, &entry);
    if (status != OS_EOK) {
        return status;
    }
    SpinlockAcquireIrq(&g_interruptTableLock);
    status = __SetInterruptEnabledLocked(entry, enable);
    SpinlockReleaseIrq(&g_interruptTableLock);
    if (!enable || status != OS_EOK) {
        __SynchronizeInterrupts();
    }
    __EndInterruptUpdate(entry);
    return status;
}

/**
 * @brief Releases mappings while retaining the disabled line reservation.
 * Enabled handlers are rejected so callers cannot remove memory still in use.
 */
oserr_t
InterruptUnbind(
    _In_ uuid_t id)
{
    SystemInterrupt_t* entry;
    oserr_t status;

    if (InterruptGetActiveStatus()) {
        return OS_EINVALPARAMS;
    }
    status = __BeginInterruptUpdate(id, &entry);
    if (status != OS_EOK) {
        return status;
    }
    if (atomic_load(&entry->Enabled)) {
        __EndInterruptUpdate(entry);
        return OS_EBUSY;
    }
    __SynchronizeInterrupts();
    if (entry->Bound && !(entry->Flags & INTERRUPT_KERNEL)) {
        // A failed unmap can leave only part of the table valid. Keep cleanup
        // retryable, but reject Enable until every mapping has been released.
        entry->Unbinding = 1;
        status = InterruptReleaseResources(entry);
    }
    if (status == OS_EOK) {
        memset(&entry->KernelResources, 0, sizeof(entry->KernelResources));
        entry->Handler = NULL;
        entry->Bound = 0;
        entry->Unbinding = 0;
    }
    __EndInterruptUpdate(entry);
    return status;
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
    uuid_t id;
    oserr_t status;

    status = InterruptReserve(descriptor, flags, &id);
    if (status != OS_EOK) {
        return UUID_INVALID;
    }
    status = InterruptBind(id, &descriptor->ResourceTable);
    if (status == OS_EOK) {
        status = InterruptSetEnabled(id, 1);
    }
    if (status != OS_EOK) {
        InterruptUnregister(id);
        return UUID_INVALID;
    }
    return id;
}

/**
 * @brief Releases one reservation without disturbing other enabled users.
 * Entries removed by a handler remain on the retired list until dispatch exits;
 * normal callers wait before returning so their backing resources can be freed.
 */
oserr_t
InterruptUnregister(
    _In_ uuid_t id)
{
    SystemInterrupt_t* entry;
    SystemInterrupt_t* previous = NULL;
    SystemInterrupt_t* iterator;
    InterruptTableEntry_t* slot;
    oserr_t status;

    status = __BeginInterruptUpdate(id, &entry);
    if (status != OS_EOK) {
        return status;
    }
    if (InterruptGetActiveStatus() && !(entry->Flags & INTERRUPT_KERNEL)) {
        // User mappings require ordinary execution context for cleanup. Kernel
        // sources can still retire themselves from their own interrupt handler.
        __EndInterruptUpdate(entry);
        return OS_EBUSY;
    }
    SpinlockAcquireIrq(&g_interruptTableLock);
    status = __SetInterruptEnabledLocked(entry, 0);
    if (status != OS_EOK) {
        SpinlockReleaseIrq(&g_interruptTableLock);
        __EndInterruptUpdate(entry);
        return status;
    }
    SpinlockReleaseIrq(&g_interruptTableLock);
    if (entry->Bound && !(entry->Flags & INTERRUPT_KERNEL)) {
        // Keep the disabled reservation reachable until cleanup succeeds. A
        // failed unmap must not turn into an unreachable allocation or mapping.
        __SynchronizeInterrupts();
        entry->Unbinding = 1;
        status = InterruptReleaseResources(entry);
        if (status != OS_EOK) {
            __EndInterruptUpdate(entry);
            return status;
        }
        entry->Bound = 0;
        entry->Unbinding = 0;
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
    return OS_EOK;
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

Context_t*
InterruptHandle(
    _In_  Context_t* context,
    _In_  int        tableIndex)
{
    uint32_t           initialPriority = InterruptsGetPriority();
    int                interruptSource = INTERRUPT_NONE;
    irqstatus_t        interruptStatus;
    SystemInterrupt_t* entry;

    InterruptsSetPriority(tableIndex);
    CpuCoreEnterInterrupt(context, initialPriority);

    // The interrupt path deliberately does not acquire the table lock.
    // Entering the RCU read-side critical section before loading Descriptor
    // keeps every descriptor, handler, and context used below alive until
    // the complete dispatch walk has finished.
    InterruptReadEnter();
    entry = atomic_load(&g_interruptTable[tableIndex].Descriptor);
    while (entry != NULL) {
        // A disabled reservation may share a line with enabled devices. Keep
        // its line available for acknowledgement, but never call its handler.
        interruptSource = entry->Source;
        if (!atomic_load(&entry->Enabled)) {
            entry = atomic_load(&entry->Link);
            continue;
        }
        if (entry->Flags & INTERRUPT_KERNEL) {
            interruptStatus = entry->Handler(GetFastInterruptTable(), entry->Context);
        } else {
            interruptStatus = entry->KernelResources.Handler(GetFastInterruptTable(), &entry->KernelResources);
        }

        if (interruptStatus == IRQSTATUS_HANDLED) {
            interruptSource = entry->Source;
            break;
        }
        entry = atomic_load(&entry->Link);
    }
    InterruptReadExit();
    
    InterruptsAcknowledge(interruptSource, tableIndex);
    return CpuCoreExitInterrupt(context, initialPriority);
}
