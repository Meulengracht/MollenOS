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

static _Atomic(uint32_t) g_x86MsiVectorBitmap;

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
            systemInterrupt->ParentLine, systemInterrupt->Pin);

    // So we could load balance by splitting it up between group 1-7, but for now all to all
    flags.u.LowPart  = APIC_DESTINATION_LOGICAL;
    flags.u.HighPart = APIC_DESTINATION(APIC_DESTINATION_ALL);

    // Case 1 - ISA Interrupts 
    // - In most cases are Edge-Triggered, Active-High
    // - ALL ISA interrupts are going directly to the BSP core
    if (systemInterrupt->ParentLine < NUM_ISA_INTERRUPTS && systemInterrupt->Pin == INTERRUPT_NONE) {
        int Enabled, LevelTriggered;
        uuid_t bspCoreId;

        PicGetConfiguration(systemInterrupt->ParentLine, &Enabled, &LevelTriggered);
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
    else if (systemInterrupt->ParentLine >= NUM_ISA_INTERRUPTS && systemInterrupt->Pin == INTERRUPT_NONE) {
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
            flags.u.LowPart |= APIC_ACTIVE_LOW;
            flags.u.LowPart |= APIC_LEVEL_TRIGGER;
        }
        else {
            TRACE("__GetApicConfiguration pci interrupt (pin-configured - 0x%" PRIxIN ")", systemInterrupt->AcpiConform);
            flags.u.LowPart |= APIC_DELIVERY_MODE(APIC_MODE_LOWEST_PRIORITY);

            // Both trigger and polarity is either fixed or set by the
            // information we extracted earlier
            if (systemInterrupt->ParentLine >= NUM_ISA_INTERRUPTS) {
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
    if ((flags & INTERRUPT_VECTOR) || deviceInterrupt->Line != INTERRUPT_NONE) {

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

oserr_t
PlatformMsiAllocate(
    _InOut_ DeviceInterrupt_t* deviceInterrupt)
{
    InterruptMsiRoute_t route = { 0 };
    uint32_t            bit;
    uint32_t            bitmap;
    uint32_t            vector;
    oserr_t             oserr;

    if (deviceInterrupt == NULL) {
        return OS_EINVALPARAMS;
    }

    for (uint32_t slot = 0; slot < (INTERRUPT_SOFTWARE_END - INTERRUPT_SOFTWARE_BASE); slot++) {
        bit = 1U << slot;
        bitmap = atomic_load(&g_x86MsiVectorBitmap);
        while (!(bitmap & bit)) {
            if (atomic_compare_exchange_weak(&g_x86MsiVectorBitmap, &bitmap, bitmap | bit)) {
                break;
            }
        }
        if (bitmap & bit) {
            continue;
        }

        vector = INTERRUPT_SOFTWARE_BASE + slot;
        route.ControllerId = INTERRUPT_MSI_CONTROLLER_X86_LAPIC;
        route.HwIrq = vector;
        route.Index = (uint16_t)vector;
        route.ParentLine = INTERRUPT_NONE;
        oserr = InterruptMsiReserveTableRoute(&route);
        if (oserr != OS_EOK) {
            atomic_fetch_and(&g_x86MsiVectorBitmap, ~bit);
            continue;
        }

        deviceInterrupt->MsiControllerId = route.ControllerId;
        deviceInterrupt->MsiHwIrq = route.HwIrq;
        deviceInterrupt->MsiIndex = route.Index;
        deviceInterrupt->MsiParentLine = route.ParentLine;
        deviceInterrupt->MsiRouteFlags = route.Flags;
        deviceInterrupt->MsiAddress = 0xFEE00000 | (0x0007F000) | 0x8 | 0x4;
        deviceInterrupt->MsiValue = 0x100 | (route.HwIrq & 0xFF);
        return OS_EOK;
    }
    return OS_EOOM;
}

void
PlatformMsiRelease(
    _In_ const InterruptMsiRoute_t* route)
{
    uint32_t slot;
    uint32_t bit;
    uint32_t bitmap;

    if (route == NULL || route->ControllerId != INTERRUPT_MSI_CONTROLLER_X86_LAPIC ||
        route->HwIrq < INTERRUPT_SOFTWARE_BASE || route->HwIrq >= INTERRUPT_SOFTWARE_END ||
        route->Index != route->HwIrq || route->ParentLine != INTERRUPT_NONE) {
        return;
    }

    slot = route->HwIrq - INTERRUPT_SOFTWARE_BASE;
    bit = 1U << slot;
    bitmap = atomic_fetch_and(&g_x86MsiVectorBitmap, ~bit);
    if (!(bitmap & bit)) {
        ERROR("Duplicate x86 MSI route release for vector %u", route->HwIrq);
    }
}

oserr_t
InterruptResolve(
        _In_  DeviceInterrupt_t* deviceInterrupt,
        _In_  unsigned int       flags,
        _Out_ uuid_t*            tableIndex)
{
    if (flags & INTERRUPT_MSI) {
        if (deviceInterrupt->MsiControllerId != INTERRUPT_MSI_CONTROLLER_X86_LAPIC ||
            deviceInterrupt->MsiIndex >= MAX_SUPPORTED_INTERRUPTS) {
            return OS_EINVALPARAMS;
        }
        *tableIndex = deviceInterrupt->MsiIndex;
        return OS_EOK;
    }

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

    return OS_EOK;
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

oserr_t
InterruptConfigure(
    _In_ SystemInterrupt_t* systemInterrupt,
    _In_ int                enable)
{
    SystemInterruptController_t* ic = NULL;
    int      pin;
    uint64_t apicFlags;
    uuid_t   tableIndex;
    
    union {
        struct {
            uint32_t Lo;
            uint32_t Hi;
        } Parts;
        uint64_t Full;
    } ApicExisting;
    
    // Debug
    TRACE("InterruptConfigure(Id 0x%" PRIxIN ", Enable %i)", systemInterrupt->Id, enable);

    // Is this a software interrupt? Don't install
    if (systemInterrupt->Flags & (INTERRUPT_SOFT | INTERRUPT_MSI)) {
        return OS_EOK;
    }

    // Are we disabling? Skip configuration
    if (enable == 0) {
        goto UpdateEntry;
    }

    // Determine the kind of apic configuration
    tableIndex = systemInterrupt->Index;
    apicFlags  = __GetApicConfiguration(systemInterrupt);
    apicFlags  |= tableIndex;

    // Trace
    TRACE("Calculated flags for interrupt: 0x%" PRIxIN " (TableIndex %" PRIuIN ")", LODWORD(apicFlags), tableIndex);

    // If this is an (E)ISA interrupt make sure it's configured
    // properly in the PIC/ELCR
    if (systemInterrupt->ParentLine < NUM_ISA_INTERRUPTS) {
        // ISA Interrupts can be level triggered
        // so make sure we configure it for level triggering
        if (apicFlags & APIC_LEVEL_TRIGGER) {
            PicConfigureLine(systemInterrupt->ParentLine, -1, 1);
        }
    }

UpdateEntry:
    if (GetApicInterruptMode() == InterruptMode_PIC) {
        PicConfigureLine(systemInterrupt->ParentLine, enable, -1);
    }
    else {
        // If Apic Entry is located, we need to adjust
        ic = GetInterruptControllerByLine(systemInterrupt->ParentLine);
        pin = GetPinOffsetByLine(systemInterrupt->ParentLine);
        if (ic != NULL && pin != APIC_NO_GSI) {
            if (enable == 0) {
                ApicWriteIoEntry(ic, pin, APIC_MASKED);
            } else {
                ApicExisting.Full = ApicReadIoEntry(ic, pin);

                // Sanity, we can't just override the existing interrupt vector
                // so if it's already installed, we modify the table-index
                if (!(ApicExisting.Parts.Lo & APIC_MASKED)) {
                    uuid_t ExistingIndex = LOBYTE(LOWORD(ApicExisting.Parts.Lo));
                    if (ExistingIndex != tableIndex) {
                        FATAL(FATAL_SCOPE_KERNEL, "Table index for already installed interrupt: %" PRIuIN "",
                              tableIndex);
                    }
                } else {
                    // Unmask the irq in the io-apic
                    TRACE("Installing source %i => 0x%" PRIxIN "", systemInterrupt->ParentLine, LODWORD(apicFlags));
                    ApicWriteIoEntry(ic, pin, apicFlags);
                }
            }
        } else {
            ERROR("Failed to derive io-apic for source %i", systemInterrupt->ParentLine);
            return OS_EUNKNOWN;
        }
    }
    return OS_EOK;
}

irqstate_t
InterruptDisable(void)
{
    irqstate_t irqState;

    // When we disable or enable interrupts out-right, let's always return
    // previous state to allow the caller to restore a previous state. This
    // is useful for irq-spinlocks.
    irqState = InterruptSaveState();

    // Disable the current interrupt context, and return the previous state.
    __cli();
    return irqState;
}

irqstate_t
InterruptEnable(void)
{
    irqstate_t irqState;

    // When we disable or enable interrupts out-right, let's always return
    // previous state to allow the caller to restore a previous state. This
    // is useful for irq-spinlocks.
    irqState = InterruptSaveState();

    // Enable current state, and return the state before this enable.
    __sti();
    return irqState;
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
