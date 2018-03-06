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
 * MollenOS Interrupt Interface (X86)
 * - Contains the shared kernel interrupt interface
 *   that all sub-layers must conform to
 *
 * - ISA Interrupts should be routed to boot-processor without lowest-prio?
 */
#define __MODULE        "IRQS"
//#define __TRACE

/* Includes 
 * - System */
#include <system/interrupts.h>
#include <system/thread.h>
#include <system/utils.h>

#include <process/phoenix.h>
#include <acpiinterface.h>
#include <interrupts.h>
#include <threading.h>
#include <memory.h>
#include <thread.h>
#include <debug.h>
#include <heap.h>
#include <apic.h>
#include <gdt.h>
#include <idt.h>
#include <pic.h>

/* Includes
 * - Library */
#include <ds/collection.h>
#include <assert.h>
#include <stdio.h>

/* ThreadingFpuException
 * Handles the fpu exception that might get triggered
 * when performing any float/double instructions. */
OsStatus_t
ThreadingFpuException(
    _In_ MCoreThread_t *Thread);

/* Internal definitons and helper contants */
#define EFLAGS_INTERRUPT_FLAG        (1 << 9)
#define APIC_FLAGS_DEFAULT            0x7F00000000000000
#define NUM_ISA_INTERRUPTS			16

/* Externs 
 * Extern assembly functions */
__EXTERN void __cli(void);
__EXTERN void __sti(void);
__EXTERN reg_t __getflags(void);
__EXTERN reg_t __getcr2(void);
__EXTERN void enter_thread(Context_t *Regs);

/* Externs 
 * These are for external access to some of the ACPI information */
__EXTERN Collection_t *GlbAcpiNodes;

/* InterruptGetApicConfiguration
 * Determines the correct APIC flags for the io-apic entry
 * from the interrupt structure */
uint64_t
InterruptGetApicConfiguration(
    _In_ MCoreInterrupt_t *Interrupt)
{
    // Variables
    uint64_t ApicFlags = APIC_FLAGS_DEFAULT;
    
    // Trace
    TRACE("InterruptDetermine()");

    // Case 1 - ISA Interrupts 
    // - In most cases are Edge-Triggered, Active-High
    if (Interrupt->Line < NUM_ISA_INTERRUPTS
        && Interrupt->Pin == INTERRUPT_NONE) {
        int Enabled, LevelTriggered;
        PicGetConfiguration(Interrupt->Line, &Enabled, &LevelTriggered);
        ApicFlags |= 0x100;                    // Lowest Priority
        ApicFlags |= 0x800;                    // Logical Destination Mode
        if (LevelTriggered == 1) {
            TRACE(" - ISA Peripheral Interrupt (Active-Low, Level-Triggered)");
            ApicFlags |= APIC_ACTIVE_LOW;            // Set Polarity
            ApicFlags |= APIC_LEVEL_TRIGGER;        // Set Trigger Mode
        }
        else {
            TRACE(" - ISA Interrupt (Active-High, Edge-Triggered)");
        }
    }
    
    // Case 2 - PCI Interrupts (No-Pin) 
    // - Must be Level Triggered Low-Active
    else if (Interrupt->Line >= NUM_ISA_INTERRUPTS
            && Interrupt->Pin == INTERRUPT_NONE) {
        TRACE(" - PCI Interrupt (Active-Low, Level-Triggered)");
        ApicFlags |= 0x100;                        // Lowest Priority
        ApicFlags |= 0x800;                        // Logical Destination Mode
        ApicFlags |= APIC_ACTIVE_LOW;            // Set Polarity
        ApicFlags |= APIC_LEVEL_TRIGGER;        // Set Trigger Mode
    }

    // Case 3 - PCI Interrupts (Pin) 
    // - Usually Level Triggered Low-Active
    else if (Interrupt->Pin != INTERRUPT_NONE) {
        // If no routing exists use the pci interrupt line
        if (!(Interrupt->AcpiConform & __DEVICEMANAGER_ACPICONFORM_PRESENT)) {
            TRACE(" - PCI Interrupt (Active-Low, Level-Triggered)");
            ApicFlags |= 0x100;                    // Lowest Priority
            ApicFlags |= 0x800;                    // Logical Destination Mode
        }
        else {
            TRACE(" - PCI Interrupt (Pin-Configured - 0x%x)", Interrupt->AcpiConform);
            ApicFlags |= 0x100;                    // Lowest Priority
            ApicFlags |= 0x800;                    // Logical Destination Mode

            // Both trigger and polarity is either fixed or set by the
            // information we extracted earlier
            if (Interrupt->Line >= NUM_ISA_INTERRUPTS) {
                ApicFlags |= APIC_ACTIVE_LOW;
                ApicFlags |= APIC_LEVEL_TRIGGER;
            }
            else {
                if (Interrupt->AcpiConform & __DEVICEMANAGER_ACPICONFORM_TRIGGERMODE) {
                    ApicFlags |= APIC_LEVEL_TRIGGER;
                }
                if (Interrupt->AcpiConform & __DEVICEMANAGER_ACPICONFORM_POLARITY) {
                    ApicFlags |= APIC_ACTIVE_LOW;
                }
            }
        }
    }

    // Done
    return ApicFlags;
}

