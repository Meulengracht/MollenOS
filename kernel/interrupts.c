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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Interrupt Interface
 * - Contains the shared kernel interrupt interface
 *   that is generic and can be shared/used by all systems
 */

#define __MODULE "INIF"
//#define __TRACE

#include <arch.h>
#include <arch/interrupts.h>
#include <arch/utils.h>
#include <assert.h>
#include <component/cpu.h>
#include <ddk/interrupt.h>
#include <deviceio.h>
#include <debug.h>
#include <heap.h>
#include <modules/manager.h>
#include <memoryspace.h>
#include <irq_spinlock.h>
#include <interrupts.h>
#include <threading.h>
#include <string.h>

typedef struct InterruptTableEntry {
    SystemInterrupt_t* Descriptor;
    int                Penalty;
    int                Sharable;
} InterruptTableEntry_t;

static InterruptTableEntry_t InterruptTable[MAX_SUPPORTED_INTERRUPTS] = { { 0 } };
static IrqSpinlock_t         InterruptTableSyncObject = OS_IRQ_SPINLOCK_INIT;
static _Atomic(UUId_t)       InterruptIdGenerator     = ATOMIC_VAR_INIT(0);

OsStatus_t
InterruptIncreasePenalty(
    _In_ int Source)
{
    // Sanitize the requested source bounds
    if (Source < 0 || Source >= MAX_SUPPORTED_INTERRUPTS) {
        return INTERRUPT_NONE;
    }
    InterruptTable[Source].Penalty++;
    return OsSuccess;
}

OsStatus_t
InterruptDecreasePenalty(
    _In_ int Source)
{
    // Sanitize the requested source bounds
    if (Source < 0 || Source >= MAX_SUPPORTED_INTERRUPTS) {
        return INTERRUPT_NONE;
    }
    InterruptTable[Source].Penalty--;
    return OsSuccess;
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
    if (InterruptTable[Source].Sharable == 0
        && InterruptTable[Source].Penalty > 0) {
        return INTERRUPT_NONE;
    }
    return InterruptTable[Source].Penalty;
}

int
InterruptGetLeastLoaded(
    _In_ int Irqs[],
    _In_ int Count)
{
    int SelectedPenality = INTERRUPT_NONE;
    int SelectedIrq      = INTERRUPT_NONE;
    int i;

    TRACE("InterruptGetLeastLoaded(Count %" PRIiIN ")", Count);

    // Iterate all the available irqs
    // that the device-supports
    for (i = 0; i < Count; i++) {
        if (Irqs[i] == INTERRUPT_NONE) {
            break;
        }

        // Calculate count
        int Penalty = InterruptGetPenalty(Irqs[i]);

        // Sanitize status, if -1 then its not usable
        if (Penalty == INTERRUPT_NONE) {
            continue;
        }

        // Store the lowest penalty
        if (SelectedIrq == INTERRUPT_NONE || Penalty < SelectedPenality) {
            SelectedIrq = Irqs[i];
            SelectedPenality = Penalty;
        }
    }
    return SelectedIrq;
}

/* InterruptCleanupIoResources
 * Releases all kernel copies of the io-resources. */
