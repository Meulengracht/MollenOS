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
    // retired list. They are no longer dispatchable, but their storage and
    // user mapping must remain valid until the reference is released.
    retired = g_retiredInterrupts;
    g_retiredInterrupts = NULL;
    while (retired != NULL) {
        next = retired->RetiredLink;
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
    g_interruptTable[Source].Penalty++;
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
    g_interruptTable[Source].Penalty--;
    return OS_EOK;
}

int
InterruptGetPenalty(
    _In_ int Source)
{
    // Sanitize the requested source bounds
    if (Source < 0 || Source >= MAX_SUPPORTED_INTERRUPTS) {
        return INTERRUPT_NONE;
    }

    // Sanitize that source is valid
    if (g_interruptTable[Source].Sharable == 0 && g_interruptTable[Source].Penalty > 0) {
        return INTERRUPT_NONE;
    }
    return g_interruptTable[Source].Penalty;
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
    _In_ DeviceInterrupt_t* deviceInterrupt,
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
    _In_ DeviceInterrupt_t* deviceInterrupt,
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
    _In_ DeviceInterrupt_t* deviceInterrupt,
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
        return Status;
    }

    TRACE(" > remapping memory-resources");
    Status = InterruptResolveMemoryResources(deviceInterrupt, systemInterrupt);
    if (Status != OS_EOK) {
        ERROR(" > failed to remap interrupt memory resources");
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

    // Unmap and release the fast-handler that we had mapped in.
    Offset      = ((uintptr_t)resources->Handler) % GetMemorySpacePageSize();
    Length      = GetMemorySpacePageSize() + Offset;
    osStatus      = MemorySpaceUnmap(GetCurrentMemorySpace(),
        (uintptr_t)resources->Handler, Length);
    if (osStatus != OS_EOK) {
        ERROR(" > failed to cleanup interrupt handler mapping");
        return osStatus;
    }

    osStatus = InterruptCleanupIoResources(Interrupt);
    if (osStatus != OS_EOK) {
        ERROR(" > failed to cleanup interrupt io resources");
        return osStatus;
    }

    osStatus = InterruptCleanupMemoryResources(Interrupt);
    if (osStatus != OS_EOK) {
        ERROR(" > failed to cleanup interrupt memory resources");
        return osStatus;
    }
    return osStatus;
}

uuid_t
InterruptRegister(
        _In_ DeviceInterrupt_t* deviceInterrupt,
        _In_ unsigned int       flags)
{
    SystemInterrupt_t* systemInterrupt;
    uuid_t             tableIndex;
    uuid_t             id;

    if (!deviceInterrupt) {
        return OS_EINVALPARAMS;
    }

    // Reclaim entries retired by an earlier interrupt-context unregister.
    InterruptReclaimRetired();

    TRACE("InterruptRegister(Line %i Pin %i, Vector %i, Flags 0x%" PRIxIN ")",
        deviceInterrupt->Line, deviceInterrupt->Pin, deviceInterrupt->Vectors[0], flags);

    systemInterrupt = (SystemInterrupt_t*)kmalloc(sizeof(SystemInterrupt_t));
    if (!systemInterrupt) {
        return UUID_INVALID;
    }
    
    // TODO: change this to use handle system
    id = atomic_fetch_add(&g_nextInterruptId, 1);
    memset((void*)systemInterrupt, 0, sizeof(SystemInterrupt_t));
    atomic_init(&systemInterrupt->References, 0);

    systemInterrupt->Id           = (id << 16U);
    systemInterrupt->Owner        = UUID_INVALID;
    systemInterrupt->Thread       = ThreadCurrentHandle();
    systemInterrupt->Flags        = flags;
    systemInterrupt->Line         = deviceInterrupt->Line;
    systemInterrupt->Pin          = deviceInterrupt->Pin;
    systemInterrupt->AcpiConform  = deviceInterrupt->AcpiConform;

    // Get process id?
    if (!(flags & INTERRUPT_KERNEL)) {
        systemInterrupt->Owner = GetCurrentMemorySpaceHandle();
    }

    // Resolve the table index
    if (InterruptResolve(deviceInterrupt, flags, &tableIndex) != OS_EOK) {
        ERROR("Failed to resolve the interrupt, invalid flags.");
        kfree(systemInterrupt);
        return OS_EUNKNOWN;
    }

    // Update remaining members now that we resolved
    systemInterrupt->Source                         = deviceInterrupt->Line; // clear Source for software interrupts?
    systemInterrupt->Id                            |= tableIndex;
    systemInterrupt->Handler                        = deviceInterrupt->ResourceTable.Handler;
    systemInterrupt->Context                        = deviceInterrupt->Context;
    systemInterrupt->KernelResources.HandleResource = deviceInterrupt->ResourceTable.HandleResource;

    // Trace
    TRACE("Updated line %i:%i for index 0x%" PRIxIN,
          deviceInterrupt->Line, deviceInterrupt->Pin, tableIndex);

    // If it's an user interrupt, resolve resources
    if (systemInterrupt->Owner != UUID_INVALID) {
        if (InterruptResolveResources(deviceInterrupt, systemInterrupt) != OS_EOK) {
            ERROR(" > failed to resolve the requested resources");
            kfree(systemInterrupt);
            return OS_EUNKNOWN;
        }
    }
    
    // Initialize the table entry?
    SpinlockAcquireIrq(&g_interruptTableLock);

    // Check sharing while holding the same lock used to publish the entry.
    if (flags & INTERRUPT_EXCLUSIVE) {
        if (atomic_load(&g_interruptTable[tableIndex].Descriptor) != NULL) {
            ERROR(" > can't gain exclusive access as there exist interrupt for 0x%x", tableIndex);
            SpinlockReleaseIrq(&g_interruptTableLock);
            if (systemInterrupt->Owner != UUID_INVALID) {
                InterruptReleaseResources(systemInterrupt);
            }
            kfree(systemInterrupt);
            return OS_EUNKNOWN;
        }
    } else if (g_interruptTable[tableIndex].Sharable != 1 && g_interruptTable[tableIndex].Penalty > 0) {
        ERROR(" > existing interrupt has exclusive access");
        SpinlockReleaseIrq(&g_interruptTableLock);
        if (systemInterrupt->Owner != UUID_INVALID) {
            InterruptReleaseResources(systemInterrupt);
        }
        kfree(systemInterrupt);
        return OS_EUNKNOWN;
    }

    if (atomic_load(&g_interruptTable[tableIndex].Descriptor) == NULL) {
        atomic_store(&g_interruptTable[tableIndex].Descriptor, systemInterrupt);
        g_interruptTable[tableIndex].Penalty    = 1;
        g_interruptTable[tableIndex].Sharable   = (flags & INTERRUPT_EXCLUSIVE) ? 0 : 1;
    } else {
        // Insert and increase penalty
        atomic_store(&systemInterrupt->Link,
                     atomic_load(&g_interruptTable[tableIndex].Descriptor));
        atomic_store(&g_interruptTable[tableIndex].Descriptor, systemInterrupt);
        if (InterruptIncreasePenalty(tableIndex) != OS_EOK) {
            ERROR("Failed to increase penalty for source %" PRIiIN "", systemInterrupt->Source);
        }
    }

    // Enable the new interrupt
    if (InterruptConfigure(systemInterrupt, 1) != OS_EOK) {
        ERROR("Failed to enable source %" PRIiIN "", systemInterrupt->Source);
    }
    SpinlockReleaseIrq(&g_interruptTableLock);
    TRACE("Interrupt Id 0x%" PRIxIN " (Handler 0x%" PRIxIN ", Context 0x%" PRIxIN ")",
          systemInterrupt->Id, systemInterrupt->Interrupt.ResourceTable.Handler, systemInterrupt->Interrupt.Context);
    return systemInterrupt->Id;
}