/* InterruptResolve 
 * Resolves the table index from the given interrupt settings. */
OsStatus_t
InterruptResolve(
    _InOut_ MCoreInterrupt_t *Interrupt,
    _In_ Flags_t Flags,
    _Out_ UUId_t *TableIndex)
{
    // 1 Resolve the physical interrupt line
    if (!(Flags & INTERRUPT_SOFT)) {
        if (Flags & (INTERRUPT_VECTOR | INTERRUPT_MSI)) {
            int Vectors[INTERRUPT_PHYSICAL_END - INTERRUPT_PHYSICAL_BASE];
            int i;
            if (Flags & INTERRUPT_MSI) {
                for (i = 0; i < (INTERRUPT_PHYSICAL_END - INTERRUPT_PHYSICAL_BASE); i++) {
                    Vectors[i] = (INTERRUPT_PHYSICAL_BASE + i);
                }
            }
            else {
                Vectors[INTERRUPT_MAXVECTORS] = INTERRUPT_NONE;
                for (i = 0; i < INTERRUPT_MAXVECTORS; i++) {
                    if (Interrupt->Vectors[i] == INTERRUPT_NONE
                        || i == INTERRUPT_MAXVECTORS) {
                        Vectors[i] = INTERRUPT_NONE;
                        break;
                    }
                    Vectors[i] = (INTERRUPT_PHYSICAL_BASE + Interrupt->Vectors[i]);
                }
            }
            Interrupt->Line = InterruptGetLeastLoaded(Vectors, i);

            // Adjust to physical
            if (Interrupt->Line != INTERRUPT_NONE) {
                Interrupt->Line -= INTERRUPT_PHYSICAL_BASE;
            }
        }
    }

    // Do we need to override the source?
    if (Interrupt->Line != INTERRUPT_NONE) {
        // Now lookup in ACPI overrides if we should
        // change the global source
        foreach(iNode, GlbAcpiNodes) {
            if (iNode->Key.Value == ACPI_MADT_TYPE_INTERRUPT_OVERRIDE) {
                ACPI_MADT_INTERRUPT_OVERRIDE *IoEntry =
                    (ACPI_MADT_INTERRUPT_OVERRIDE*)iNode->Data;
                if ((int)IoEntry->SourceIrq == Interrupt->Line) {
                    Interrupt->Line = IoEntry->GlobalIrq;
                    break;
                }
            }
        }
    }

    // 2 Resolve the table index
    if (Flags & INTERRUPT_MSI) {
        *TableIndex = (INTERRUPT_PHYSICAL_BASE + (UUId_t)Interrupt->Line);

        // Fill in MSI data
        // MSI Message Address Register (0xFEE00000 LAPIC)
        // Bits 31-20: Must be 0xFEE
        // Bits 19-11: Destination ID
        // Bits 11-04: Reserved
        // Bit      3: 0 = Destination is ONE CPU, 1 = Destination is Group
        // Bit      2: Destination Mode (1 Logical, 0 Physical)
        // Bits 00-01: X
        Interrupt->MsiAddress = 0xFEE00000 | (0x0007F0000) 
            | (0x8 | 0x4);

        // Message Data Register Format
        // Bits 31-16: Reserved
        // Bit     15: Trigger Mode (1 Level, 0 Edge)
        // Bit     14: If edge, this is not used, if level, 1 = Assert, 0 = Deassert
        // Bits 13-11: Reserved
        // Bits 10-08: Delivery Mode, standard
        // Bits 07-00: Vector
        Interrupt->MsiValue = (0x100 | (*TableIndex & 0xFF));
    }
    else {
        // Driver/kernel interrupt
        if (Flags & INTERRUPT_SOFT) {
            if (Flags & INTERRUPT_VECTOR) {
                *TableIndex = InterruptGetLeastLoaded(
                    Interrupt->Vectors, INTERRUPT_MAXVECTORS);
            }
            else {
                *TableIndex = Interrupt->Vectors[0];
            }
        }
        else {
            *TableIndex = (INTERRUPT_PHYSICAL_BASE + (UUId_t)Interrupt->Line);
        }
    }
    return OsSuccess;
}

