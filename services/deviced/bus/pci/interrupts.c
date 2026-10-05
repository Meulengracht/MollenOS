/**
 * MollenOS
 *
 * Copyright (C) Philip Meulengracht
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

#include "bus.h"

static inline void __UpdateInterruptLine(
    _In_ PciDevice_t* parent,
    _In_ int          bus,
    _In_ int          slot,
    _In_ int          function,
    _In_ int          interruptLine,
    _In_ PciDevice_t* pciDevice)
{
    PciWrite8(parent->Host, (unsigned int)bus, (unsigned int)slot,
              (unsigned int)function, 0x3C, (uint8_t)interruptLine);
    pciDevice->Header->InterruptLine = (uint8_t)interruptLine;
    pciDevice->InterruptLine = interruptLine;
}

static inline int 
__SwizzleInterruptPin(int device, int pin) {
    return (((pin - 1) + device) % 4) + 1;
}

void
PciResolveInterruptLineAndPin(
    _In_ PciDevice_t* parent,
    _In_ int          bus,
    _In_ int          slot,
    _In_ int          function,
    _In_ PciDevice_t* pciDevice)
{
    PciDevice_t* iterator;
    unsigned int pin;
    unsigned int flags;
    int line;

    TRACE("PciResolveInterruptLineAndPin(bus=%i, slot=%i, function=%i)",
          bus, slot, function);

    pciDevice->InterruptLine = pciDevice->Header->InterruptLine;
    if (parent->Host->Operations->ResolveInterrupt != NULL) {
        pciDevice->InterruptLine = INTERRUPT_NONE;
        pin = pciDevice->Header->InterruptPin;
        iterator = pciDevice;
        if (pin == 0 || pin > 4) {
            return;
        }
        while (iterator->Parent != parent->Host->RootDevice) {
            pin = __SwizzleInterruptPin((int)iterator->Slot, (int)pin);
            iterator = iterator->Parent;
        }
        if (parent->Host->Operations->ResolveInterrupt(parent->Host,
                iterator->Bus, iterator->Slot, iterator->Function, pin, &line, &flags) == OS_EOK) {
            pciDevice->InterruptLine = line;
            pciDevice->AcpiConform = flags;
        }
        return;
    }

    // We do need acpi for this to query acpi interrupt information for device
    if (g_acpiAvailable == 1) {
        PciDevice_t* iterator      = pciDevice;
        unsigned int acpiConform   = 0;
        int          interruptLine = pciDevice->Header->InterruptLine;
        int          interruptPin  = pciDevice->Header->InterruptPin;
        oserr_t   hasRouting    = OS_ENOENT;
        TRACE("PciResolveInterruptLineAndPin initial line=%i, pin=%i", interruptLine, interruptPin);

        // Sanitize legals
        if (interruptPin > 4) {
            interruptPin = 1;
        }

        // Does device even use interrupts?
        if (interruptPin != 0) {
            // Swizzle till we reach root
            // Case 1 - Query device for ACPI filter
            //        -> 1.1: It has an routing for our Dev/Pin
            //             -> Exit
            //          -> 1.2: It does not have an routing
            //           -> Swizzle-pin
            //           -> Get parent device
            //           -> Go-To 1
            while (iterator && iterator != parent->Host->RootDevice) {
                hasRouting = AcpiQueryInterrupt(
                        iterator->Bus, iterator->Slot, interruptPin,
                        &interruptLine, &acpiConform);

                // Did routing exist?
                if (hasRouting == OS_EOK) {
                    break;
                }

                // Nope, swizzle pin, move up the ladder
                interruptPin = __SwizzleInterruptPin((int) iterator->Slot, interruptPin);
                iterator     = iterator->Parent;
                TRACE("PciResolveInterruptLineAndPin derived pin %i", interruptPin);
            }

            // Update the irq-line if we found a new line
            if (hasRouting == OS_EOK) {
                TRACE("PciResolveInterruptLineAndPin updating device, line=%i, pin=%i", interruptLine, interruptPin);
                __UpdateInterruptLine(parent, bus, slot, function, interruptLine, pciDevice);
                pciDevice->AcpiConform = acpiConform;
            }
        }
    }
}
