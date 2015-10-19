/* MollenOS
*
* Copyright 2011 - 2015, Philip Meulengracht
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
* MollenOS x86-32 Pit Header
*/

/* Includes */
#include <stddef.h>
#include <Timers.h>
#include <Drivers\Timers\Pit\Pit.h>
#include <Interrupts.h>

/* Globals */
volatile uint64_t GlbPitCounter = 0;

/* Externs */
extern void _yield(void);

/* The Pit Handler */
int PitIrqHandler(void *Data)
{
	/* Unused */
	_CRT_UNUSED(Data);

	/* Update Peroidic Tick Counter */
	GlbPitCounter++;

	/* Apply Timer Time (roughly 1 ms) */
	TimersApplyMs(1);

	return X86_IRQ_HANDLED;
}

/* Initializor */
OsStatus_t PitInit(void)
{
	/* We want a frequncy of 1000 hz */
	uint32_t Divisor = (1193181 / 1000);
	IntStatus_t IntrState;

	/* Disable IRQ's for this duration */
	IntrState = InterruptDisable();
	GlbPitCounter = 0;

	/* Install Irq */
	InterruptInstallISA(X86_PIT_IRQ, INTERRUPT_PIT, PitIrqHandler, NULL);

	/* We use counter 0, select counter 0 and configure it */
	outb(X86_PIT_REGISTER_COMMAND, 
		X86_PIT_COMMAND_SQUAREWAVEGEN | 
		X86_PIT_COMMAND_RL_DATA | 
		X86_PIT_COMMAND_COUNTER_0);

	/* Set divisor */
	outb(X86_PIT_REGISTER_COUNTER0, (uint8_t)(Divisor & 0xFF));
	outb(X86_PIT_REGISTER_COUNTER0, (uint8_t)((Divisor >> 8) & 0xFF));

	/* Done, reenable interrupts */
	InterruptRestoreState(IntrState);

	/* Done */
	return OS_STATUS_OK;
}

/* Pit Ticks */
uint64_t PitGetClocks(void)
{
	return GlbPitCounter;
}

/* Sleep for ms */
void PitSleep(uint32_t MilliSeconds)
{
	/* Calculate TickEnd in NanoSeconds */
	uint64_t TickEnd = MilliSeconds + PitGetClocks();

	/* While */
	while (TickEnd >= PitGetClocks())
		_yield();
}

/* Stall for ms */
void PitStall(uint32_t MilliSeconds)
{
	/* Calculate TickEnd in NanoSeconds */
	uint64_t TickEnd = MilliSeconds + PitGetClocks();

	/* While */
	while (TickEnd > PitGetClocks())
		_asm nop;
}