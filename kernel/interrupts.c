/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Interrupt Interface
 * - Contains the shared kernel interrupt interface
 *   that is generic and can be shared/used by all systems
 */

#define __MODULE        "INIF"
//#define __TRACE

/* Includes 
 * - System */
#include <component/cpu.h>
#include <system/interrupts.h>
#include <system/thread.h>
#include <system/utils.h>
#include <process/phoenix.h>
#include <acpiinterface.h>
#include <interrupts.h>
#include <threading.h>
#include <timers.h>
#include <debug.h>
#include <heap.h>
#include <arch.h>

/* Includes
 * - Library */
#include <assert.h>
#include <stdio.h>

/* InterruptTableEntry
 * Describes an entry in the interrupt table */
typedef struct _InterruptTableEntry {
    MCoreInterruptDescriptor_t  *Descriptor;
    int                          Penalty;
    int                          Sharable;
} InterruptTableEntry_t;

/* Globals
 * - State keeping variables */
static InterruptTableEntry_t    InterruptTable[MAX_SUPPORTED_INTERRUPTS]    = { { 0 } };
static CriticalSection_t        InterruptTableSyncObject    = CRITICALSECTION_INITIALIZE(CRITICALSECTION_PLAIN);
static _Atomic(UUId_t)          InterruptIdGenerator        = ATOMIC_VAR_INIT(0);

/* AcpiGetPolarityMode
 * Returns whether or not the polarity is Active Low or Active High.
 * For Active Low = 1, Active High = 0 */
int
AcpiGetPolarityMode(
    _In_ uint16_t IntiFlags,
    _In_ int Source)
{
    // Parse the different polarity flags
    switch (IntiFlags & ACPI_MADT_POLARITY_MASK) {
        case ACPI_MADT_POLARITY_CONFORMS: {
            if (Source == (int)AcpiGbl_FADT.SciInterrupt)
                return 1;
            else
                return 0;
        } break;
        case ACPI_MADT_POLARITY_ACTIVE_HIGH:
            return 0;
        case ACPI_MADT_POLARITY_ACTIVE_LOW:
            return 1;
    }
    return 0;
}

/* AcpiGetTriggerMode
 * Returns whether or not the trigger mode of the interrup is level or edge.
 * For Level = 1, Edge = 0 */
int
AcpiGetTriggerMode(
    _In_ uint16_t IntiFlags,
    _In_ int Source)
{
    // Parse the different trigger mode flags
    switch (IntiFlags & ACPI_MADT_TRIGGER_MASK) {
        case ACPI_MADT_TRIGGER_CONFORMS: {
            if (Source == (int)AcpiGbl_FADT.SciInterrupt)
                return 1;
            else
                return 0;
        } break;
        case ACPI_MADT_TRIGGER_EDGE:
            return 0;
        case ACPI_MADT_TRIGGER_LEVEL:
            return 1;
    }
    return 0;
}

/* ConvertAcpiFlagsToConformFlags
 * Converts acpi interrupt flags to the system interrupt conform flags. */
Flags_t
ConvertAcpiFlagsToConformFlags(
    _In_ uint16_t           IntiFlags,
    _In_ int                Source)
{
    Flags_t ConformFlags = 0;

    if (AcpiGetPolarityMode(IntiFlags, Source) == 1) {
        ConformFlags |= __DEVICEMANAGER_ACPICONFORM_POLARITY;
    }
    if (AcpiGetTriggerMode(IntiFlags, Source) == 1) {
        ConformFlags |= __DEVICEMANAGER_ACPICONFORM_TRIGGERMODE;
    }
    return ConformFlags;
}

/* AcpiDeriveInterrupt
 * Derives an interrupt by consulting the bus of the device, 
 * and spits out flags in AcpiConform and returns irq */
