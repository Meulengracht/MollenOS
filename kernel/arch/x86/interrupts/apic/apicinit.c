/**
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
 * X86 Advanced Programmable Interrupt Controller Driver
 *  - Initialization code for boot/ap cpus
 */
#define __MODULE "APIC"
#define __TRACE

#include <component/cpu.h>
#include <arch/interrupts.h>
#include <arch/utils.h>
#include <arch/io.h>
#include <arch/x86/arch.h>
#include <arch/x86/cpu.h>
#include <arch/x86/apic.h>
#include <arch/x86/mp.h>
#include <acpiinterface.h>
#include <interrupts.h>
#include <memoryspace.h>
#include <machine.h>
#include <debug.h>

static SystemInterruptController_t* g_ioApicI8259Apic = NULL;
static int                          g_ioApicI8259Pin = 0;
static InterruptMode_t              g_interruptMode  = InterruptMode_PIC;

uintptr_t g_localApicBaseAddress = 0;

static unsigned int
__GetSystemLvtByAcpi(
    _In_ uint8_t lvt)
{
    ACPI_TABLE_HEADER* header = NULL;
    unsigned int       lvtSetup = 0;

    // Check for MADT presence and enumerate
    if (AcpiAvailable() == ACPI_AVAILABLE && 
        ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_MADT, 0, &header))) {
        ACPI_SUBTABLE_HEADER *MadtEntry = NULL;
        ACPI_TABLE_MADT *MadtTable  = (ACPI_TABLE_MADT*)header;
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
                        if (Nmi->Lint == lvt) {
                            lvtSetup = APIC_NMI_ROUTE;
                            lvtSetup |= (AcpiGetPolarityMode(Nmi->IntiFlags, 0) << 13);
                            lvtSetup |= (AcpiGetTriggerMode(Nmi->IntiFlags, 0) << 15);
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
    return lvtSetup;
}

/**
 * @brief Initializes the given lvt entry by checking acpi tables. If setup is not
 * specified in there, sane default values will be set.
 *
 * @param[In] lvt The lvt to initialize
 */
static void
__InitializeLvt(
    _In_ uint8_t lvt)
{
    uint32_t temp = __GetSystemLvtByAcpi(lvt);

    // Sanity - LVT 0 Default Settings
    // Always set to EXTINT and level trigger
    if (temp == 0 && lvt == 0) {
        temp = APIC_EXTINT_ROUTE | APIC_LEVEL_TRIGGER;
    }

    // Sanity - LVT 1 Default Settings
    // They can be dependent on whether the apic is integrated or a seperated chip
    if (temp == 0 && lvt == 1) {
        temp = APIC_NMI_ROUTE;
        if (!ApicIsIntegrated()) {
            temp |= APIC_LEVEL_TRIGGER;
        }
    }

    // Disable the lvt entry for all other than the boot processor
    if (ArchGetProcessorCoreId() != CpuCoreId(GetMachine()->Processor.Cores)) {
        temp |= APIC_MASKED;
    }
    ApicWriteLocal(APIC_LINT0_REGISTER + (lvt * 0x10), temp);
}

/**
 * @brief initializes an io-apic, looks for the 8259 pin, clears out interrupts and makes sure
 * interrupts are masked
 *
 * @param[In] ioApic The io-apic controller that should be initialized.
 */
void 
__InitializeIoApic(
    _In_ SystemInterruptController_t* ioApic)
{
    uintptr_t originalAddress, remappedAddress;
    uint32_t  ioEntryCount;
    int       i, j;
    oserr_t   oserr;
    TRACE("__InitializeIoApic(ioApic=%" PRIuIN ")", ioApic->Id);

    // Relocate the io-apic
    originalAddress = ioApic->MemoryAddress;
    oserr = MemorySpaceMap(
            GetCurrentMemorySpace(),
            &(struct MemorySpaceMapOptions) {
                .Pages = &originalAddress,
                .Length = GetMemorySpacePageSize(),
                .Mask = MEMORY_MASK_32BIT,
                .Flags = MAPPING_COMMIT | MAPPING_NOCACHE | MAPPING_PERSISTENT,
                .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_GLOBAL
            },
            &remappedAddress
    );
    if (oserr != OS_EOK) {
        ERROR("__InitializeIoApic cannot map address of io-apic");
        return;
    }
    ioApic->MemoryAddress = remappedAddress + (originalAddress & 0xFFF);

    /**
     * Maximum Redirection Entry - RO. This field contains the entry number (0 being the lowest
     * entry) of the highest entry in the I/O Redirection Table. The value is equal to the number of
     * interrupt input pins for the IOAPIC minus one. The range of values is 0 through 239.
     */
    ioEntryCount = ApicIoRead(ioApic, 1);
    ioEntryCount >>= 16;
    ioEntryCount &= 0xFF;
    ioApic->NumberOfInterruptLines = (int)(ioEntryCount + 1);
    TRACE("__InitializeIoApic number of interrupt pins: %" PRIiIN "", ioEntryCount);

    /**
     * Structure of IO Entry Register:
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
    for (i = 0; i <= ioEntryCount; i++) {
        uint64_t Entry = ApicReadIoEntry(ioApic, i);

        // Unmasked and ExtINT? 
        // - Then we found it, and should lock the interrupt route
        if ((Entry & (APIC_MASKED | APIC_EXTINT_ROUTE)) == APIC_EXTINT_ROUTE) {
            g_ioApicI8259Apic = ioApic;
            g_ioApicI8259Pin  = i;
            InterruptIncreasePenalty(i);
            break;
        }
    }

    // Next step is to clear interrupts for the io-apic
    for (i = ioApic->InterruptLineBase, j = 0; j <= ioEntryCount; i++, j++) {
        uint64_t Entry = ApicReadIoEntry(ioApic, j);

        /* Sanitize the entry
         * We do NOT want to clear the SMI and if it's an ISA we want to disable
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
            ApicWriteIoEntry(ioApic, j, Entry);
            Entry = ApicReadIoEntry(ioApic, j);
        }

        /* Check if Remote IRR is set 
         * If it has been set we want to clear the 
         * interrupt status for that irq line */
        if (Entry & 0x4000) {
            /* If it's not set to level trigger, we can't clear
             * it, so modify it */
            if (!(Entry & APIC_LEVEL_TRIGGER)) {
                Entry |= APIC_LEVEL_TRIGGER;
                ApicWriteIoEntry(ioApic, j, Entry);
            }
            ApicSendEoi(j, (uint32_t)(Entry & 0xFF));
        }
        ApicWriteIoEntry(ioApic, j, APIC_MASKED);
    }
}

