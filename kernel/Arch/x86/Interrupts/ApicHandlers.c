/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS x86-32 Advanced Programmable Interrupt Controller Driver
*/

/* Includes */
#include <Arch.h>
#include <Acpi.h>
#include <Apic.h>
#include <Thread.h>
#include <Threading.h>
#include <Interrupts.h>
#include <Log.h>

#include <stdio.h>

/* Extenrn Vars */
extern volatile uint32_t GlbTimerTicks[64];
extern uint32_t GlbTimerQuantum;

/* Extern Functions */
extern void enter_thread(Registers_t *regs);
extern void RegisterDump(Registers_t *Regs);

/* Primary CPU Timer IRQ */
int ApicTimerHandler(void *Args)
{
	/* Get registers */
	Registers_t *Regs = NULL;
	uint32_t TimeSlice = 20;
	uint32_t TaskPriority = 0;
	Cpu_t CurrCpu = ApicGetCpu();

	/* Increase timer_ticks */
	GlbTimerTicks[CurrCpu]++;

	/* Send EOI */
	ApicSendEoi(0, INTERRUPT_LAPIC);

	/* Switch Task */
	Regs = _ThreadingSwitch((Registers_t*)Args, 1, &TimeSlice, &TaskPriority);

	/* If we just got hold of idle task, well fuck it disable timer 
	 * untill we get another task */
	if (!(ThreadingGetCurrentThread(CurrCpu)->Flags & THREADING_IDLE))
	{
		/* Set Task Priority */
		ApicSetTaskPriority(61 - TaskPriority);

		/* Restart timer */
		ApicWriteLocal(APIC_INITIAL_COUNT, GlbTimerQuantum * TimeSlice);
	}
	else
	{
		/* Set low priority */
		ApicSetTaskPriority(0);

		/* Disable timer */
		ApicWriteLocal(APIC_INITIAL_COUNT, 0);
	}
	
	/* Enter new thread */
	enter_thread(Regs);

	/* Never reached */
	return X86_IRQ_HANDLED;
}

/* Spurious handler */
int ApicSpuriousHandler(void *Args)
{
	/* Unused */
	//Registers_t* mRegs = (Registers_t*)Args;
	_CRT_UNUSED(Args);

	/* Yay, we handled it ... */
	return X86_IRQ_HANDLED;
}

/* Error Handler */
int ApicErrorHandler(void *Args)
{
	/* Unused */
	//Registers_t* mRegs = (Registers_t*)Args;
	_CRT_UNUSED(Args);

	/* Yay, we handled it ... */
	return X86_IRQ_HANDLED;
}
