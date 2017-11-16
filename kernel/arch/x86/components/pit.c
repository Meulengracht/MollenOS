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
 * MollenOS X86 PIT (Timer) Driver
 * http://wiki.osdev.org/PIT
 */
#define __MODULE "PIT0"
#define __TRACE

/* Includes 
 * - System */
#include "../pit.h"
#include <interrupts.h>
#include <timers.h>
#include <debug.h>

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Since there only exists a single pit
 * chip on-board we keep some static information
 * in this driver */
static Pit_t PitUnit;

/* PitInterrupt
 * Calls the timer interface to tick the system tick. */
InterruptStatus_t
PitInterrupt(
    _In_Opt_ void *InterruptData)
{
    // Unused
    _CRT_UNUSED(InterruptData);

	// Update stats
	PitUnit.NsCounter += PitUnit.NsTick;
	PitUnit.Ticks++;
    TimersInterrupt(PitUnit.Irq);
	return InterruptHandled;
}

/* PitGetTicks
 * Retrieves the number of ticks done by the PIT. */
clock_t
PitGetTicks(void)
{
    return PitUnit.Ticks;
}

/* PitInitialize
 * Initializes the PIT unit on the system. */
OsStatus_t
PitInitialize(void)
{
	// Variables
	MCoreInterrupt_t Interrupt = { 0 };
	size_t Divisor = 1193181;

    // Debug
    TRACE("PitInitialize()");

	// Allocate a new instance of the pit-data
	memset(&PitUnit, 0, sizeof(Pit_t));

	// Initialize the interrupt request
	Interrupt.Line = PIT_IRQ;
	Interrupt.Pin = INTERRUPT_NONE;
	Interrupt.Vectors[0] = INTERRUPT_NONE;
	Interrupt.FastHandler = PitInterrupt;
	PitUnit.NsTick = 1000;

	// Install interrupt in system
	// Install a fast interrupt handler
	PitUnit.Irq = InterruptRegister(&Interrupt, INTERRUPT_NOTSHARABLE | INTERRUPT_KERNEL);

	// Register our irq as a system timer
	if (PitUnit.Irq != UUID_INVALID) {
		TimersRegisterSystemTimer(PitUnit.Irq, PitUnit.NsTick, PitGetTicks);
	}
    else {
        ERROR("Failed to register interrupt");
        return OsError;
    }

	// We want a frequncy of 1000 hz
	Divisor /= 1000;

	// We use counter 0, select counter 0 and configure it
	outb(PIT_IO_BASE + PIT_REGISTER_COMMAND,
		PIT_COMMAND_MODE3 | PIT_COMMAND_FULL |
		PIT_COMMAND_COUNTER_0);

	// Write divisor to the PIT chip
	outb(PIT_IO_BASE + PIT_REGISTER_COUNTER0,
		(uint8_t)(Divisor & 0xFF));
	outb(PIT_IO_BASE + PIT_REGISTER_COUNTER0,
		(uint8_t)((Divisor >> 8) & 0xFF));
	return OsSuccess;
}