static void
__ClearApic(void)
{
    int      maxLvt = ApicGetMaxLvt();
    uint32_t temp;

    // Work around AMD Erratum 411
    ApicWriteLocal(APIC_INITIAL_COUNT, 0);

    //  Masking an LVT entry on a P6 can trigger a local APIC error
    // if the vector is zero. Mask LVTERR first to prevent this.
    if (maxLvt >= 3) {
        ApicWriteLocal(APIC_ERROR_REGISTER, INTERRUPT_LVTERROR | APIC_MASKED);
    }

    // Carefully mask these before deactivating
    temp = ApicReadLocal(APIC_TIMER_VECTOR);
    ApicWriteLocal(APIC_TIMER_VECTOR, temp | APIC_MASKED);
    temp = ApicReadLocal(APIC_LINT0_REGISTER);
    ApicWriteLocal(APIC_LINT0_REGISTER, temp | APIC_MASKED);
    temp = ApicReadLocal(APIC_LINT1_REGISTER);
    ApicWriteLocal(APIC_LINT1_REGISTER, temp | APIC_MASKED);
    if (maxLvt >= 4) {
        temp = ApicReadLocal(APIC_PERF_MONITOR);
        ApicWriteLocal(APIC_PERF_MONITOR, temp | APIC_MASKED);
    }

    // Don't touch this untill further notice
#if 0
    if (MaxLvt >= 5) {
        Temp = ApicReadLocal(APIC_THERMAL_SENSOR);
        ApicWriteLocal(APIC_THERMAL_SENSOR, Temp | APIC_MASKED);
    }
#endif

    if (maxLvt >= 6) {
        temp = ApicReadLocal(APIC_CMCI);
        ApicWriteLocal(APIC_CMCI, temp | APIC_MASKED);
    }

    // Clean out apic states
    ApicWriteLocal(APIC_TIMER_VECTOR, APIC_MASKED);
    ApicWriteLocal(APIC_LINT0_REGISTER, APIC_MASKED);
    ApicWriteLocal(APIC_LINT1_REGISTER, APIC_MASKED);

    if (maxLvt >= 3) {
        ApicWriteLocal(APIC_ERROR_REGISTER, APIC_MASKED);
    }

    if (maxLvt >= 4) {
        ApicWriteLocal(APIC_PERF_MONITOR, APIC_MASKED);
    }

#if 0
    if (MaxLvt >= 5) {
        ApicWriteLocal(APIC_THERMAL_SENSOR, APIC_MASKED);
    }
#endif

    if (maxLvt >= 6) {
        ApicWriteLocal(APIC_CMCI, APIC_MASKED);
    }

    // Integrated APIC (!82489DX) ?
    if (ApicIsIntegrated()) {
        // Clear ESR due to Pentium errata 3AP and 11AP
        if (maxLvt > 3) {
            ApicWriteLocal(APIC_ESR, 0);
        }
        ApicReadLocal(APIC_ESR);
    }
}

