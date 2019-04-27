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
 * X86 Advanced Programmable Interrupt Controller Driver
 *  - Initialization code for boot/ap cpus
 */
#define __MODULE "APIC"
#define __TRACE

#include <ds/collection.h>
#include <component/domain.h>
#include <component/cpu.h>
#include <arch/interrupts.h>
#include <arch/utils.h>
#include <arch/io.h>
#include <acpiinterface.h>
#include <interrupts.h>
#include <memoryspace.h>
#include <machine.h>
#include <string.h>
#include <timers.h>
#include <debug.h>
#include <apic.h>
#include <heap.h>
#include <mp.h>

extern void _rdmsr(size_t Register, uint64_t *Value);

static SystemInterruptController_t* IoApicI8259Apic = NULL;
static int                          IoApicI8259Pin  = 0;
static SystemInterruptMode_t        InterruptMode   = InterruptModePic;

size_t    GlbTimerQuantum  = APIC_DEFAULT_QUANTUM;
uintptr_t GlbLocalApicBase = 0;

/* GetSystemLvtByAcpi
 * Retrieves lvt setup for the calling cpu and the given Lvt index. */
static Flags_t
GetSystemLvtByAcpi(
    _In_ uint8_t                        Lvt)
{
    ACPI_TABLE_HEADER *Header     = NULL;
    Flags_t LvtSetup             = 0;

    // Check for MADT presence and enumerate
    if (AcpiAvailable() == ACPI_AVAILABLE && ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_MADT, 0, &Header))) {
        ACPI_SUBTABLE_HEADER *MadtEntry = NULL;
        ACPI_TABLE_MADT *MadtTable  = (ACPI_TABLE_MADT*)Header;
        void *MadtTableStart        = (void*)((uintptr_t)MadtTable + sizeof(ACPI_TABLE_MADT));
        void *MadtTableEnd          = (void*)((uintptr_t)MadtTable + MadtTable->Header.Length);
        for (MadtEntry = (ACPI_SUBTABLE_HEADER*)MadtTableStart; (void *)MadtEntry < MadtTableEnd;) {
            if (MadtEntry->Length < sizeof(ACPI_SUBTABLE_HEADER)) {
                break;
            }
            switch (MadtEntry->Type) {
                case ACPI_MADT_TYPE_LOCAL_APIC_NMI: {
                    ACPI_MADT_LOCAL_APIC_NMI *Nmi = (ACPI_MADT_LOCAL_APIC_NMI*)MadtEntry;
                    if (Nmi->ProcessorId == 0xFF || Nmi->ProcessorId == ArchGetProcessorCoreId()) {
                        if (Nmi->Lint == Lvt) {
                            LvtSetup = APIC_NMI_ROUTE;
                            LvtSetup |= (AcpiGetPolarityMode(Nmi->IntiFlags, 0) << 13);
                            LvtSetup |= (AcpiGetTriggerMode(Nmi->IntiFlags, 0) << 15);
                            break;
                        }
                    }
                } break;

                case ACPI_MADT_TYPE_LOCAL_X2APIC_NMI: {
                    WARNING("implement support for x2-nmi");
                } break;

                default: break;
            }
            MadtEntry = (ACPI_SUBTABLE_HEADER*)
                ACPI_ADD_PTR(ACPI_SUBTABLE_HEADER, MadtEntry, MadtEntry->Length);
        }
        AcpiPutTable((ACPI_TABLE_HEADER*)MadtTable);
    }
    return LvtSetup;
}

/* InitializeApicLvt
 * Initializes the given lvt entry by checking acpi tables. If setup is not
 * specified in there, sane default values will be set. */
static void
InitializeApicLvt(
    _In_ uint8_t                        Lvt)
{
    // Variables
    uint32_t Temp = GetSystemLvtByAcpi(Lvt);

    // Sanity - LVT 0 Default Settings
    // Always set to EXTINT and level trigger
    if (Temp == 0 && Lvt == 0) {
        Temp = APIC_EXTINT_ROUTE | APIC_LEVEL_TRIGGER;
    }

    // Sanity - LVT 1 Default Settings
    // They can be dependant on whether or not the apic is integrated or a seperated chip
    if (Temp == 0 && Lvt == 1) {
        Temp = APIC_NMI_ROUTE;
        if (!ApicIsIntegrated()) {
            Temp |= APIC_LEVEL_TRIGGER;
        }
    }

    // Disable the lvt entry for all other than the boot processor
    if (ArchGetProcessorCoreId() != GetMachine()->Processor.PrimaryCore.Id) {
        Temp |= APIC_MASKED;
    }
    ApicWriteLocal(APIC_LINT0_REGISTER + (Lvt * 0x10), Temp);
}