/* InterruptConfigure
 * Configures the given interrupt in the system */
OsStatus_t
InterruptConfigure(
    _In_ MCoreInterruptDescriptor_t *Descriptor,
    _In_ int Enable)
{
    // Variables
    uint64_t ApicFlags      = APIC_FLAGS_DEFAULT;
    IoApic_t *IoApic        = NULL;
    UUId_t TableIndex       = 0;
    union {
        struct {
            uint32_t Lo;
            uint32_t Hi;
        } Parts;
        uint64_t Full;
    } ApicExisting;
    
    // Debug
    TRACE("InterruptConfigure(Id 0x%x, Enable %i)", 
        Descriptor->Id, Enable);

    // Is this a software interrupt? Don't install
    if (Descriptor->Flags & INTERRUPT_SOFT
        || Descriptor->Interrupt.Line == INTERRUPT_NONE) {
        return OsSuccess;
    }

    // Determine the kind of apic configuration
    TableIndex = (Descriptor->Id & 0xFF);
    ApicFlags = InterruptGetApicConfiguration(&Descriptor->Interrupt);
    ApicFlags |= TableIndex;

    // Trace
    TRACE("Calculated flags for interrupt: 0x%x (TableIndex %u)", 
        LODWORD(ApicFlags), TableIndex);

    // If this is an (E)ISA interrupt make sure it's configured
    // properly in the PIC/ELCR
    if (Descriptor->Source < NUM_ISA_INTERRUPTS) {
        // ISA Interrupts can be level triggered
        // so make sure we configure it for level triggering
        if (ApicFlags & APIC_LEVEL_TRIGGER) {
            PicConfigureLine(Descriptor->Source, 0, 1);
        }
    }

    // Get correct Io Apic
    IoApic = ApicGetIoFromGsi(Descriptor->Source);

    // If Apic Entry is located, we need to adjust
    if (IoApic != NULL) {
        ApicExisting.Full = ApicReadIoEntry(IoApic, Descriptor->Source);

        // Sanity, we can't just override the existing interrupt vector
        // so if it's already installed, we modify the table-index
        if (!(ApicExisting.Parts.Lo & APIC_MASKED)) {
            UUId_t ExistingIndex = LOBYTE(LOWORD(ApicExisting.Parts.Lo));
            if (ExistingIndex != TableIndex) {
                FATAL(FATAL_SCOPE_KERNEL, 
                    "Table index for already installed interrupt: %u", 
                    TableIndex);
            }
        }
        else {
            // Unmask the irq in the io-apic
            TRACE("Installing source %i => 0x%x", Descriptor->Source, LODWORD(ApicFlags));
            ApicWriteIoEntry(IoApic, Descriptor->Source, ApicFlags);
        }
    }
    else {
        ERROR("Failed to derive io-apic for source %i", Descriptor->Source);
    }

    // Done
    return OsSuccess;
}

/* InterruptEntry
 * The common entry point for interrupts, all
 * non-exceptions will enter here, lookup a handler
 * and execute the code */
void
InterruptEntry(
    _In_ Context_t *Registers)
{
    // Variables
    InterruptStatus_t Result = InterruptNotHandled;
    int TableIndex = (int)Registers->Irq + 32;
    int Gsi = APIC_NO_GSI;

    // Call kernel method
    Result = InterruptHandle(Registers, TableIndex, &Gsi);

    // Sanitize the result of the
    // irq-handling - all irqs must be handled
    if (Result != InterruptHandled 
        && InterruptGetIndex(TableIndex) == NULL) {
        // Unhandled interrupts are only ok if spurious
        // LAPIC, Interrupt 7 and 15
        if (TableIndex != INTERRUPT_SPURIOUS
            && TableIndex != (INTERRUPT_PHYSICAL_BASE + 7)
            && TableIndex != (INTERRUPT_PHYSICAL_BASE + 15)) {
            // Fault
            FATAL(FATAL_SCOPE_KERNEL, "Unhandled interrupt %u (Source %i)", 
                TableIndex, Gsi);
        }
    }
    else {
        // Last action is send eoi
        ApicSendEoi(Gsi, TableIndex);
    }
}

