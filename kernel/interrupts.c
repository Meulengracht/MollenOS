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
    if (InterruptTable[Source].Sharable == 0 && InterruptTable[Source].Penalty > 0) {
        return INTERRUPT_NONE;
    }
    return InterruptTable[Source].Penalty;
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
    _In_ DeviceInterrupt_t* deviceInterrupt,
    _In_ SystemInterrupt_t* systemInterrupt)
{
    InterruptResourceTable_t* Source      = &deviceInterrupt->ResourceTable;
    InterruptResourceTable_t* Destination = &systemInterrupt->KernelResources;
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
        (void)InterruptCleanupIoResources(systemInterrupt);
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
    _In_ DeviceInterrupt_t* deviceInterrupt,
    _In_ SystemInterrupt_t* systemInterrupt)
{
    InterruptResourceTable_t* source      = &deviceInterrupt->ResourceTable;
    InterruptResourceTable_t* destination = &systemInterrupt->KernelResources;
    OsStatus_t                success = OsSuccess;
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

            success = CloneMemorySpaceMapping(GetCurrentMemorySpace(), GetCurrentMemorySpace(),
                                              source->MemoryResources[i].Address, &updatedMapping, length, pageFlags, placementFlags);
            if (success != OsSuccess) {
                ERROR(" > failed to clone interrupt resource mapping");
                break;
            }
            TRACE(" > remapped resource to 0x%" PRIxIN " from 0x%" PRIxIN "", updatedMapping + offset, source->MemoryResources[i]);
            destination->MemoryResources[i].Address = updatedMapping + offset;
            destination->MemoryResources[i].Length  = source->MemoryResources[i].Length;
            destination->MemoryResources[i].Flags   = source->MemoryResources[i].Flags;
        }
    }

    if (success != OsSuccess) {
        (void)InterruptCleanupMemoryResources(systemInterrupt);
        return OsError;
    }
    return success;
}

/**
 * Maps the neccessary fast-interrupt resources into kernel space
 * and allowing the interrupt handler to access the requested memory spaces.
 * @param deviceInterrupt
 * @param systemInterrupt
 * @return
 */
