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
 * MollenOS x86 Advanced Programmable Interrupt Controller Driver
 * - Interrupt Handlers specific for the APIC
 */

/* Includes 
 * - System */
#include <Arch.h>
#include <Acpi.h>
#include <Apic.h>
#include <Thread.h>
#include <Threading.h>
#include <Interrupts.h>
#include <Log.h>

/* Externs 
 * We need access to a few of the variables
 * in the other apic files */
__CRT_EXTERN volatile size_t GlbTimerTicks[64];
__CRT_EXTERN size_t GlbTimerQuantum;

/* Extern to our assembly function
 * it loads the new task context */
__CRT_EXTERN void enter_thread(Registers_t *Regs);

/* The primary interrupt code for switching tasks
 * and is controlled by the apic timer, initially the
 * apic timer is loaded by a configurable quantum
 * but should be calibrated as soon as a reliable timer
 * source is registered. */
int ApicTimerHandler(void *Args)
{
	/* Variables we will need for loading
	 * a new task */
	Registers_t *Regs = NULL;
	Cpu_t CurrCpu = ApicGetCpu();

	/* These will be assigned from the 
	 * _switch function, but set them in
	 * case threading is not initialized yet */
	size_t TimeSlice = 20;
	int TaskPriority = 0;

	/* Increase timer_ticks for the current
	 * cpu core, I'm not so sure we actually use this atm.. */
	GlbTimerTicks[CurrCpu]++;

	/* Before we do anything, send EOI so 
	 * we don't forget :-) */
	ApicSendEoi(0, INTERRUPT_LAPIC);

	/* Switch Task, if there is no threading enabled yet
	 * it should return the same structure as we give */
	Regs = _ThreadingSwitch((Registers_t*)Args, 1, &TimeSlice, &TaskPriority);

	/* If we just got hold of idle task, well fuck it disable timer 
	 * untill we get another task */
	if (!ThreadingIsCurrentTaskIdle(CurrCpu)) {
		ApicSetTaskPriority(61 - TaskPriority);
		ApicWriteLocal(APIC_INITIAL_COUNT, GlbTimerQuantum * TimeSlice);
	}
	else {
		ApicSetTaskPriority(0);
		ApicWriteLocal(APIC_INITIAL_COUNT, 0);
	}
	
	/* Enter new thread */
	enter_thread(Regs);

	/* Never reached */
	return X86_IRQ_HANDLED;
}

/* Spurious handler, occurs on spurious
 * interrupts, don't acknowledge the interrupt */
int ApicSpuriousHandler(void *Args)
{
	_CRT_UNUSED(Args);
	return X86_IRQ_HANDLED;
}

/* The apic error handler interrupt
 * this occurs on errors, but i'm not really
 * sure yet what that entails, we leave it NA for now*/
int ApicErrorHandler(void *Args)
{
	_CRT_UNUSED(Args);
	return X86_IRQ_HANDLED;
}
