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

#include <firmware/resources.h>
#include <firmware/interrupt.h>
#include <ddk/interrupt.h>

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

// The final GIC interrupt cell describes whether the signal changes at an
// edge or stays asserted at a level, and whether that signal is high or low.
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
    _In_  const struct FdtResources* provider)
{
    for (size_t i = 0; i < SIZEOF_ARRAY(g_SupportedCompatible); ++i) {
        if (FdtCompatible(&provider->View, g_SupportedCompatible[i])) {
            return 1;
        }
    }
    return 0;
}

// The trigger cell use one of the four supported values this function knows
// how to represent: rising edge, falling edge, level high, or level low.
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
    _In_  const struct FdtResources* provider,
    _In_  const uint8_t*             cells,
    _Out_ struct FdtInterrupt*       interrupt)
{
    uint32_t type;
    uint32_t number;
    uint32_t trigger;

    // This decoder understands the three-cell format used by GIC interrupt
    // descriptions: the interrupt kind, its number, and how its signal is
    // triggered. Other controllers or formats need a different decoder, so
    // reject them rather than interpreting their cells incorrectly.
    if (provider->InterruptCells != 3) {
        return OS_ENOTSUPPORTED;
    }

    // Device Tree stores each cell as a 32-bit big-endian value. Read the
    // values separately so the checks below can describe each part of the
    // interrupt without depending on the machine's byte order.
    type = FdtReadBe32(cells);
    number = FdtReadBe32(cells + 4);
    trigger = FdtReadBe32(cells + 8);

    // This implementation is for the supported Arm GIC controller families,
    // and only accepts a node that firmware identifies as an interrupt
    // controller. This function translates shared peripheral interrupts
    // (SPIs), which are not private to one processor. Processor private
    // interrupts (PPIs) have different routing rules and are not translated
    // here.
    //
    // The number is the SPI number from the Device Tree description, not the
    // final GIC interrupt ID. The GIC reserves IDs 0 through 31 for other
    // purposes, so SPI numbers from 0 through 987 become IDs 32 through 1019.
    if (!__IsSupportedCompatible(provider) || !provider->IsInterruptController ||
        type != FdtGicInterruptTypeSpi || number > 987 || !__IsSupportedTriggerMode(trigger)) {
        return OS_ENOTSUPPORTED;
    }

    // Keep the firmware controller's phandle so the caller can associate
    // this interrupt with the GIC that owns it. Convert the SPI number into
    // the GIC-wide interrupt ID expected by the rest of the system.
    interrupt->Controller = provider->Phandle;
    interrupt->Line = (int)number + 32;

    // These flags describe a real, fixed hardware interrupt line that may be
    // shared. The rest of the interrupt code uses this common flag format for
    // information gathered from different firmware sources.
    interrupt->Flags = 
        INTERRUPT_ACPICONFORM_PRESENT | 
        INTERRUPT_ACPICONFORM_FIXED |
        INTERRUPT_ACPICONFORM_SHAREABLE;

    // In this flag format, trigger mode means level-triggered. Device Tree
    // values 4 and 8 describe level signals; values 1 and 2 describe edge
    // signals and therefore leave this bit clear.
    if (trigger == FdtGicTriggerLevelHigh || trigger == FdtGicTriggerLevelLow) {
        interrupt->Flags |= INTERRUPT_ACPICONFORM_TRIGGERMODE;
    }

    // Polarity means active-low in the common flag format. A falling edge
    // (value 2) and a low level (value 8) are active-low; rising edges and
    // high levels are active-high and leave this bit clear.
    if (trigger == FdtGicTriggerFallingEdge || trigger == FdtGicTriggerLevelLow) {
        interrupt->Flags |= INTERRUPT_ACPICONFORM_POLARITY;
    }
    
    return OS_EOK;
}