/* This code initializes an io-apic, looks for the 
 * 8259 pin, clears out interrupts and makes sure
 * interrupts are masked */
void 
ParseIoApic(
    _In_ SystemInterruptController_t*   Controller)
{
    uintptr_t Original, Updated;
    int IoEntries, i, j;

    // Debug
    TRACE(" > initialing io-apic %" PRIuIN "", Controller->Id);

    // Relocate the io-apic
    Original = Controller->MemoryAddress;
    CreateMemorySpaceMapping(GetCurrentMemorySpace(), &Original, &Updated, GetMemorySpacePageSize(), 
        MAPPING_COMMIT | MAPPING_NOCACHE | MAPPING_PERSISTENT, 
        MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_GLOBAL, __MASK);
    Controller->MemoryAddress = Updated + (Original & 0xFFF);

    /* Maximum Redirection Entry - RO. This field contains the entry number (0 being the lowest
     * entry) of the highest entry in the I/O Redirection Table. The value is equal to the number of
     * interrupt input pins for the IOAPIC minus one. The range of values is 0 through 239. */
    IoEntries = ApicIoRead(Controller, 1);
    IoEntries >>= 16;
    IoEntries &= 0xFF;
    Controller->NumberOfInterruptLines = IoEntries + 1;
    TRACE(" > number of interrupt pins: %" PRIiIN "", IoEntries);

    /* Structure of IO Entry Register:
     * Bits 0 - 7: Interrupt Vector that will be raised (Valid ranges are from 0x10 - 0xFE) - Read/Write
     * Bits 8 - 10: Delivery Mode. - Read / Write
     *      - 000: Fixed Delivery, deliver interrupt to all cores listed in destination.
     *      - 001: Lowest Priority, deliver interrupt to a core running lowest priority.
     *      - 010: System Management Interrupt, must be edge triggered.
     *      - 011: Reserved
     *      - 100: NMI, deliver the interrupt to NMI signal of all cores, must be edge triggered.
     *      - 101: INIT, deliver the signal to all cores by asserting init signal
     *      - 110: Reserved
     *      - 111: ExtINT, Like fixed, requires edge triggered.
     * Bit 11: Destination Mode, determines how the destination is interpreted. 0 means
     *                           phyiscal mode (we use apic id), 1 means logical mode (we use set of processors).
     * Bit 12: Delivery Status of the interrupt, read only. 0 = IDLE, 1 = Send Pending
     * Bit 13: Interrupt Pin Polarity, Read/Write, 0 = High active, 1 = Low active
     * Bit 14: Remote IRR, read only. it is set to 0 when EOI has been recieved for that interrupt
     * Bit 15: Trigger Mode, read / write, 1 = Level sensitive, 0 = Edge sensitive.
     * Bit 16: Interrupt Mask, read / write, 1 = Masked, 0 = Unmasked.
     * Bits 17 - 55: Reserved
     * Bits 56 - 63: Destination Field, if destination mode is physical, bits 56:59 should contain
     *                                   an apic id. If it is logical, bits 56:63 defines a set of
     *                                   processors that is the destination
     */

    // Step 1 - find the i8259 connection
    for (i = 0; i <= IoEntries; i++) {
        uint64_t Entry = ApicReadIoEntry(Controller, i);

        // Unmasked and ExtINT? 
        // - Then we found it, and should lock the interrupt route
        if ((Entry & (APIC_MASKED | APIC_EXTINT_ROUTE)) == APIC_EXTINT_ROUTE) {
            IoApicI8259Apic = Controller;
            IoApicI8259Pin  = i;
            InterruptIncreasePenalty(i);
            break;
        }
    }

    /* Now clear interrupts */
    for (i = Controller->InterruptLineBase, j = 0; j <= IoEntries; i++, j++) {
        uint64_t Entry = ApicReadIoEntry(Controller, j);

        /* Sanitize the entry
         * We do NOT want to clear the SMI 
         * and if it's an ISA we want to disable
         * it for allocation */
        if (Entry & APIC_SMI_ROUTE) {
            if (j < 16) {
                InterruptIncreasePenalty(i);
            }
            continue;
        }

        /* Make sure entry is masked */
        if (!(Entry & APIC_MASKED)) {
            Entry |= APIC_MASKED;
            ApicWriteIoEntry(Controller, j, Entry);
            Entry = ApicReadIoEntry(Controller, j);
        }

        /* Check if Remote IRR is set 
         * If it has been set we want to clear the 
         * interrupt status for that irq line */
        if (Entry & 0x4000) {
            /* If it's not set to level trigger, we can't clear
             * it, so modify it */
            if (!(Entry & APIC_LEVEL_TRIGGER)) {
                Entry |= APIC_LEVEL_TRIGGER;
                ApicWriteIoEntry(Controller, j, Entry);
            }
            ApicSendEoi(j, (uint32_t)(Entry & 0xFF));
        }
        ApicWriteIoEntry(Controller, j, APIC_MASKED);
    }
}

