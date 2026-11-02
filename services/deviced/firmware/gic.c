/**
 * Copyright 2026, Philip Meulengracht
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
 */

//#define __TRACE
#include <firmware/resources.h>
#include <firmware/interrupt.h>
#include <ddk/interrupts/interrupt.h>
#include <ddk/utils.h>

// The first GIC interrupt cell identifies whether the interrupt belongs to a
// shared peripheral or is private to a processor.
enum FdtGicInterruptType {
    // An interrupt from a shared peripheral, routed through the GIC.
    FdtGicInterruptTypeSpi = 0,
    // An interrupt private to a processor, with processor-specific routing.
    // This decoder does not handle PPIs because it only translates shared
    // peripheral interrupts.
    FdtGicInterruptTypePpi = 1
};

// The third GIC interrupt cell specifies the trigger mode and polarity.
enum FdtGicTrigger {
    // The signal is active high and interrupts when it changes from low to high.
    FdtGicTriggerRisingEdge = 1,
    // The signal is active low and interrupts when it changes from high to low.
    FdtGicTriggerFallingEdge = 2,
    // The interrupt remains asserted while the signal is high.
    FdtGicTriggerLevelHigh = 4,
    // The interrupt remains asserted while the signal is low.
    FdtGicTriggerLevelLow = 8
};

static const char* g_SupportedCompatible[] = {
    "arm,gic-400",
    "arm,cortex-a15-gic",
    "arm,gic-v3"
};

static int
__IsSupportedCompatible(
    _In_  const struct FdtResources* resources)
{
    for (size_t i = 0; i < SIZEOF_ARRAY(g_SupportedCompatible); ++i) {
        if (FdtCompatible(&resources->View, g_SupportedCompatible[i])) {
            return 1;
        }
    }
    return 0;
}

// Require one supported trigger value. Combinations and additional bits
// cannot be represented by this decoder.
static int
__IsSupportedTriggerMode(
    _In_ int trigger)
{
    switch (trigger) {
        case FdtGicTriggerRisingEdge:
        case FdtGicTriggerFallingEdge:
        case FdtGicTriggerLevelHigh:
        case FdtGicTriggerLevelLow:
            return 1;
        default:
            return 0;
    }
}

oserr_t
FdtGicInterrupt(
    _In_  const struct FdtResources* resources,
    _In_  const uint8_t*             cells,
    _Out_ struct FdtInterrupt*       interrupt)
{
    uint32_t type;
    uint32_t number;
    uint32_t trigger;

    TRACE("FdtGicInterrupt(resources=%u)", resources->Phandle);

    // Require the three-cell GIC format: interrupt type, number, and trigger.
    if (resources->InterruptCells != 3) {
        WARNING("FdtGicInterrupt: resource %u has unsupported cell count %u",
            resources->Phandle, resources->InterruptCells);
        return OS_ENOTSUPPORTED;
    }

    // Device Tree cells are 32-bit big-endian values.
    type = FdtReadBe32(cells);
    number = FdtReadBe32(cells + 4);
    trigger = FdtReadBe32(cells + 8);

    // Require a supported GIC node marked as an interrupt controller.
    if (!__IsSupportedCompatible(resources) || !resources->IsInterruptController) {
        WARNING("FdtGicInterrupt: resource %u is not a supported GIC", resources->Phandle);
        return OS_ENOTSUPPORTED;
    }

    // Only shared peripheral interrupts (SPIs) are supported. SPI numbers
    // 0 through 987 map to GIC interrupt IDs 32 through 1019.
    if (type != FdtGicInterruptTypeSpi || number > 987 || !__IsSupportedTriggerMode(trigger)) {
        WARNING("FdtGicInterrupt: resource %u invalid type %u number %u trigger %u",
            resources->Phandle, type, number, trigger);
        return OS_ENOTSUPPORTED;
    }

    // Preserve the controller phandle and convert the SPI number to a GIC ID.
    // IDs 0 through 31 are reserved for software and processor interrupts.
    interrupt->Controller = resources->Phandle;
    interrupt->Line = (int)number + 32;

    // Use the common firmware flag format for a fixed, shareable hardware line.
    interrupt->Flags =
        INTERRUPT_ACPICONFORM_PRESENT |
        INTERRUPT_ACPICONFORM_FIXED |
        INTERRUPT_ACPICONFORM_SHAREABLE;

    // Set TRIGGERMODE for level signals. Edge signals leave this bit clear.
    if (trigger == FdtGicTriggerLevelHigh || trigger == FdtGicTriggerLevelLow) {
        interrupt->Flags |= INTERRUPT_ACPICONFORM_TRIGGERMODE;
    }

    // Set POLARITY for active-low signals: falling edges and low levels.
    if (trigger == FdtGicTriggerFallingEdge || trigger == FdtGicTriggerLevelLow) {
        interrupt->Flags |= INTERRUPT_ACPICONFORM_POLARITY;
    }

    TRACE("FdtGicInterrupt: controller %u line %i flags 0x%x",
        interrupt->Controller, interrupt->Line, interrupt->Flags);
    return OS_EOK;
}