int
AcpiDeriveInterrupt(
    _In_  DevInfo_t Bus, 
    _In_  DevInfo_t Device,
    _In_  int       Pin,
    _Out_ Flags_t*  AcpiConform)
{
    // Variables
    AcpiDevice_t *Dev = NULL;
    DataKey_t iKey;

    // Calculate routing index
    unsigned rIndex = (Device * 4) + (Pin - 1);
    iKey.Value      = 0;

    // Trace
    TRACE("AcpiDeriveInterrupt(Bus %u, Device %u, Pin %i)", Bus, Device, Pin);

    // Start by checking if we can find the
    // routings by checking the given device
    Dev = AcpiDeviceLookupBusRoutings(Bus);

    // Make sure there was a bus device for it
    if (Dev != NULL) {
        TRACE("Found bus-device <%s>, accessing index %u", 
            &Dev->HId[0], rIndex);
        if (Dev->Routings->ActiveIrqs[rIndex] != INTERRUPT_NONE
            && Dev->Routings->InterruptEntries[rIndex] != NULL) {
            PciRoutingEntry_t *RoutingEntry = NULL;

            // Lookup entry in list
            foreach(iNode, Dev->Routings->InterruptEntries[rIndex]) {
                RoutingEntry = (PciRoutingEntry_t*)iNode->Data;
                if (RoutingEntry->Irq == Dev->Routings->ActiveIrqs[rIndex]) {
                    break;
                }
            }

            // Update IRQ Information
            *AcpiConform = __DEVICEMANAGER_ACPICONFORM_PRESENT;
            if (RoutingEntry->Trigger == ACPI_LEVEL_SENSITIVE) {
                *AcpiConform |= __DEVICEMANAGER_ACPICONFORM_TRIGGERMODE;
            }

            if (RoutingEntry->Polarity == ACPI_ACTIVE_LOW) {
                *AcpiConform |= __DEVICEMANAGER_ACPICONFORM_POLARITY;
            }

            if (RoutingEntry->Shareable != 0) {
                *AcpiConform |= __DEVICEMANAGER_ACPICONFORM_SHAREABLE;
            }

            if (RoutingEntry->Fixed != 0) {
                *AcpiConform |= __DEVICEMANAGER_ACPICONFORM_FIXED;
            }

            // Return found interrupt
            TRACE("Found interrupt %i", RoutingEntry->Irq);
            return RoutingEntry->Irq;
        }
    }

    // None found
    TRACE("No interrupt found");
    return INTERRUPT_NONE;
}

/* InterruptIncreasePenalty 
 * Increases the penalty for an interrupt source. */
OsStatus_t
InterruptIncreasePenalty(
    _In_ int Source)
{
    // Sanitize the requested source bounds
    if (Source < 0 || Source >= MAX_SUPPORTED_INTERRUPTS) {
        return INTERRUPT_NONE;
    }

    // Done
    InterruptTable[Source].Penalty++;
    return OsSuccess;
}

/* InterruptDecreasePenalty 
 * Decreases the penalty for an interrupt source. */
OsStatus_t
InterruptDecreasePenalty(
    _In_ int Source)
{
    // Sanitize the requested source bounds
    if (Source < 0 || Source >= MAX_SUPPORTED_INTERRUPTS) {
        return INTERRUPT_NONE;
    }

    // Done
    InterruptTable[Source].Penalty--;
    return OsSuccess;
}

/* InterruptGetPenalty
 * Retrieves the penalty for an interrupt source. 
 * If INTERRUPT_NONE is returned the source is unavailable. */
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

    // Finished
    return InterruptTable[Source].Penalty;
}

/* InterruptGetLeastLoaded
 * Returns the least loaded interrupt source currently. 
 * Out of the given available interrupt sources. */
int
InterruptGetLeastLoaded(
    _In_ int Irqs[],
    _In_ int Count)
{
    // Variables
    int SelectedPenality = INTERRUPT_NONE;
    int SelectedIrq = INTERRUPT_NONE;
    int i;
    
    // Debug
    TRACE("InterruptGetLeastLoaded(Count %i)", Count);

    // Iterate all the available irqs
    // that the device-supports
    for (i = 0; i < Count; i++) {
        if (Irqs[i] == INTERRUPT_NONE) {
            break;
        }

        // Calculate count
        int Penalty = InterruptGetPenalty(Irqs[i]);

        // Sanitize status, if -1
        // then its not usable
        if (Penalty == INTERRUPT_NONE) {
            continue;
        }

        // Store the lowest penalty
        if (SelectedIrq == INTERRUPT_NONE
            || Penalty < SelectedPenality) {
            SelectedIrq = Irqs[i];
            SelectedPenality = Penalty;
        }
    }
    return SelectedIrq;
}

/* InterruptRegister
 * Tries to allocate the given interrupt source
 * by the given descriptor and flags. On success
 * it returns the id of the irq, and on failure it
 * returns UUID_INVALID */