/* Resets the local apic for the current
 * cpu and resets it to sane values, deasserts lines 
 * and clears errors */
void ApicClear(void)
{
    int MaxLvt = 0;
    uint32_t Temp = 0;

    /* Get Max LVT */
    MaxLvt = ApicGetMaxLvt();

    /* Mask error lvt */
    if (MaxLvt >= 3) {
        ApicWriteLocal(APIC_ERROR_REGISTER, INTERRUPT_LVTERROR | APIC_MASKED);
    }

    /* Mask these before deasserting */
    Temp = ApicReadLocal(APIC_TIMER_VECTOR);
    ApicWriteLocal(APIC_TIMER_VECTOR, Temp | APIC_MASKED);
    Temp = ApicReadLocal(APIC_LINT0_REGISTER);
    ApicWriteLocal(APIC_LINT0_REGISTER, Temp | APIC_MASKED);
    Temp = ApicReadLocal(APIC_LINT1_REGISTER);
    ApicWriteLocal(APIC_LINT1_REGISTER, Temp | APIC_MASKED);
    if (MaxLvt >= 4) {
        Temp = ApicReadLocal(APIC_PERF_MONITOR);
        ApicWriteLocal(APIC_PERF_MONITOR, Temp | APIC_MASKED);
    }

    /* Clean out APIC */
    ApicWriteLocal(APIC_TIMER_VECTOR, APIC_MASKED);
    ApicWriteLocal(APIC_LINT0_REGISTER, APIC_MASKED);
    ApicWriteLocal(APIC_LINT1_REGISTER, APIC_MASKED);
    if (MaxLvt >= 3) {
        ApicWriteLocal(APIC_ERROR_REGISTER, APIC_MASKED);
    }
    if (MaxLvt >= 4) {
        ApicWriteLocal(APIC_PERF_MONITOR, APIC_MASKED);
    }

    /* Integrated APIC (!82489DX) ? */
    if (ApicIsIntegrated()) {
        if (MaxLvt > 3) {
            /* Clear ESR due to Pentium errata 3AP and 11AP */
            ApicWriteLocal(APIC_ESR, 0);
        }
        ApicReadLocal(APIC_ESR);
    }
}

/* Basic initializationo of the local apic
 * chip, it resets the apic to a known default state
 * before we try and initialize */
void ApicInitialSetup(UUId_t Cpu)
{
    // Variables
    uint32_t Temp = 0;
    int i = 0, j = 0;

    // Clear apic state and hammer the ESR to 0
    ApicClear();
#if defined(i386) || defined(__i386__)
    if (ApicIsIntegrated()) {
        ApicWriteLocal(APIC_ESR, 0);
        ApicWriteLocal(APIC_ESR, 0);
        ApicWriteLocal(APIC_ESR, 0);
        ApicWriteLocal(APIC_ESR, 0);
    }
#endif
    ApicWriteLocal(APIC_PERF_MONITOR, APIC_NMI_ROUTE);

    // Set the destination to flat and compute a logical index
    ApicWriteLocal(APIC_DEST_FORMAT,  0xFFFFFFFF);
    ApicWriteLocal(APIC_LOGICAL_DEST, APIC_DESTINATION(ApicComputeLogicalDestination(Cpu)));
    ApicSetTaskPriority(0);

    // Last thing is clear status and interrupt registers
    for (i = 8 - 1; i >= 0; i--) {
        Temp = ApicReadLocal(0x100 + i * 0x10);
        for (j = 31; j >= 0; j--) {
            if (Temp & (1 << j)) {
                ApicSendEoi(0, 0);
            }
        }
    }
}