static OsStatus_t
InterruptCleanupIoResources(
    _In_ SystemInterrupt_t* Interrupt)
{
    InterruptResourceTable_t* Resources = &Interrupt->KernelResources;
    OsStatus_t                Status    = OsSuccess;

    for (int i = 0; i < INTERRUPT_MAX_IO_RESOURCES; i++) {
        if (Resources->IoResources[i] != NULL) {
            Status = ReleaseKernelSystemDeviceIo(Resources->IoResources[i]);
            if (Status != OsSuccess) {
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
static OsStatus_t
InterruptResolveIoResources(
    _In_ SystemInterrupt_t* Interrupt)
{
    InterruptResourceTable_t* Source      = &Interrupt->Interrupt.ResourceTable;
    InterruptResourceTable_t* Destination = &Interrupt->KernelResources;
    OsStatus_t                Status      = OsSuccess;

    for (int i = 0; i < INTERRUPT_MAX_IO_RESOURCES; i++) {
        if (Source->IoResources[i] != NULL) {
            Status = CreateKernelSystemDeviceIo(Source->IoResources[i], &Destination->IoResources[i]);
            if (Status != OsSuccess) {
                ERROR(" > failed to create system copy of io-resource");
                break;
            }
        }
    }
    
    if (Status != OsSuccess) {
        Status = InterruptCleanupIoResources(Interrupt);
        return OsError;
    }
    return Status;
}

/* InterruptCleanupMemoryResources
 * Releases all memory copies of the interrupt memory resources. */
static OsStatus_t
InterruptCleanupMemoryResources(
    _In_ SystemInterrupt_t* Interrupt)
{
    InterruptResourceTable_t * Resources = &Interrupt->KernelResources;
    OsStatus_t Status                       = OsSuccess;

    for (int i = 0; i < INTERRUPT_MAX_MEMORY_RESOURCES; i++) {
        if (Resources->MemoryResources[i].Address != 0) {
            uintptr_t Offset    = Resources->MemoryResources[i].Address % GetMemorySpacePageSize();
            size_t Length       = Resources->MemoryResources[i].Length + Offset;

            Status = MemorySpaceUnmap(GetCurrentMemorySpace(),
                Resources->MemoryResources[i].Address, Length);
            if (Status != OsSuccess) {
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
static OsStatus_t
InterruptResolveMemoryResources(
    _In_ SystemInterrupt_t* Interrupt)
{
    InterruptResourceTable_t* Source      = &Interrupt->Interrupt.ResourceTable;
    InterruptResourceTable_t* Destination = &Interrupt->KernelResources;
    OsStatus_t                Status      = OsSuccess;
    uintptr_t                 UpdatedMapping;

    for (int i = 0; i < INTERRUPT_MAX_MEMORY_RESOURCES; i++) {
        if (Source->MemoryResources[i].Address != 0) {
            uintptr_t Offset       = Source->MemoryResources[i].Address % GetMemorySpacePageSize();
            size_t Length          = Source->MemoryResources[i].Length + Offset;
            unsigned int PageFlags      = MAPPING_COMMIT | MAPPING_PERSISTENT;
            unsigned int PlacementFlags = MAPPING_VIRTUAL_GLOBAL | MAPPING_PHYSICAL_FIXED;
            if (Source->MemoryResources[i].Flags & INTERRUPT_RESOURCE_DISABLE_CACHE) {
                PageFlags |= MAPPING_NOCACHE;
            }

            Status = CloneMemorySpaceMapping(GetCurrentMemorySpace(), GetCurrentMemorySpace(),
                Source->MemoryResources[i].Address, &UpdatedMapping, Length, PageFlags, PlacementFlags);
            if (Status != OsSuccess) {
                ERROR(" > failed to clone interrupt resource mapping");
                break;
            }
            TRACE(" > remapped resource to 0x%" PRIxIN " from 0x%" PRIxIN "", UpdatedMapping + Offset, Source->MemoryResources[i]);
            Destination->MemoryResources[i].Address = UpdatedMapping + Offset;
            Destination->MemoryResources[i].Length  = Source->MemoryResources[i].Length;
            Destination->MemoryResources[i].Flags   = Source->MemoryResources[i].Flags;
        }
    }

    if (Status != OsSuccess) {
        Status = InterruptCleanupMemoryResources(Interrupt);
        return OsError;
    }
    return Status;
}

/* InterruptResolveResources
 * Maps the neccessary fast-interrupt resources into kernel space
 * and allowing the interrupt handler to access the requested memory spaces. */
static OsStatus_t
InterruptResolveResources(
    _In_ SystemInterrupt_t* Interrupt)
{
    InterruptResourceTable_t * Source      = &Interrupt->Interrupt.ResourceTable;
    InterruptResourceTable_t * Destination = &Interrupt->KernelResources;
    unsigned int               PlacementFlags;
    unsigned int               PageFlags;
    OsStatus_t                 Status;
    uintptr_t                  Virtual;
    uintptr_t                  Offset;
    size_t                     Length;

    TRACE("InterruptResolveResources()");

    // Calculate metrics we need to create the mappings
    Offset         = ((uintptr_t)Source->Handler) % GetMemorySpacePageSize();
    Length         = GetMemorySpacePageSize() + Offset;
    PageFlags      = MAPPING_COMMIT | MAPPING_EXECUTABLE | MAPPING_READONLY;
    PlacementFlags = MAPPING_VIRTUAL_GLOBAL;
    Status         = CloneMemorySpaceMapping(GetCurrentMemorySpace(), GetCurrentMemorySpace(),
        (VirtualAddress_t)Source->Handler, &Virtual, Length, PageFlags, PlacementFlags);
    if (Status != OsSuccess) {
        ERROR(" > failed to clone interrupt handler mapping");
        return OsError;
    }
    Virtual += Offset;

    TRACE(" > remapped irq-handler to 0x%" PRIxIN " from 0x%" PRIxIN "", Virtual, (uintptr_t)Source->Handler);
    Destination->Handler = (InterruptHandler_t)Virtual;

    TRACE(" > remapping io-resources");
    if (InterruptResolveIoResources(Interrupt) != OsSuccess) {
        ERROR(" > failed to remap interrupt io resources");
        return OsError;
    }

    TRACE(" > remapping memory-resources");
    if (InterruptResolveMemoryResources(Interrupt) != OsSuccess) {
        ERROR(" > failed to remap interrupt memory resources");
        return OsError;
    }
    return OsSuccess;
}

/* InterruptReleaseResources
 * Releases previously allocated resources for the system interrupt. */
static OsStatus_t
InterruptReleaseResources(
    _In_ SystemInterrupt_t* Interrupt)
{
    InterruptResourceTable_t * Resources = &Interrupt->KernelResources;
    OsStatus_t Status;
    uintptr_t Offset;
    size_t Length;

    // Sanitize a handler is present, if not, no resources are present
    if ((uintptr_t)Resources->Handler == 0) {
        return OsSuccess;
    }

    // Unmap and release the fast-handler that we had mapped in.
    Offset      = ((uintptr_t)Resources->Handler) % GetMemorySpacePageSize();
    Length      = GetMemorySpacePageSize() + Offset;
    Status      = MemorySpaceUnmap(GetCurrentMemorySpace(),
        (uintptr_t)Resources->Handler, Length);
    if (Status != OsSuccess) {
        ERROR(" > failed to cleanup interrupt handler mapping");
        return OsError;
    }

    if (InterruptCleanupIoResources(Interrupt) != OsSuccess) {
        ERROR(" > failed to cleanup interrupt io resources");
        return OsError;
    }

    if (InterruptCleanupMemoryResources(Interrupt) != OsSuccess) {
        ERROR(" > failed to cleanup interrupt memory resources");
        return OsError;
    }
    return OsSuccess;
}

UUId_t
InterruptRegister(
    _In_ DeviceInterrupt_t* Interrupt,
    _In_ unsigned int       Flags)
{
    SystemInterrupt_t* Entry;
    UUId_t             TableIndex;
    UUId_t             Id;

    TRACE("InterruptRegister(Line %i Pin %i, Vector %i, Flags 0x%" PRIxIN ")",
        Interrupt->Line, Interrupt->Pin, Interrupt->Vectors[0], Flags);

    Entry = (SystemInterrupt_t*)kmalloc(sizeof(SystemInterrupt_t));
    if (!Entry) {
        return UUID_INVALID;
    }
    
    // TODO: change this to use handle system
    Id = atomic_fetch_add(&InterruptIdGenerator, 1);
    memset((void*)Entry, 0, sizeof(SystemInterrupt_t));

    Entry->Id           = (Id << 16);    
    Entry->ModuleHandle = UUID_INVALID;
    Entry->Thread       = GetCurrentThreadId();
    Entry->Flags        = Flags;

    // Get process id?
    if (!(Flags & INTERRUPT_KERNEL)) {
        assert(GetCurrentModule() != NULL);
        Entry->ModuleHandle = GetCurrentModule()->Handle;
    }

    // Resolve the table index
    if (InterruptResolve(Interrupt, Flags, &TableIndex) != OsSuccess) {
        ERROR("Failed to resolve the interrupt, invalid flags.");
        kfree(Entry);
        return OsError;
    }

    // Update remaining members now that we resolved
    Entry->Source  = Interrupt->Line; // clear Source for software interrupts?
    Entry->Id     |= TableIndex;
    memcpy(&Entry->Interrupt, Interrupt, sizeof(DeviceInterrupt_t));

    // Check against sharing
    if (Flags & INTERRUPT_EXCLUSIVE) {
        if (InterruptTable[TableIndex].Descriptor != NULL) {
            // We failed to gain exclusive access
            ERROR(" > can't gain exclusive access as there exist interrupt for 0x%x", TableIndex);
            kfree(Entry);
            return OsError;
        }
    }
    else if (InterruptTable[TableIndex].Sharable != 1 && 
             InterruptTable[TableIndex].Penalty > 0) {
        // Existing interrupt has exclusive access
        ERROR(" > existing interrupt has exclusive access");
        kfree(Entry);
        return OsError;
    }

    // Trace
    TRACE("Updated line %i:%i for index 0x%" PRIxIN "",
        Interrupt->Line, Interrupt->Pin, TableIndex);

    // If it's an user interrupt, resolve resources
    if (Entry->ModuleHandle != UUID_INVALID) {
        if (InterruptResolveResources(Entry) != OsSuccess) {
            ERROR(" > failed to resolve the requested resources");
            kfree(Entry);
            return OsError;
        }
    }
    
    // Initialize the table entry?
    IrqSpinlockAcquire(&InterruptTableSyncObject);
    if (InterruptTable[TableIndex].Descriptor == NULL) {
        InterruptTable[TableIndex].Descriptor = Entry;
        InterruptTable[TableIndex].Penalty    = 1;
        InterruptTable[TableIndex].Sharable   = (Flags & INTERRUPT_EXCLUSIVE) ? 0 : 1;
    }
    else {
        // Insert and increase penalty
        Entry->Link = InterruptTable[TableIndex].Descriptor;
        InterruptTable[TableIndex].Descriptor = Entry;
        if (InterruptIncreasePenalty(TableIndex) != OsSuccess) {
            ERROR("Failed to increase penalty for source %" PRIiIN "", Entry->Source);
        }
    }

    // Enable the new interrupt
    if (InterruptConfigure(Entry, 1) != OsSuccess) {
        ERROR("Failed to enable source %" PRIiIN "", Entry->Source);
    }
    IrqSpinlockRelease(&InterruptTableSyncObject);
    TRACE("Interrupt Id 0x%" PRIxIN " (Handler 0x%" PRIxIN ", Context 0x%" PRIxIN ")",
          Entry->Id, Entry->Interrupt.ResourceTable.Handler, Entry->Interrupt.Context);
    return Entry->Id;
}

OsStatus_t
InterruptUnregister(
    _In_ UUId_t Source)
{
    SystemInterrupt_t* Entry;
    SystemInterrupt_t* Previous   = NULL;
    OsStatus_t         Result     = OsError;
    uint16_t           TableIndex = LOWORD(Source);
    int                Found      = 0;

    // Sanitize parameter
    if (TableIndex >= MAX_SUPPORTED_INTERRUPTS) {
        return OsError;
    }
    
    // Iterate handlers in that table index and unlink the given entry
    IrqSpinlockAcquire(&InterruptTableSyncObject);
    Entry = InterruptTable[TableIndex].Descriptor;
    while (Entry != NULL) {
        if (Entry->Id == Source) {
            if (!(Entry->Flags & INTERRUPT_KERNEL)) {
                if (Entry->ModuleHandle != GetCurrentModule()->Handle) {
                    Previous = Entry;
                    Entry    = Entry->Link;
                    continue;
                }
            }

            // Marked entry as found
            Found = 1;
            if (Previous == NULL) {
                InterruptTable[TableIndex].Descriptor = Entry->Link;
            }
            else {
                Previous->Link = Entry->Link;
            }
            break;
        }
        Previous = Entry;
        Entry    = Entry->Link;
    }
    IrqSpinlockRelease(&InterruptTableSyncObject);

    // Sanitize if we were successfull
    if (Found == 0) {
        return OsError;
    }
    
    // Decrease penalty
    if (Entry->Source != INTERRUPT_NONE) {
        InterruptDecreasePenalty(Entry->Source);
    }

    // Entry is now unlinked, clean it up 
    // mask the interrupt again if neccessary
    if (Found == 1) {
        if (InterruptTable[Entry->Source].Penalty == 0) {
            InterruptConfigure(Entry, 0);
        }
        if (Entry->ModuleHandle != UUID_INVALID) {
            if (InterruptReleaseResources(Entry) != OsSuccess) {
                ERROR(" > failed to cleanup interrupt resources");
            }
        }
        kfree(Entry);
    }
    return Result;
}

SystemInterrupt_t*
InterruptGet(
    _In_ UUId_t Source)
{
    SystemInterrupt_t* Iterator;
    uint16_t           TableIndex = LOWORD(Source);

    Iterator = InterruptTable[TableIndex].Descriptor;
    while (Iterator != NULL) {
        if (Iterator->Id == Source) {
            return Iterator;
        }
    }
    return NULL;
}

SystemInterrupt_t*
InterruptGetIndex(
   _In_ UUId_t TableIndex)
{
    return InterruptTable[TableIndex].Descriptor;
}

void
InterruptSetActiveStatus(
    _In_ int Active)
{
    SystemCpuState_t State = READ_VOLATILE(GetCurrentProcessorCore()->State);
    State &= ~(CpuStateInterruptActive);
    if (Active) {
        State |= CpuStateInterruptActive;
    }
    WRITE_VOLATILE(GetCurrentProcessorCore()->State, State);
}

int
InterruptGetActiveStatus(void)
{
    SystemCpuState_t State = READ_VOLATILE(GetCurrentProcessorCore()->State);
    return (State & CpuStateInterruptActive) == 0 ? 0 : 1;
}

Context_t*
InterruptHandle(
    _In_  Context_t* Context,
    _In_  int        TableIndex)
{
    SystemCpuCore_t*   Core     = GetCurrentProcessorCore();
    uint32_t           Priority = InterruptsGetPriority();
    int                Source   = INTERRUPT_NONE;
    InterruptStatus_t  Result   = InterruptNotHandled;
    SystemInterrupt_t* Entry;
    InterruptsSetPriority(TableIndex);

    if (!Core->InterruptNesting) {
        InterruptSetActiveStatus(1);
        Core->InterruptRegisters = Context;
        Core->InterruptPriority  = Priority;
    }
    Core->InterruptNesting++;
#ifdef __OSCONFIG_NESTED_INTERRUPTS
    InterruptEnable();
#endif

    // Update current status
    Entry = InterruptTable[TableIndex].Descriptor;
    while (Entry != NULL) {
        if (Entry->Flags & INTERRUPT_KERNEL) {
            Result = Entry->Interrupt.ResourceTable.Handler(GetFastInterruptTable(), Entry->Interrupt.Context);
        }
        else {
            Result = Entry->KernelResources.Handler(GetFastInterruptTable(), &Entry->KernelResources);
        }

        if (Result == InterruptHandled) {
            Source = Entry->Source;
            break;
        }
        Entry = Entry->Link;
    }
    
    InterruptsAcknowledge(Source, TableIndex);
    
#ifdef __OSCONFIG_NESTED_INTERRUPTS
    InterruptDisable();
#endif
    Core->InterruptNesting--;
    if (!Core->InterruptNesting) {
        Context = Core->InterruptRegisters;
        Core->InterruptRegisters = NULL;
        InterruptsSetPriority(Core->InterruptPriority);
        InterruptSetActiveStatus(0);
    }
    else {
        InterruptsSetPriority(Priority);
    }
    return Context;
}
