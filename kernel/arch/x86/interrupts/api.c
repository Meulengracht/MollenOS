/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Interrupt Interface
 * - Contains the shared kernel interrupt interface
 *   that all sub-layers must conform to
 *
 * - ISA Interrupts should be routed to boot-processor without lowest-prio?
 */

//#define __TRACE

#include <arch/interrupts.h>
#include <arch/utils.h>
#include <arch/x86/arch.h>
#include <arch/x86/apic.h>
#include <arch/x86/pic.h>
#include <assert.h>
#include <ddk/interrupt.h>
#include <debug.h>
#include <machine.h>

#define EFLAGS_INTERRUPT_FLAG         (1 << 9)
#define APIC_DESTINATION_ALL          0x1
#define APIC_DESTINATION_GROUP(group) (1 << (group))
#define NUM_ISA_INTERRUPTS            16

extern void  __cli(void);
extern void  __sti(void);
extern reg_t __getflags(void);

static inline uuid_t __GetBspCoreId(void)
{
    if (!GetCurrentDomain() || !GetCurrentDomain()->CoreGroup.Cores) {
        return CpuCoreId(GetMachine()->Processor.Cores);
    }
    return CpuCoreId(GetCurrentDomain()->CoreGroup.Cores);
}

static uint64_t __GetApicConfiguration(
    _In_ SystemInterrupt_t* systemInterrupt)
{
    UInteger64_t flags;

    TRACE("__GetApicConfiguration(%i:%i)",
          systemInterrupt->Line, systemInterrupt->Pin);

    // So we could load balance by splitting it up between group 1-7, but for now all to all
    flags.u.LowPart  = APIC_DESTINATION_LOGICAL;
    flags.u.HighPart = APIC_DESTINATION(APIC_DESTINATION_ALL);

    // Case 1 - ISA Interrupts 
    // - In most cases are Edge-Triggered, Active-High
    // - ALL ISA interrupts are going directly to the BSP core
    if (systemInterrupt->Line < NUM_ISA_INTERRUPTS && systemInterrupt->Pin == INTERRUPT_NONE) {
        int Enabled, LevelTriggered;
        uuid_t bspCoreId;

        PicGetConfiguration(systemInterrupt->Line, &Enabled, &LevelTriggered);
        bspCoreId        = __GetBspCoreId();

        // Physical destination, BSP core
        flags.u.LowPart  &= ~(APIC_DESTINATION_LOGICAL);
        flags.u.LowPart  |= APIC_DELIVERY_MODE(APIC_MODE_FIXED);
        flags.u.HighPart = APIC_DESTINATION(bspCoreId); // overwrite
        
        // Configure as level triggered if requested by interrupt flags
        // Ignore polarity mode as that is automatically treated as active low
        // when trigger is set to level
        if (systemInterrupt->AcpiConform & INTERRUPT_ACPICONFORM_TRIGGERMODE) {
            LevelTriggered = 1;
        }

        if (LevelTriggered == 1) {
            TRACE("__GetApicConfiguration isa peripheral interrupt (active-low, level-triggered)");
            flags.u.LowPart |= APIC_ACTIVE_LOW;           // Set Polarity
            flags.u.LowPart |= APIC_LEVEL_TRIGGER;        // Set Trigger Mode
        }
        else {
            TRACE("__GetApicConfiguration isa interrupt (active-high, edge-triggered)");
        }
    }
    
    // Case 2 - PCI Interrupts (No-Pin) 
    // - Must be Level Triggered Low-Active
    // - PCI interrupts go to all cores
    else if (systemInterrupt->Line >= NUM_ISA_INTERRUPTS && systemInterrupt->Pin == INTERRUPT_NONE) {
        TRACE("__GetApicConfiguration pci interrupt (active-low, level-triggered)");
        flags.u.LowPart |= APIC_DELIVERY_MODE(APIC_MODE_LOWEST_PRIORITY);
        flags.u.LowPart |= APIC_ACTIVE_LOW;
        flags.u.LowPart |= APIC_LEVEL_TRIGGER;
    }

    // Case 3 - PCI Interrupts (Pin) 
    // - Usually Level Triggered Low-Active
    else if (systemInterrupt->Pin != INTERRUPT_NONE) {
        // If no routing exists use the pci interrupt line
        if (!(systemInterrupt->AcpiConform & INTERRUPT_ACPICONFORM_PRESENT)) {
            TRACE("__GetApicConfiguration pci interrupt (active-low, level-triggered)");
            flags.u.LowPart |= APIC_DELIVERY_MODE(APIC_MODE_LOWEST_PRIORITY);
        }
        else {
            TRACE("__GetApicConfiguration pci interrupt (pin-configured - 0x%" PRIxIN ")", systemInterrupt->AcpiConform);
            flags.u.LowPart |= APIC_DELIVERY_MODE(APIC_MODE_LOWEST_PRIORITY);

            // Both trigger and polarity is either fixed or set by the
            // information we extracted earlier
            if (systemInterrupt->Line >= NUM_ISA_INTERRUPTS) {
                flags.u.LowPart |= APIC_ACTIVE_LOW;
                flags.u.LowPart |= APIC_LEVEL_TRIGGER;
            }
            else {
                if (systemInterrupt->AcpiConform & INTERRUPT_ACPICONFORM_TRIGGERMODE) {
                    flags.u.LowPart |= APIC_LEVEL_TRIGGER;
                }
                if (systemInterrupt->AcpiConform & INTERRUPT_ACPICONFORM_POLARITY) {
                    flags.u.LowPart |= APIC_ACTIVE_LOW;
                }
            }
        }
    }
    TRACE("__GetApicConfiguration returns=0x%x:0x%x", flags.u.HighPart, flags.u.LowPart);
    return flags.QuadPart;
}