/* Initialization code for the local apic
 * ESR. It clears out the error registers and
 * the ESR register */
void
ApicSetupESR(void)
{
    // Variables
    int MaxLvt      = 0;
    uint32_t Temp   = 0;

    // Sanitize whether or not this
    // is an integrated chip, because if not ESR is not needed
    if (!ApicIsIntegrated()) {
        return;
    }

    // Get the max level of LVT supported
    // on this local apic chip
    MaxLvt = ApicGetMaxLvt();
    if (MaxLvt > 3) {
        ApicWriteLocal(APIC_ESR, 0);
    }
    Temp = ApicReadLocal(APIC_ESR);

    // Enable errors and clear register
    ApicWriteLocal(APIC_ERROR_REGISTER, INTERRUPT_LVTERROR);
    if (MaxLvt > 3) {
        ApicWriteLocal(APIC_ESR, 0);
    }
}

/* ApicEnable
 * Sets the current cpu into apic mode by enabling the apic. */
void
ApicEnable(void)
{
    uint32_t Temp = 0;

    // Enable local apic
    Temp = ApicReadLocal(APIC_SPURIOUS_REG);
    Temp &= ~(0x000FF);
    Temp |= 0x100;

#if defined(i386) || defined(__i386__)
    // This reduces some problems with to fast interrupt mask/unmask
    Temp &= ~(0x200);
#endif

    // Set spurious vector and enable
    Temp |= INTERRUPT_SPURIOUS;
    ApicWriteLocal(APIC_SPURIOUS_REG, Temp);
}

void
ApicStartTimer(
    _In_ size_t Quantum)
{
    ApicWriteLocal(APIC_TIMER_VECTOR,    APIC_TIMER_ONESHOT | INTERRUPT_LAPIC);
    ApicWriteLocal(APIC_DIVIDE_REGISTER, APIC_TIMER_DIVIDER_1);
    ApicWriteLocal(APIC_INITIAL_COUNT,   Quantum);
}