static OsStatus_t
InterruptResolveResources(
    _In_ DeviceInterrupt_t* deviceInterrupt,
    _In_ SystemInterrupt_t* systemInterrupt)
{
    InterruptResourceTable_t * Source      = &deviceInterrupt->ResourceTable;
    InterruptResourceTable_t * Destination = &systemInterrupt->KernelResources;
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
    if (InterruptResolveIoResources(deviceInterrupt, systemInterrupt) != OsSuccess) {
        ERROR(" > failed to remap interrupt io resources");
        return OsError;
    }

    TRACE(" > remapping memory-resources");
    if (InterruptResolveMemoryResources(deviceInterrupt, systemInterrupt) != OsSuccess) {
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
    _In_ DeviceInterrupt_t* deviceInterrupt,
    _In_ unsigned int       flags)
{
    SystemInterrupt_t* systemInterrupt;
    UUId_t             tableIndex;
    UUId_t             id;

    if (!deviceInterrupt) {
        return OsInvalidParameters;
    }

    TRACE("InterruptRegister(Line %i Pin %i, Vector %i, Flags 0x%" PRIxIN ")",
          Interrupt->Line, Interrupt->Pin, Interrupt->Vectors[0], flags);

    systemInterrupt = (SystemInterrupt_t*)kmalloc(sizeof(SystemInterrupt_t));
    if (!systemInterrupt) {
        return UUID_INVALID;
    }
    
    // TODO: change this to use handle system
    id = atomic_fetch_add(&InterruptIdGenerator, 1);
    memset((void*)systemInterrupt, 0, sizeof(SystemInterrupt_t));

    systemInterrupt->Id           = (id << 16U);
    systemInterrupt->ModuleHandle = UUID_INVALID;
    systemInterrupt->Thread       = GetCurrentThreadId();
    systemInterrupt->Flags        = flags;
    systemInterrupt->Line         = deviceInterrupt->Line;
    systemInterrupt->Pin          = deviceInterrupt->Pin;
    systemInterrupt->AcpiConform  = deviceInterrupt->AcpiConform;

    // Get process id?
    if (!(flags & INTERRUPT_KERNEL)) {
        assert(GetCurrentModule() != NULL);
        systemInterrupt->ModuleHandle = GetCurrentModule()->Handle;
    }

    // Resolve the table index
    if (InterruptResolve(deviceInterrupt, flags, &tableIndex) != OsSuccess) {
        ERROR("Failed to resolve the interrupt, invalid flags.");
        kfree(systemInterrupt);
        return OsError;
    }

    // Update remaining members now that we resolved
    systemInterrupt->Source                         = deviceInterrupt->Line; // clear Source for software interrupts?
    systemInterrupt->Id                            |= tableIndex;
    systemInterrupt->Handler                        = deviceInterrupt->ResourceTable.Handler;
    systemInterrupt->Context                        = deviceInterrupt->Context;
    systemInterrupt->KernelResources.HandleResource = deviceInterrupt->ResourceTable.HandleResource;

    // Check against sharing
    if (flags & INTERRUPT_EXCLUSIVE) {
        if (InterruptTable[tableIndex].Descriptor != NULL) {
            // We failed to gain exclusive access
            ERROR(" > can't gain exclusive access as there exist interrupt for 0x%x", tableIndex);
            kfree(systemInterrupt);
            return OsError;
        }
    }
    else if (InterruptTable[tableIndex].Sharable != 1 && InterruptTable[tableIndex].Penalty > 0) {
        // Existing interrupt has exclusive access
        ERROR(" > existing interrupt has exclusive access");
        kfree(systemInterrupt);
        return OsError;
    }

    // Trace
    TRACE("Updated line %i:%i for index 0x%" PRIxIN,
          deviceInterrupt->Line, deviceInterrupt->Pin, tableIndex);

    // If it's an user interrupt, resolve resources
    if (systemInterrupt->ModuleHandle != UUID_INVALID) {
        if (InterruptResolveResources(deviceInterrupt, systemInterrupt) != OsSuccess) {
            ERROR(" > failed to resolve the requested resources");
            kfree(systemInterrupt);
            return OsError;
        }
    }
    
    // Initialize the table entry?
    IrqSpinlockAcquire(&InterruptTableSyncObject);
    if (InterruptTable[tableIndex].Descriptor == NULL) {
        InterruptTable[tableIndex].Descriptor = systemInterrupt;
        InterruptTable[tableIndex].Penalty    = 1;
        InterruptTable[tableIndex].Sharable   = (flags & INTERRUPT_EXCLUSIVE) ? 0 : 1;
    }
    else {
        // Insert and increase penalty
        systemInterrupt->Link                 = InterruptTable[tableIndex].Descriptor;
        InterruptTable[tableIndex].Descriptor = systemInterrupt;
        if (InterruptIncreasePenalty(tableIndex) != OsSuccess) {
            ERROR("Failed to increase penalty for source %" PRIiIN "", systemInterrupt->Source);
        }
    }

    // Enable the new interrupt
    if (InterruptConfigure(systemInterrupt, 1) != OsSuccess) {
        ERROR("Failed to enable source %" PRIiIN "", systemInterrupt->Source);
    }
    IrqSpinlockRelease(&InterruptTableSyncObject);
    TRACE("Interrupt Id 0x%" PRIxIN " (Handler 0x%" PRIxIN ", Context 0x%" PRIxIN ")",
          systemInterrupt->Id, systemInterrupt->Interrupt.ResourceTable.Handler, systemInterrupt->Interrupt.Context);
    return systemInterrupt->Id;
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
        return OsInvalidParameters;
    }
    
    // Iterate handlers in that table index and unlink the given entry
    IrqSpinlockAcquire(&InterruptTableSyncObject);
    Entry = InterruptTable[TableIndex].Descriptor;
    while (Entry) {
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
    if (!Found) {
        return OsDoesNotExist;
    }
    
    // Decrease penalty
    if (Entry->Source != INTERRUPT_NONE) {
        InterruptDecreasePenalty(Entry->Source);
    }

    // Entry is now unlinked, clean it up mask the interrupt again if neccessary
    if (InterruptTable[Entry->Source].Penalty == 0) {
        InterruptConfigure(Entry, 0);
    }
    if (Entry->ModuleHandle != UUID_INVALID) {
        if (InterruptReleaseResources(Entry) != OsSuccess) {
            ERROR(" > failed to cleanup interrupt resources");
        }
    }
    kfree(Entry);
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
    InterruptStatus_t  Result;
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
            Result = Entry->Handler(GetFastInterruptTable(), Entry->Context);
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