static uuid_t __AllocateSoftwareVector(
    _In_ DeviceInterrupt_t* deviceInterrupt,
    _In_ unsigned int       flags)
{
    uuid_t result = 0;
    
    // Is it fixed?
    if ((flags & INTERRUPT_VECTOR) ||
        deviceInterrupt->Line != INTERRUPT_NONE) {

        result = (uuid_t)deviceInterrupt->Line;

        // Fixed by vector?
        if (flags & INTERRUPT_VECTOR) {
            result = InterruptGetLeastLoaded(deviceInterrupt->Vectors, INTERRUPT_MAXVECTORS);
        }

        // @todo verify proper fixed lines
    }
    else if (deviceInterrupt->Line == INTERRUPT_NONE) {
        int Vectors[INTERRUPT_SOFTWARE_END - INTERRUPT_SOFTWARE_BASE];
        int i;
        for (i = 0; i < (INTERRUPT_SOFTWARE_END - INTERRUPT_SOFTWARE_BASE); i++) {
            Vectors[i] = (INTERRUPT_SOFTWARE_BASE + i);
        }
        result = InterruptGetLeastLoaded(Vectors, i);
    }
    else {
        assert(0);
    }
    return result;
}

oscode_t
InterruptResolve(
        _In_  DeviceInterrupt_t* deviceInterrupt,
        _In_  unsigned int       flags,
        _Out_ uuid_t*            tableIndex)
{
    if (!(flags & (INTERRUPT_SOFT | INTERRUPT_MSI))) {
        if (flags & INTERRUPT_VECTOR) {
            int Vectors[INTERRUPT_PHYSICAL_END - INTERRUPT_PHYSICAL_BASE];
            int i;
            Vectors[INTERRUPT_MAXVECTORS] = INTERRUPT_NONE;
            for (i = 0; i < INTERRUPT_MAXVECTORS; i++) {
                if (deviceInterrupt->Vectors[i] == INTERRUPT_NONE
                    || i == (INTERRUPT_MAXVECTORS - 1)) {
                    Vectors[i] = INTERRUPT_NONE;
                    break;
                }
                Vectors[i] = (INTERRUPT_PHYSICAL_BASE + deviceInterrupt->Vectors[i]);
            }

            deviceInterrupt->Line = InterruptGetLeastLoaded(Vectors, i);

            // Adjust to physical
            if (deviceInterrupt->Line != INTERRUPT_NONE) {
                deviceInterrupt->Line -= INTERRUPT_PHYSICAL_BASE;
            }
        }

        // Do we need to override the source?
        if (deviceInterrupt->Line != INTERRUPT_NONE) {
            // Now lookup in ACPI overrides if we should
            // change the global source
            for (int i = 0; i < GetMachine()->NumberOfOverrides; i++) {
                if (GetMachine()->Overrides[i].SourceLine == deviceInterrupt->Line) {
                    deviceInterrupt->Line        = GetMachine()->Overrides[i].DestinationLine;
                    deviceInterrupt->AcpiConform = GetMachine()->Overrides[i].OverrideFlags;
                }
            }
        }
        *tableIndex = INTERRUPT_PHYSICAL_BASE + (uuid_t)deviceInterrupt->Line;
    }
    else {
        *tableIndex = __AllocateSoftwareVector(deviceInterrupt, flags);
    }

    // In case of MSI interrupt, update msi format
    if (flags & INTERRUPT_MSI) {
        // Fill in MSI data
        // MSI Message Address Register (0xFEE00000 LAPIC)
        // Bits 31-20: Must be 0xFEE
        // Bits 19-11: Destination ID
        // Bits 11-04: Reserved
        // Bit      3: 0 = Destination is ONE CPU, 1 = Destination is Group
        // Bit      2: Destination Mode (1 Logical, 0 Physical)
        // Bits 00-01: X
        deviceInterrupt->MsiAddress = 0xFEE00000 | (0x0007F0000) | 0x8 | 0x4;

        // Message Data Register Format
        // Bits 31-16: Reserved
        // Bit     15: Trigger Mode (1 Level, 0 Edge)
        // Bit     14: If edge, this is not used, if level, 1 = Assert, 0 = Deassert
        // Bits 13-11: Reserved
        // Bits 10-08: Delivery Mode, standard
        // Bits 07-00: Vector
        deviceInterrupt->MsiValue = (0x100 | (*tableIndex & 0xFF));
    }
    return OsOK;
}

