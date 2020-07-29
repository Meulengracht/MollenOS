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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * X86 PIT (Timer) Driver
 * http://wiki.osdev.org/PIT
 */

#define __MODULE "PIT0"
#define __TRACE

#include <arch/io.h>
#include <hpet.h>
#include <interrupts.h>
#include <timers.h>
#include <debug.h>
#include <string.h>
#include <stdlib.h>
#include "../pit.h"

// Keep static information about the PIT as there is only one instance
static Pit_t PitUnit = { 0 };

InterruptStatus_t
PitInterrupt(
        _In_ InterruptFunctionTable_t*  NotUsed,
        _In_ void*                      Context)
{
    // Unused
    _CRT_UNUSED(NotUsed);
	_CRT_UNUSED(Context);

	// Update stats
	PitUnit.NsCounter += PitUnit.NsTick;
	PitUnit.Ticks++;
    TimersInterrupt(PitUnit.Irq);
	return InterruptHandled;
}

void
PitResetTicks(void)
{
    PitUnit.Ticks = 0;
}

clock_t
PitGetTicks(void)
{
    return PitUnit.Ticks;
}

OsStatus_t
PitInitialize(void)
{
	DeviceInterrupt_t Interrupt = { { 0 } };
	size_t Divisor              = 1193181;

    TRACE("PitInitialize()");

	Interrupt.Line                  = PIT_IRQ;
	Interrupt.Pin                   = INTERRUPT_NONE;
	Interrupt.Vectors[0]            = INTERRUPT_NONE;
	Interrupt.ResourceTable.Handler = PitInterrupt;
	PitUnit.NsTick                  = 1000;

	PitUnit.Irq = InterruptRegister(&Interrupt, INTERRUPT_EXCLUSIVE | INTERRUPT_KERNEL);
	if (PitUnit.Irq != UUID_INVALID) {
		TimersRegisterSystemTimer(PitUnit.Irq, PitUnit.NsTick, PitGetTicks, PitResetTicks);
	}
    else {
        ERROR("Failed to register interrupt");
        return OsError;
    }

    // Detect whether or not we are emulated by the hpet
    if (HpHasLegacyController() == OsSuccess) {
    	// Counter 0 is the IRQ 0 emulator
        HpComparatorStart(0, 1000, 1, Interrupt.Line);
    }
    else {
		// We want a frequncy of 1000 hz
		Divisor /= 1000;
	
		// We use counter 0, select counter 0 and configure it
		WriteDirectIo(DeviceIoPortBased, PIT_IO_BASE + PIT_REGISTER_COMMAND, 1,
			PIT_COMMAND_MODE3 | PIT_COMMAND_FULL | PIT_COMMAND_COUNTER_0);
	
		// Write divisor to the PIT chip
		WriteDirectIo(DeviceIoPortBased, PIT_IO_BASE + PIT_REGISTER_COUNTER0, 1, Divisor & 0xFF);
		WriteDirectIo(DeviceIoPortBased, PIT_IO_BASE + PIT_REGISTER_COUNTER0, 1, (Divisor >> 8) & 0xFF);
    }
	return OsSuccess;
}