UUId_t
InterruptRegister(
    _In_ MCoreInterrupt_t*  Interrupt,
    _In_ Flags_t            Flags)
{
    // Variables
    MCoreInterruptDescriptor_t *Entry = NULL;
    UUId_t TableIndex                 = 0;
    UUId_t Id                         = 0;

    // Trace
    TRACE("InterruptRegister(Line %i, Pin %i, Vector %i, Flags 0x%x)",
        Interrupt->Line, Interrupt->Pin, Interrupt->Vectors[0], Flags);

    // Allocate a new entry for the table
    Entry   = (MCoreInterruptDescriptor_t*)kmalloc(sizeof(MCoreInterruptDescriptor_t));
    Id      = atomic_fetch_add(&InterruptIdGenerator, 1);

    // Setup some initial information
    Entry->Id       = (Id << 16);    
    Entry->Ash      = UUID_INVALID;
    Entry->Thread   = ThreadingGetCurrentThreadId();
    Entry->Flags    = Flags;
    Entry->Link     = NULL;

    // Clear out line if the interrupt is software
    if (Flags & INTERRUPT_SOFT) {
        Interrupt->Line = INTERRUPT_NONE;
    }

    // Get process id?
    if (!(Flags & INTERRUPT_KERNEL)) {
        Entry->Ash = ThreadingGetCurrentThread(CpuGetCurrentId())->AshId;
    }

    // Resolve the table index
    if (InterruptResolve(Interrupt, Flags, &TableIndex) != OsSuccess) {
        ERROR("Failed to resolve the interrupt, invalid flags.");
        goto Error;
    }
    else {
        Entry->Source = Interrupt->Line;
        Entry->Id |= TableIndex;
    }

    // Trace
    TRACE("Updated line %i:%i for index 0x%x", 
        Interrupt->Line, Interrupt->Pin, TableIndex);

    // Copy interrupt information over
    memcpy(&Entry->Interrupt, Interrupt, sizeof(MCoreInterrupt_t));
    
    // Sanitize the sharable status first
    CriticalSectionEnter(&InterruptTableSyncObject);
    if (Flags & INTERRUPT_NOTSHARABLE) {
        if (InterruptTable[TableIndex].Descriptor != NULL) {
            ERROR("Failed to install interrupt as it was not sharable");
            goto Error;
        }
    }
    else if (InterruptTable[TableIndex].Sharable != 1
            && InterruptTable[TableIndex].Penalty > 0) {
        ERROR("Failed to install interrupt as it was not sharable");
        goto Error;
    }
    
    // Initialize the table entry?
    if (InterruptTable[TableIndex].Descriptor == NULL) {
        InterruptTable[TableIndex].Descriptor = Entry;
        InterruptTable[TableIndex].Penalty = 1;
        InterruptTable[TableIndex].Sharable = (Flags & INTERRUPT_NOTSHARABLE) ? 0 : 1;
    }
    else {
        // Insert and increase penalty
        Entry->Link = InterruptTable[TableIndex].Descriptor;
        InterruptTable[TableIndex].Descriptor = Entry;
        if (InterruptIncreasePenalty(TableIndex) != OsSuccess) {
            ERROR("Failed to increase penalty for source %i", Entry->Source);
        }
    }

    // Enable the new interrupt
    if (InterruptConfigure(Entry, 1) != OsSuccess) {
        ERROR("Failed to enable source %i", Entry->Source);
    }
    CriticalSectionLeave(&InterruptTableSyncObject);
    
    TRACE("Interrupt Id 0x%x", Entry->Id);
    return Entry->Id;
Error:
    // Cleanup
    CriticalSectionLeave(&InterruptTableSyncObject);
    if (Entry != NULL) {
        kfree(Entry);
    }
    return UUID_INVALID;
}

/* InterruptUnregister 
 * Unregisters the interrupt from the system and removes
 * any resources that was associated with that interrupt 
 * also masks the interrupt if it was the only user */
