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

#include <interrupts.h>
#include <debug.h>
#include <apic.h>

void
InterruptEntry(
    _In_ Context_t* Registers)
{
    InterruptStatus_t Result     = InterruptNotHandled;
    int               TableIndex = (int)Registers->Irq + 32;
    int               Gsi        = APIC_NO_GSI;

    // Call kernel method
    Result = InterruptHandle(Registers, TableIndex, &Gsi);

    // Sanitize the result of the
    // irq-handling - all irqs must be handled
    if (Result == InterruptNotHandled 
        && InterruptGetIndex(TableIndex) == NULL) {
        // Unhandled interrupts are only ok if spurious
        // LAPIC, Interrupt 7 and 15
        if (TableIndex != INTERRUPT_SPURIOUS
            && TableIndex != (INTERRUPT_PHYSICAL_BASE + 7)
            && TableIndex != (INTERRUPT_PHYSICAL_BASE + 15)) {
            // Fault
            FATAL(FATAL_SCOPE_KERNEL, "Unhandled interrupt %" PRIuIN " (Source %i)", 
                TableIndex, Gsi);
        }
    }
    else {
        // Last action is send eoi
        ApicSendEoi(Gsi, TableIndex);
    }
}