void InterruptSetMode(
        _In_ int mode)
{
    // I don't know if we're supposed to be able to switch on the fly. The issue is that we've initialized
    // interrupts before trying to set PIC or APIC mode at acpi. I guess we should assume we can always set
    // APIC mode if there is any APIC present.
    // @todo should we be able to switch interrupt-mode on demand?
    _CRT_UNUSED(mode);
}

oscode_t
InterruptConfigure(
    _In_ SystemInterrupt_t* systemInterrupt,
    _In_ int                enable)
{
    SystemInterruptController_t* ic = NULL;
    uint64_t apicFlags;
    uuid_t   tableIndex;
    union {
        struct {
            uint32_t Lo;
            uint32_t Hi;
        } Parts;
        uint64_t Full;
    }        ApicExisting;
    
    // Debug
    TRACE("InterruptConfigure(Id 0x%" PRIxIN ", Enable %i)", systemInterrupt->Id, enable);

    // Is this a software interrupt? Don't install
    if (systemInterrupt->Flags & (INTERRUPT_SOFT | INTERRUPT_MSI)) {
        return OsOK;
    }

    // Are we disabling? Skip configuration
    if (enable == 0) {
        goto UpdateEntry;
    }

    // Determine the kind of apic configuration
    tableIndex = (systemInterrupt->Id & 0xFF);
    apicFlags  = __GetApicConfiguration(systemInterrupt);
    apicFlags  |= tableIndex;

    // Trace
    TRACE("Calculated flags for interrupt: 0x%" PRIxIN " (TableIndex %" PRIuIN ")", LODWORD(apicFlags), tableIndex);

    // If this is an (E)ISA interrupt make sure it's configured
    // properly in the PIC/ELCR
    if (systemInterrupt->Source < NUM_ISA_INTERRUPTS) {
        // ISA Interrupts can be level triggered
        // so make sure we configure it for level triggering
        if (apicFlags & APIC_LEVEL_TRIGGER) {
            PicConfigureLine(systemInterrupt->Source, -1, 1);
        }
    }

UpdateEntry:
    if (GetApicInterruptMode() == InterruptMode_PIC) {
        PicConfigureLine(systemInterrupt->Source, enable, -1);
    }
    else {
        // If Apic Entry is located, we need to adjust
        ic = GetInterruptControllerByLine(systemInterrupt->Source);
        if (ic != NULL) {
            if (enable == 0) {
                ApicWriteIoEntry(ic, systemInterrupt->Source, APIC_MASKED);
            }
            else {
                ApicExisting.Full = ApicReadIoEntry(ic, systemInterrupt->Source);

                // Sanity, we can't just override the existing interrupt vector
                // so if it's already installed, we modify the table-index
                if (!(ApicExisting.Parts.Lo & APIC_MASKED)) {
                    uuid_t ExistingIndex = LOBYTE(LOWORD(ApicExisting.Parts.Lo));
                    if (ExistingIndex != tableIndex) {
                        FATAL(FATAL_SCOPE_KERNEL, "Table index for already installed interrupt: %" PRIuIN "",
                              tableIndex);
                    }
                }
                else {
                    // Unmask the irq in the io-apic
                    TRACE("Installing source %i => 0x%" PRIxIN "", systemInterrupt->Source, LODWORD(apicFlags));
                    ApicWriteIoEntry(ic, systemInterrupt->Source, apicFlags);
                }
            }
        }
        else {
            ERROR("Failed to derive io-apic for source %i", systemInterrupt->Source);
            return OsError;
        }
    }
    return OsOK;
}

irqstate_t
InterruptDisable(void)
{
    irqstate_t CurrentState = InterruptSaveState();
    __cli();
    return CurrentState;
}

irqstate_t
InterruptEnable(void)
{
    irqstate_t CurrentState = InterruptSaveState();
    __sti();
    return CurrentState;
}

irqstate_t
InterruptRestoreState(
        _In_ irqstate_t State)
{
    if (State != 0) {
        return InterruptEnable();
    }
    else {
        return InterruptDisable();
    }
}

irqstate_t
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


uint32_t
InterruptsGetPriority(void)
{
    return ApicGetTaskPriority();
}

void
InterruptsSetPriority(uint32_t Priority)
{
    ApicSetTaskPriority(Priority);
}

void
InterruptsAcknowledge(int Source, uint32_t TableIndex)
{
    ApicSendEoi(Source, TableIndex);
}