void
ApicInitialize(void)
{
    SystemInterruptController_t* Ic                = NULL;
    ACPI_TABLE_HEADER*           Header            = NULL;
    uint32_t                     TemporaryValue    = 0;
    UUId_t                       BspApicId         = 0;
    uintptr_t                    OriginalApAddress = 0;
    uintptr_t                    UpdatedApAddress  = 0;

    // Step 1. Disable IMCR if present (to-do..) 
    // But the bit that tells us if IMCR is present
    // is located in the MP tables
    WriteDirectIo(DeviceIoPortBased, 0x22, 1, 0x70);
    WriteDirectIo(DeviceIoPortBased, 0x23, 1, 0x1);

    // Step 2. Get the LAPIC base 
    // So we lookup the MADT table if it exists (if it doesn't
    // we should fallback to MP tables)
    if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_MADT, 0, &Header))) {
        ACPI_TABLE_MADT *MadtTable = (ACPI_TABLE_MADT*)Header;
        OriginalApAddress          = MadtTable->Address;
        AcpiPutTable(Header);
    }
    else if (MpInitialize() == OsSuccess) {
        if (MpGetLocalApicAddress(&OriginalApAddress) != OsSuccess) {
            // Fallback to msr
            uint64_t Value = 0;
            _rdmsr(0x1B, &Value);
            OriginalApAddress = (uintptr_t)Value;
        }
    }
    else {
        // Read from msr
        uint64_t Value = 0;
        _rdmsr(0x1B, &Value);
        OriginalApAddress = (uintptr_t)Value;
    }

    // Perform the remap
    TRACE(" > local apic at 0x%" PRIxIN "", OriginalApAddress);
    CreateMemorySpaceMapping(GetCurrentMemorySpace(), &OriginalApAddress, 
        &UpdatedApAddress, GetMemorySpacePageSize(), 
        MAPPING_COMMIT | MAPPING_NOCACHE | MAPPING_PERSISTENT, 
        MAPPING_VIRTUAL_GLOBAL | MAPPING_PHYSICAL_FIXED, __MASK);
    GlbLocalApicBase = UpdatedApAddress + (OriginalApAddress & 0xFFF);
    BspApicId        = (ApicReadLocal(APIC_PROCESSOR_ID) >> 24) & 0xFF;
    TRACE(" > local bsp id %u", BspApicId);

    // Do some initial shared Apic setup
    // for this processor id
    ApicInitialSetup(BspApicId);

    // Actually enable APIC on the
    // boot processor, afterwards
    // we do some more setup
    ApicEnable();

    // Setup LVT0 & LVT1
    InitializeApicLvt(0);
    InitializeApicLvt(1);

    // Do the last shared setup code, which 
    // sets up error registers
    ApicSetupESR();

    // Disable Apic Timer while we setup the io-apics 
    // we need to be careful still
    TemporaryValue = ApicReadLocal(APIC_TIMER_VECTOR);
    TemporaryValue |= (APIC_MASKED | INTERRUPT_LAPIC);
    ApicWriteLocal(APIC_TIMER_VECTOR, TemporaryValue);

    // Set the system into IoApic mode if possible by initializing
    // the io-apics. If the io-apics don't exist, then there are no interrupt
    // controllers and we should instead create it as PIC
    TRACE(" > initializing interrupt controllers");
    Ic = GetMachine()->InterruptController;
    if (Ic != NULL) {
        InterruptMode = InterruptModeApic;
        while (Ic != NULL) {
            ParseIoApic(Ic);
            Ic = Ic->Link;
        }
    }

    // We can now enable the interrupts, as 
    // the IVT table is in place and the local apic
    // has been configured!
    TRACE(" > enabling interrupts");
    InterruptEnable();
    ApicSendEoi(0, 0);
}

SystemInterruptMode_t
GetApicInterruptMode(void)
{
    return InterruptMode;
}

OsStatus_t
ApicIsInitialized(void)
{
    return (GlbLocalApicBase == 0) ? OsError : OsSuccess;
}

void
InitializeLocalApicForApplicationCore(void)
{
    // Perform inital preperations for the APIC
    ApicInitialSetup(ArchGetProcessorCoreId());
    ApicEnable();

    // Setup the LVT channels
    InitializeApicLvt(0);
    InitializeApicLvt(1);

    // Setup the ESR and disable timer
    ApicSetupESR();
    ApicStartTimer(0);
}

void
ApicRecalibrateTimer(void)
{
    volatile clock_t InitialTick = 0;
    volatile clock_t Tick        = 0;
    clock_t          PassedTicks;
    size_t           TimerTicks;

    // Debug
    TRACE("ApicRecalibrateTimer()");

    // Setup initial local apic timer registers
    ApicWriteLocal(APIC_TIMER_VECTOR,    INTERRUPT_LAPIC);
    ApicWriteLocal(APIC_DIVIDE_REGISTER, APIC_TIMER_DIVIDER_1);
    ApicWriteLocal(APIC_INITIAL_COUNT,   0xFFFFFFFF); // Set counter to max, it counts down

    // Sleep for 100 ms
    if (TimersGetSystemTick((clock_t*)&InitialTick) != OsSuccess) {
        FATAL(FATAL_SCOPE_KERNEL, "No system timers are present, can't calibrate APIC");
    }
    while (Tick < (InitialTick + 100)) {
        TimersGetSystemTick((clock_t*)&Tick);
    }
    PassedTicks = Tick - InitialTick;
    
    // Stop counter and calibrate
    ApicWriteLocal(APIC_TIMER_VECTOR, APIC_MASKED);
    TimerTicks = (0xFFFFFFFF - ApicReadLocal(APIC_CURRENT_COUNT));
    TRACE("Bus Speed: %" PRIuIN " Hz", (TimerTicks * 10));
    GlbTimerQuantum = (TimerTicks / PassedTicks) + 1;
    TRACE("Quantum: %" PRIuIN "", GlbTimerQuantum);

    // Start timer for good
    ApicStartTimer(GlbTimerQuantum * 20);
}