/* ExceptionSignal
 * Sends a signal to the executing thread/process immediately.
 * If the signal is blocked the process/thread is killed. */
OsStatus_t
ExceptionSignal(
    _In_ Context_t  *Registers,
    _In_ int         Signal)
{
    // Variables
    MCoreThread_t *Thread   = NULL;
    MCoreAsh_t *Process     = NULL;
    UUId_t Cpu              = CpuGetCurrentId();

    // Debug
    TRACE("ExceptionSignal(Signal %i)", Signal);

    // Sanitize if user-process
    Process = PhoenixGetCurrentAsh();
#ifdef __OSCONFIG_DISABLE_SIGNALLING
    if (Signal >= 0) {
#else
    if (Process == NULL) {
#endif
        return OsError;
    }

    // Lookup current thread
    Thread = ThreadingGetCurrentThread(Cpu);

    // Initialize signal
    Thread->ActiveSignal.Ignorable = 0;
    Thread->ActiveSignal.Signal = Signal;
    Thread->ActiveSignal.Context = Registers;

    // Dispatch
    return ThreadingSignalDispatch(Thread);
}

/* ExceptionEntry
 * Common entry for all exceptions */
void
ExceptionEntry(
    _In_ Context_t *Registers)
{
    // Variables
    MCoreThread_t *Thread   = NULL;
    uintptr_t Address       = __MASK;
    int IssueFixed          = 0;

    // Handle IRQ
    if (Registers->Irq == 0) {      // Divide By Zero (Non-math instruction)
        if (ExceptionSignal(Registers, SIGFPE) == OsSuccess) {
            IssueFixed = 1;
        }
    }
    else if (Registers->Irq == 1) { // Single Step
        if (DebugSingleStep(Registers) == OsSuccess) {
            // Re-enable single-step
        }
        IssueFixed = 1;
    }
    else if (Registers->Irq == 2) { // NMI
        // Fall-through to kernel fault
    }
    else if (Registers->Irq == 3) { // Breakpoint
        DebugBreakpoint(Registers);
        IssueFixed = 1;
    }
    else if (Registers->Irq == 4) { // Overflow
        if (ExceptionSignal(Registers, SIGSEGV) == OsSuccess) {
            IssueFixed = 1;
        }
    }
    else if (Registers->Irq == 5) { // Bound Range Exceeded
        if (ExceptionSignal(Registers, SIGSEGV) == OsSuccess) {
            IssueFixed = 1;
        }
    }
    else if (Registers->Irq == 6) { // Invalid Opcode
        if (ExceptionSignal(Registers, SIGILL) == OsSuccess) {
            IssueFixed = 1;
        }
    }
    else if (Registers->Irq == 7) { // DeviceNotAvailable 
        Thread = ThreadingGetCurrentThread(CpuGetCurrentId());
        assert(Thread != NULL);

        // This might be because we need to restore fpu/sse state
        if (ThreadingFpuException(Thread) != OsSuccess) {
            if (ExceptionSignal(Registers, SIGFPE) == OsSuccess) {
                IssueFixed = 1;
            }
        }
        else {
            IssueFixed = 1;
        }
    }
    else if (Registers->Irq == 8) { // Double Fault
        // Fall-through to kernel fault
    }
    else if (Registers->Irq == 9) { // Coprocessor Segment Overrun (Obsolete)
        // Fall-through to kernel fault
    }
    else if (Registers->Irq == 10) { // Invalid TSS
        // Fall-through to kernel fault
    }
    else if (Registers->Irq == 11) { // Segment Not Present
        if (ExceptionSignal(Registers, SIGSEGV) == OsSuccess) {
            IssueFixed = 1;
        }
    }
    else if (Registers->Irq == 12) { // Stack Segment Fault
        if (ExceptionSignal(Registers, SIGSEGV) == OsSuccess) {
            IssueFixed = 1;
        }
    }
    else if (Registers->Irq == 13) { // General Protection Fault
        if (ExceptionSignal(Registers, SIGSEGV) == OsSuccess) {
            IssueFixed = 1;
        }
    }
    else if (Registers->Irq == 14) {    // Page Fault
        Address = (uintptr_t)__getcr2();

        // The first thing we must check before propegating events
        // is that we must check special locations
        if (Address == MEMORY_LOCATION_SIGNAL_RET) {
            UUId_t Cpu  = CpuGetCurrentId();
            Thread      = ThreadingGetCurrentThread(Cpu);
            Registers   = Thread->ActiveSignal.Context;
            TssUpdateThreadStack(Cpu, (uintptr_t)Thread->Contexts[THREADING_CONTEXT_LEVEL0]);

            // Complete signal handling
            SignalReturn();
            enter_thread(Registers);
        }

        // Next step is to check whether or not the address is already
        // mapped, because then it's due to accessibility
        if (MmVirtualGetMapping(NULL, Address) != 0) {
            WARNING("Page fault at address 0x%x, but page is already mapped, invalid access. (User tried to access kernel memory ex).", Address);
        }

        // Final step is to see if kernel can handle the 
        // unallocated address
        else if (DebugPageFault(Registers, Address) == OsSuccess) {
            IssueFixed = 1;
        }
        else {
            if (ExceptionSignal(Registers, SIGSEGV) == OsSuccess) {
                IssueFixed = 1;
            }
        }
    }
    else if (Registers->Irq == 16 || Registers->Irq == 19) {    // FPU & SIMD Floating Point Exception
        if (ExceptionSignal(Registers, SIGFPE) == OsSuccess) {
            IssueFixed = 1;
        }
    }

    // Was the exception handled?
    if (IssueFixed == 0) {
        uintptr_t Base  = 0;
        char *Name      = NULL;
        LogRedirect(LogConsole);

        // Was it a page-fault?
        if (Address != __MASK) {
            LogDebug(__MODULE, "CR2 Address: 0x%x", Address);
        }

        // Locate which module
        if (DebugGetModuleByAddress(CONTEXT_IP(Registers), &Base, &Name) == OsSuccess) {
            uintptr_t Diff = CONTEXT_IP(Registers) - Base;
            LogDebug(__MODULE, "Faulty Address: 0x%x (%s)", Diff, Name);
        }
        else {
            LogDebug(__MODULE, "Faulty Address: 0x%x", CONTEXT_IP(Registers));
        }

        // Enter panic handler
        DebugContext(Registers);
        DebugPanic(FATAL_SCOPE_KERNEL, Registers, __MODULE,
            "Unhandled or fatal interrupt %u, Error Code: %u, Faulty Address: 0x%x",
            Registers->Irq, Registers->ErrorCode, CONTEXT_IP(Registers));
    }
}

/* InterruptDisable
 * Disables interrupts and returns
 * the state before disabling */
IntStatus_t
InterruptDisable(void)
{
    // Variables
    IntStatus_t CurrentState;

    // Save status
    CurrentState = InterruptSaveState();

    // Disable interrupts and return
    __cli();
    return CurrentState;
}

/* InterruptEnable
 * Enables interrupts and returns 
 * the state before enabling */
IntStatus_t
InterruptEnable(void)
{
    // Variables
    IntStatus_t CurrentState;

    // Save current status
    CurrentState = InterruptSaveState();

    // Enable interrupts and return
    __sti();
    return CurrentState;
}

/* InterruptRestoreState
 * Restores the interrupt-status to the given
 * state, that must have been saved from SaveState */
IntStatus_t
InterruptRestoreState(
    _In_ IntStatus_t State)
{
    if (State != 0) {
        return InterruptEnable();
    }
    else {
        return InterruptDisable();
    }
}

/* InterruptSaveState
 * Retrieves the current state of interrupts */
IntStatus_t
InterruptSaveState(void)
{
    if (__getflags() & EFLAGS_INTERRUPT_FLAG) {
        return 1;
    }
    else {
        return 0;
    }
}

/* InterruptIsDisabled
 * Returns 1 if interrupts are currently
 * disabled or 0 if interrupts are enabled */
int
InterruptIsDisabled(void)
{
    /* Just negate this state */
    return !InterruptSaveState();
}