static void
__PrepareApic(
        _In_ uuid_t coreId)
{
    uint32_t temp;
    int      i, j;

    // Clear apic state and hammer the ESR to 0
    __ClearApic();

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
    ApicWriteLocal(APIC_LOGICAL_DEST, APIC_DESTINATION(ApicComputeLogicalDestination(coreId)));
    ApicSetTaskPriority(0);

    // Last thing is clear status and interrupt registers
    for (i = 8 - 1; i >= 0; i--) {
        temp = ApicReadLocal(0x100 + i * 0x10);
        for (j = 31; j >= 0; j--) {
            if (temp & (1 << j)) {
                ApicSendEoi(0, 0);
            }
        }
    }
}

/**
 * @brief Initializes error registers and the ESR
 */
static void
__InitializeESR(void)
{
    int maxLvt;

    // Sanitize whether this is an integrated chip, because if not ESR is not needed
    if (!ApicIsIntegrated()) {
        return;
    }

    // Get the max level of LVT supported
    // on this local apic chip
    maxLvt = ApicGetMaxLvt();
    if (maxLvt > 3) {
        ApicWriteLocal(APIC_ESR, 0);
    }
    (void)ApicReadLocal(APIC_ESR);

    // Enable errors and clear register
    ApicWriteLocal(APIC_ERROR_REGISTER, INTERRUPT_LVTERROR);
    if (maxLvt > 3) {
        ApicWriteLocal(APIC_ESR, 0);
    }
}

static void
__EnableApic(void)
{
    uint32_t temp;

    // Enable local apic
    temp = ApicReadLocal(APIC_SPURIOUS_REG);
    temp &= ~(0x000FF);
    temp |= 0x100; // Enable Apic
#if defined(i386) || defined(__i386__)
    // This bit is reserved on P4/Xeon and should be cleared
    temp &= ~(0x200);
#endif
    temp |= INTERRUPT_SPURIOUS;
    ApicWriteLocal(APIC_SPURIOUS_REG, temp);
}