oserr_t
InterruptUnregister(
        _In_ uuid_t Source)
{
    SystemInterrupt_t* Entry;
    SystemInterrupt_t* Previous   = NULL;
    uint16_t           TableIndex = LOWORD(Source);
    int                Found      = 0;

    // Sanitize parameter
    if (TableIndex >= MAX_SUPPORTED_INTERRUPTS) {
        return OS_EINVALPARAMS;
    }
    
    // Iterate handlers in that table index and unlink the given entry
    SpinlockAcquireIrq(&g_interruptTableLock);
    Entry = atomic_load(&g_interruptTable[TableIndex].Descriptor);
    while (Entry) {
        if (Entry->Id == Source) {
            if (!(Entry->Flags & INTERRUPT_KERNEL)) {
                if (Entry->Owner != GetCurrentMemorySpaceHandle()) {
                    Previous = Entry;
                    Entry    = atomic_load(&Entry->Link);
                    continue;
                }
            }

            // Marked entry as found
            Found = 1;
            if (Previous == NULL) {
                atomic_store(&g_interruptTable[TableIndex].Descriptor,
                             atomic_load(&Entry->Link));
            } else {
                atomic_store(&Previous->Link, atomic_load(&Entry->Link));
            }
            InterruptRetire(Entry);
            break;
        }
        Previous = Entry;
        Entry    = atomic_load(&Entry->Link);
    }
    SpinlockReleaseIrq(&g_interruptTableLock);

    // Sanitize if we were successfull
    if (!Found) {
        return OS_ENOENT;
    }
    
    // Decrease penalty
    if (Entry->Source != INTERRUPT_NONE) {
        InterruptDecreasePenalty(Entry->Source);
    }

    // Entry is now unlinked, clean it up mask the interrupt again if neccessary
    if (g_interruptTable[Entry->Source].Penalty == 0) {
        InterruptConfigure(Entry, 0);
    }
    
    // A caller in interrupt context cannot wait for a grace period because
    // the current handler may be the reader keeping this entry alive. Normal
    // callers wait, so returning from unregister guarantees that the entry
    // and its context are no longer in use.
    if (!InterruptGetActiveStatus()) {
        while (atomic_load(&g_interruptReaders) != 0) {
            ArchThreadYield();
        }
    }
    
    // Reclaim retired interrupt entries if we are not in an active interrupt context
    if (!InterruptGetActiveStatus()) {
        InterruptReclaimRetired();
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
