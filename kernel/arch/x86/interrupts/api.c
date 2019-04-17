/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 *   that all sub-layers must conform to
 *
 * - ISA Interrupts should be routed to boot-processor without lowest-prio?
 */
#define __MODULE        "IRQS"
//#define __TRACE

#include <arch/interrupts.h>
#include <machine.h>
#include <debug.h>
#include <arch.h>
#include <apic.h>
#include <pic.h>

#define EFLAGS_INTERRUPT_FLAG        (1 << 9)
#define APIC_FLAGS_DEFAULT            0x7F00000000000000
#define NUM_ISA_INTERRUPTS            16

extern void  __cli(void);
extern void  __sti(void);
extern reg_t __getflags(void);

static uint64_t
InterruptGetApicConfiguration(
    _In_ DeviceInterrupt_t* Interrupt)
{
    uint64_t ApicFlags = APIC_FLAGS_DEFAULT;

    TRACE("InterruptGetApicConfiguration(%i:%i)", Interrupt->Line, Interrupt->Pin);

    // Case 1 - ISA Interrupts 
    // - In most cases are Edge-Triggered, Active-High
    if (Interrupt->Line < NUM_ISA_INTERRUPTS && Interrupt->Pin == INTERRUPT_NONE) {
        int Enabled, LevelTriggered;
        PicGetConfiguration(Interrupt->Line, &Enabled, &LevelTriggered);
        ApicFlags |= 0x100;                    // Lowest Priority
        ApicFlags |= 0x800;                    // Logical Destination Mode
        
        // Configure as level triggered if requested by interrupt flags
        // Ignore polarity mode as that is automatically treated as active low
        // when trigger is set to level
        if (Interrupt->AcpiConform & __DEVICEMANAGER_ACPICONFORM_TRIGGERMODE) {
            LevelTriggered = 1;
        }

        if (LevelTriggered == 1) {
            TRACE(" > isa peripheral interrupt (active-low, level-triggered)");
            ApicFlags |= APIC_ACTIVE_LOW;           // Set Polarity
            ApicFlags |= APIC_LEVEL_TRIGGER;        // Set Trigger Mode
        }
        else {
            TRACE(" > isa interrupt (active-high, edge-triggered)");
        }
    }
    
    // Case 2 - PCI Interrupts (No-Pin) 
    // - Must be Level Triggered Low-Active
    else if (Interrupt->Line >= NUM_ISA_INTERRUPTS && Interrupt->Pin == INTERRUPT_NONE) {
        TRACE(" > pci interrupt (active-low, level-triggered)");
        ApicFlags |= 0x100;                         // Lowest Priority
        ApicFlags |= 0x800;                         // Logical Destination Mode
        ApicFlags |= APIC_ACTIVE_LOW;               // Set Polarity
        ApicFlags |= APIC_LEVEL_TRIGGER;            // Set Trigger Mode
    }

    // Case 3 - PCI Interrupts (Pin) 
    // - Usually Level Triggered Low-Active
    else if (Interrupt->Pin != INTERRUPT_NONE) {
        // If no routing exists use the pci interrupt line
        if (!(Interrupt->AcpiConform & __DEVICEMANAGER_ACPICONFORM_PRESENT)) {
            TRACE(" > pci interrupt (active-low, level-triggered)");
            ApicFlags |= 0x100;                     // Lowest Priority
            ApicFlags |= 0x800;                     // Logical Destination Mode
        }
        else {
            TRACE(" > pci interrupt (pin-configured - 0x%" PRIxIN ")", Interrupt->AcpiConform);
            ApicFlags |= 0x100;                     // Lowest Priority
            ApicFlags |= 0x800;                     // Logical Destination Mode

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
    return ApicFlags;
}

OsStatus_t
InterruptResolve(
    _In_    DeviceInterrupt_t*  Interrupt,
    _In_    Flags_t             Flags,
    _Out_   UUId_t*             TableIndex)
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
        for (int i = 0; i < GetMachine()->NumberOfOverrides; i++) {
            if (GetMachine()->Overrides[i].SourceLine == Interrupt->Line) {
                Interrupt->Line         = GetMachine()->Overrides[i].DestinationLine;
                Interrupt->AcpiConform  = GetMachine()->Overrides[i].OverrideFlags;
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
        Interrupt->MsiAddress = 0xFEE00000 | (0x0007F0000) | 0x8 | 0x4;

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

OsStatus_t
InterruptConfigure(
    _In_ SystemInterrupt_t* Descriptor,
    _In_ int                Enable)
{
    SystemInterruptController_t *Ic = NULL;
    uint64_t ApicFlags      = APIC_FLAGS_DEFAULT;
    UUId_t TableIndex       = 0;
    union {
        struct {
            uint32_t Lo;
            uint32_t Hi;
        } Parts;
        uint64_t Full;
    } ApicExisting;
    
    // Debug
    TRACE("InterruptConfigure(Id 0x%" PRIxIN ", Enable %i)", Descriptor->Id, Enable);

    // Is this a software interrupt? Don't install
    if (Descriptor->Flags & INTERRUPT_SOFT || 
        Descriptor->Interrupt.Line == INTERRUPT_NONE) {
        return OsSuccess;
    }

    // Are we disabling? Skip configuration
    if (Enable == 0) {
        goto UpdateEntry;
    }

    // Determine the kind of apic configuration
    TableIndex  = (Descriptor->Id & 0xFF);
    ApicFlags   = InterruptGetApicConfiguration(&Descriptor->Interrupt);
    ApicFlags  |= TableIndex;

    // Trace
    TRACE("Calculated flags for interrupt: 0x%" PRIxIN " (TableIndex %" PRIuIN ")", LODWORD(ApicFlags), TableIndex);

    // If this is an (E)ISA interrupt make sure it's configured
    // properly in the PIC/ELCR
    if (Descriptor->Source < NUM_ISA_INTERRUPTS) {
        // ISA Interrupts can be level triggered
        // so make sure we configure it for level triggering
        if (ApicFlags & APIC_LEVEL_TRIGGER) {
            PicConfigureLine(Descriptor->Source, -1, 1);
        }
    }

UpdateEntry:
    if (GetApicInterruptMode() == InterruptModePic) {
        PicConfigureLine(Descriptor->Source, Enable, -1);
    }
    else {
        // If Apic Entry is located, we need to adjust
        Ic = GetInterruptControllerByLine(Descriptor->Source);
        if (Ic != NULL) {
            if (Enable == 0) {
                ApicWriteIoEntry(Ic, Descriptor->Source, APIC_MASKED);
            }
            else {
                ApicExisting.Full = ApicReadIoEntry(Ic, Descriptor->Source);

                // Sanity, we can't just override the existing interrupt vector
                // so if it's already installed, we modify the table-index
                if (!(ApicExisting.Parts.Lo & APIC_MASKED)) {
                    UUId_t ExistingIndex = LOBYTE(LOWORD(ApicExisting.Parts.Lo));
                    if (ExistingIndex != TableIndex) {
                        FATAL(FATAL_SCOPE_KERNEL, "Table index for already installed interrupt: %" PRIuIN "", 
                            TableIndex);
                    }
                }
                else {
                    // Unmask the irq in the io-apic
                    TRACE("Installing source %i => 0x%" PRIxIN "", Descriptor->Source, LODWORD(ApicFlags));
                    ApicWriteIoEntry(Ic, Descriptor->Source, ApicFlags);
                }
            }
        }
        else {
            ERROR("Failed to derive io-apic for source %i", Descriptor->Source);
            return OsError;
        }
    }
    return OsSuccess;
}

IntStatus_t
InterruptDisable(void)
{
    IntStatus_t CurrentState = InterruptSaveState();
    __cli();
    return CurrentState;
}

IntStatus_t
InterruptEnable(void)
{
    IntStatus_t CurrentState = InterruptSaveState();
    __sti();
    return CurrentState;
}

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

int
InterruptIsDisabled(void)
{
    return !InterruptSaveState();
}