void
ApicInitialize(void)
{
    SystemInterruptController_t* ic;
    ACPI_TABLE_HEADER*           header = NULL;
    uintptr_t originalApAddress = 0;
    uintptr_t remappedApAddress = 0;
    uuid_t    bspApicId;
    uint32_t  temporaryValue;
    oserr_t   oserr;
    TRACE("ApicInitialize()");

    // Step 1. Disable IMCR if present (to-do)
    // But the bit that tells us if IMCR is present
    // is located in the MP tables
    WriteDirectIo(DeviceIoPortBased, 0x22, 1, 0x70);
    WriteDirectIo(DeviceIoPortBased, 0x23, 1, 0x1);

    // Step 2. Get the LAPIC base 
    // So we look up the MADT table if it exists (if it doesn't
    // we should fall back to MP tables)
    if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_MADT, 0, &header))) {
        ACPI_TABLE_MADT *MadtTable = (ACPI_TABLE_MADT*)header;
        originalApAddress = MadtTable->Address;
        AcpiPutTable(header);
    }
    else if (MpInitialize() == OS_EOK) {
        if (MpGetLocalApicAddress(&originalApAddress) != OS_EOK) {
            // Fallback to msr
            uint64_t Value = 0;
            CpuReadModelRegister(CPU_MSR_LAPIC_BASE, &Value);
            originalApAddress = (uintptr_t)Value;
        }
    }
    else {
        // Read from msr
        uint64_t Value = 0;
        CpuReadModelRegister(CPU_MSR_LAPIC_BASE, &Value);
        originalApAddress = (uintptr_t)Value;
    }

    // Perform the remap
    TRACE("ApicInitialize local apic at 0x%" PRIxIN "", originalApAddress);
    oserr = MemorySpaceMap(
            GetCurrentMemorySpace(),
            &(struct MemorySpaceMapOptions) {
                .Pages = &originalApAddress,
                .Length = GetMemorySpacePageSize(),
                .Mask = MEMORY_MASK_32BIT,
                .Flags = MAPPING_COMMIT | MAPPING_NOCACHE | MAPPING_PERSISTENT,
                .PlacementFlags = MAPPING_VIRTUAL_GLOBAL | MAPPING_PHYSICAL_FIXED
            },
            &remappedApAddress
    );
    if (oserr != OS_EOK) {
        ERROR("ApicInitialize cannot map the local apic");
        return;
    }
    g_localApicBaseAddress = remappedApAddress + (originalApAddress & 0xFFF);
    bspApicId              = (ApicReadLocal(APIC_PROCESSOR_ID) >> 24) & 0xFF;
    TRACE("ApicInitialize local bsp id %u", bspApicId);

    // Initialize and enable the local apic for the processor
    __PrepareApic(bspApicId);
    __EnableApic();
    __InitializeLvt(0);
    __InitializeLvt(1);
    __InitializeESR();

    // Disable Apic Timer while we set up the io-apics
    // we need to be careful still
    temporaryValue = ApicReadLocal(APIC_TIMER_VECTOR);
    temporaryValue |= (APIC_MASKED | INTERRUPT_LAPIC);
    ApicWriteLocal(APIC_TIMER_VECTOR, temporaryValue);

    // Set the system into IoApic mode if possible by initializing
    // the io-apics. If the io-apics don't exist, then there are no interrupt
    // controllers, and we should instead create it as PIC
    TRACE("ApicInitialize initializing interrupt controllers");
    ic = GetMachine()->InterruptController;
    if (ic) {
        g_interruptMode = InterruptMode_APIC;
        while (ic) {
            __InitializeIoApic(ic);
            ic = ic->Link;
        }
    }

    // We can now enable the interrupts, as 
    // the IVT table is in place and the local apic
    // has been configured!
    TRACE("ApicInitialize enabling interrupts");
    InterruptEnable();
    ApicSendEoi(0, 0);
}

InterruptMode_t
GetApicInterruptMode(void)
{
    return g_interruptMode;
}

oserr_t
ApicIsInitialized(void)
{
    return (g_localApicBaseAddress == 0) ? OS_ENOTSUPPORTED : OS_EOK;
}

void
ApicInitializeForApplicationCore(void)
{
    // Perform inital preperations for the APIC
    __PrepareApic(ArchGetProcessorCoreId());
    __EnableApic();

    // Set up the LVT channels
    __InitializeLvt(0);
    __InitializeLvt(1);

    // Set up the ESR and disable timer
    __InitializeESR();
    ApicTimerStart(0);
}
