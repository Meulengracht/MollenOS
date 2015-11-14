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
#include <DeviceManager.h>
#include <Devices\Timer.h>
#include <Module.h>
#include "Pit.h"
#include <Heap.h>

/* Structures */
#pragma pack(push, 1)
typedef struct _PitTimer
{
	/* Device Id */
	DevId_t DeviceId;

	/* Divisor Value */
	uint32_t Divisor;

	/* Timer Count */
	uint64_t PitCounter;

} PitTimer_t;
#pragma pack(pop)

/* The Pit Handler */
int PitIrqHandler(void *Data)
{
	/* Cast */
	MCoreTimerDevice_t *Timer = (MCoreTimerDevice_t*)Data;
	PitTimer_t *Pit = (PitTimer_t*)Timer->TimerData;

	/* Update Peroidic Tick Counter */
	Pit->PitCounter++;

	/* Apply Timer Time (roughly 1 ms) */
	Timer->ReportMs(1);

	/* Done */
	return X86_IRQ_HANDLED;
}

/* Entry point of a module */
MODULES_API void ModuleInit(Addr_t *FunctionTable, void *Data)
{
	/* We need these */
	MCoreTimerDevice_t *Timer = NULL;
	PitTimer_t *Pit = NULL;

	/* We want a frequncy of 1000 hz */
	uint32_t Divisor = (1193181 / 1000);
	IntStatus_t IntrState;

	/* Unused */
	_CRT_UNUSED(Data);

	/* Save */
	GlbFunctionTable = FunctionTable;

	/* Allocate */
	Pit = (PitTimer_t*)kmalloc(sizeof(PitTimer_t));
	Timer = (MCoreTimerDevice_t*)kmalloc(sizeof(MCoreTimerDevice_t));

	/* Disable IRQ's for this duration */
	IntrState = InterruptDisable();
	Pit->PitCounter = 0;
	Pit->Divisor = Divisor;

	/* Setup Timer */
	Timer->TimerData = Pit;
	Timer->Sleep = PitSleep;
	Timer->Stall = PitStall;
	Timer->GetTicks = PitGetClocks;

	/* Install Irq */
	InterruptInstallISA(X86_PIT_IRQ, INTERRUPT_PIT, PitIrqHandler, Timer);

	/* We use counter 0, select counter 0 and configure it */
	outb(X86_PIT_REGISTER_COMMAND,
		X86_PIT_COMMAND_SQUAREWAVEGEN |
		X86_PIT_COMMAND_RL_DATA |
		X86_PIT_COMMAND_COUNTER_0);

	/* Set divisor */
	outb(X86_PIT_REGISTER_COUNTER0, (uint8_t)(Divisor & 0xFF));
	outb(X86_PIT_REGISTER_COUNTER0, (uint8_t)((Divisor >> 8) & 0xFF));

	/* Before enabling, register us */
	Pit->DeviceId = DmCreateDevice("PIT Timer", DeviceTimer, Pit);

	/* Done, reenable interrupts */
	InterruptRestoreState(IntrState);
}

/* Pit Ticks */
uint64_t PitGetClocks(void *Data)
{
	/* Cast */
	PitTimer_t *Pit = (PitTimer_t*)Data;

	/* Return Val */
	return Pit->PitCounter;
}

/* Sleep for ms */
void PitSleep(void *Data, uint32_t MilliSeconds)
{
	/* Calculate TickEnd in NanoSeconds */
	uint64_t TickEnd = MilliSeconds + PitGetClocks(Data);

	/* While */
	while (TickEnd >= PitGetClocks(Data))
		_ThreadYield();
}

/* Stall for ms */
void PitStall(void *Data, uint32_t MilliSeconds)
{
	/* Calculate TickEnd in NanoSeconds */
	uint64_t TickEnd = MilliSeconds + PitGetClocks(Data);

	/* While */
	while (TickEnd > PitGetClocks(Data))
		_asm nop;
}