OsStatus_t
InterruptUnregister(
    _In_ UUId_t Source)
{
    // Variables
    MCoreInterruptDescriptor_t  *Entry    = NULL, 
                                *Previous = NULL;
    OsStatus_t Result                     = OsError;
    uint16_t TableIndex                   = LOWORD(Source);
    int Found                             = 0;

    // Sanitize parameter
    if (TableIndex >= MAX_SUPPORTED_INTERRUPTS) {
        return OsError;
    }
    
    // Iterate handlers in that table index and unlink the given entry
    CriticalSectionEnter(&InterruptTableSyncObject);
    Entry = InterruptTable[TableIndex].Descriptor;
    while (Entry != NULL) {
        if (Entry->Id == Source) {
            if (!(Entry->Flags & INTERRUPT_KERNEL)) {
                if (Entry->Ash != ThreadingGetCurrentThread(CpuGetCurrentId())->AshId) {
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
        
        // Next entry
        Previous = Entry;
        Entry = Entry->Link;
    }
    CriticalSectionLeave(&InterruptTableSyncObject);

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
        kfree(Entry);
    }
    return Result;
}

/* InterruptGet
 * Retrieves the given interrupt source information
 * as a MCoreInterruptDescriptor_t */
MCoreInterruptDescriptor_t*
InterruptGet(
    _In_ UUId_t Source)
{
    // Variables
    MCoreInterruptDescriptor_t *Iterator = NULL;
    uint16_t TableIndex = LOWORD(Source);

    // Iterate at the correct entry
    Iterator = InterruptTable[TableIndex].Descriptor;
    while (Iterator != NULL) {
        if (Iterator->Id == Source) {
            return Iterator;
        }
    }

    // We didn't find it
    return NULL;
}

/* InterruptGetIndex
 * Retrieves the given interrupt source information
 * as a MCoreInterruptDescriptor_t */
MCoreInterruptDescriptor_t*
InterruptGetIndex(
   _In_ UUId_t TableIndex)
{
    return InterruptTable[TableIndex].Descriptor;
}

/* InterruptSetActiveStatus
 * Set's the current status for the calling cpu to
 * interrupt-active state */
void
InterruptSetActiveStatus(
    _In_ int Active)
{
    // Update current cpu status
    GetCurrentProcessorCore()->State &= ~(CpuStateInterruptActive);
    if (Active) {
        GetCurrentProcessorCore()->State |= CpuStateInterruptActive;
    }
}

/* InterruptGetActiveStatus
 * Get's the current status for the calling cpu to
 * interrupt-active state */
int
InterruptGetActiveStatus(void)
{
    return (GetCurrentProcessorCore()->State & CpuStateInterruptActive) == 0 ? 0 : 1;
}

/* InterruptHandle
 * Handles an interrupt by invoking the registered handlers
 * on the given table-index. */
InterruptStatus_t
InterruptHandle(
    _In_  Context_t*    Context,
    _In_  int           TableIndex,
    _Out_ int*          Source)
{
    // Variables
    MCoreThread_t *Current = NULL, *Target = NULL, *Start = NULL;
    MCoreInterruptDescriptor_t *Entry = NULL;
    InterruptStatus_t Result = InterruptNotHandled;

    // Update current status
    InterruptSetActiveStatus(1);
    
    // Initiate values
    Start = Current = ThreadingGetCurrentThread(CpuGetCurrentId());

    // Iterate handlers in that table index
    Entry = InterruptTable[TableIndex].Descriptor;
    if (Entry != NULL) {
        *Source = Entry->Source;
    }
    while (Entry != NULL) {
        if (Entry->Flags & INTERRUPT_KERNEL) {
            if (Entry->Flags & INTERRUPT_CONTEXT) {
                Result = Entry->Interrupt.FastHandler((void*)Context);
            }
            else {
                Result = Entry->Interrupt.FastHandler(Entry->Interrupt.Data);
            }

            // If it was handled we can break
            // early as there is no need to check rest
            if (Result == InterruptHandled) {
                break;
            }
        }
        else {
            Target = ThreadingGetThread(Entry->Thread);

            // Impersonate the target thread
            // and call the fast handler
            if (Current->MemorySpace != Target->MemorySpace) {
                Current = Target;
                ThreadingImpersonate(Target);
            }
            Result = Entry->Interrupt.FastHandler(Entry->Interrupt.Data);

            // If it was handled
            // - Restore original context
            // - Register interrupt, might be a system timer
            // - Queue the processing handler if any
            if (Result == InterruptHandled) {
                TimersInterrupt(Entry->Id);

                // Send a interrupt-event to this
                // and mark as handled, so we don't spit out errors
                if (Entry->Flags & INTERRUPT_USERSPACE) {
                    __KernelInterruptDriver(Entry->Ash, Entry->Id, Entry->Interrupt.Data);
                }
                break;
            }
        }

        // Move on to next entry
        Entry = Entry->Link;
    }

    // We might have to restore context
    if (Start->MemorySpace != Current->MemorySpace) {
        ThreadingImpersonate(Start);
    }

    // Update current status
    InterruptSetActiveStatus(0);
    return Result;
}
