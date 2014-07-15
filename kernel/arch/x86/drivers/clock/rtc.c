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
* MollenOS X86-32 RTC (Timer) Driver
* http://wiki.osdev.org/RTC
*/

/* Includes */
#include <arch.h>
#include <lapic.h>
#include <drivers\clock\clock.h>
#include <stddef.h>
#include <stdio.h>

/* Externs */
extern volatile uint32_t timer_quantum;
extern void rdtsc(uint64_t *value);

/* Globals */
volatile time_t glb_clock_tick = 0;

/* The Clock Handler */
void clock_irq_handler(void *data)
{
	_CRT_UNUSED(data);

	/* Update Tick Counter */
	glb_clock_tick++;

	/* Acknowledge Irq 8 by reading register C */
	clock_read_register(X86_CMOS_REGISTER_STATUS_C);
}

/* Initialization */
void clock_init(void)
{
	interrupt_status_t int_state;
	uint8_t state_b = 0;
	uint8_t rate = 9; /* must be between 3 and 15 */

	/* Ms is 7.8, 128 ints per sec */
	/* Frequency = 32768 >> (rate-1) */

	/* Disable IRQ's for this duration */
	int_state = interrupt_disable();
	glb_clock_tick = 0;

	/* Disable RTC Irq */
	state_b = clock_read_register(X86_CMOSX_BIT_DISABLE_NMI | X86_CMOS_REGISTER_STATUS_B);
	state_b &= ~0x70;
	clock_write_register(X86_CMOSX_BIT_DISABLE_NMI | X86_CMOS_REGISTER_STATUS_B, state_b);

	/* Install IRQ Handler */
	interrupt_install(X86_CMOS_RTC_IRQ, INTERRUPT_RTC, clock_irq_handler, NULL);

	/* Set Frequency */
	clock_write_register(X86_CMOSX_BIT_DISABLE_NMI | X86_CMOS_REGISTER_STATUS_A, 0x20 | rate);

	/* Enable Periodic Interrupts */
	state_b = clock_read_register(X86_CMOSX_BIT_DISABLE_NMI | X86_CMOS_REGISTER_STATUS_B);
	state_b |= X86_CMOSB_RTC_PERIODIC;
	clock_write_register(X86_CMOSX_BIT_DISABLE_NMI | X86_CMOS_REGISTER_STATUS_B, state_b);

	/* Clear pending interrupt */
	clock_read_register(X86_CMOS_REGISTER_STATUS_C);

	/* Done, reenable interrupts */
	interrupt_set_state(int_state);
}

/* Clock Ticks */
time_t clock_get_clocks(void)
{
	return glb_clock_tick;
}

/* Stall for ms */
void clock_stall(time_t ms)
{
	time_t ticks = (ms / 8) + clock_get_clocks();

	/* If glb_clock_tick is 0, RTC failure */
	if (clock_get_clocks() == 0)
	{
		stall_ms((size_t)ms);
		return;
	}

	/* While */
	while (ticks >= clock_get_clocks())
		idle();
}

/* Stall for ms */
void clock_stall_noint(time_t ms)
{
	uint64_t rd_ticks = 0;
	uint64_t ticks = 0;

	/* Read Time Stamp Counter */
	rdtsc(&rd_ticks);

	/* Calculate ticks */
	ticks = rd_ticks + (ms * timer_quantum);

	/* Wait */
	while (ticks > rd_ticks)
		rdtsc(&rd_ticks);
}