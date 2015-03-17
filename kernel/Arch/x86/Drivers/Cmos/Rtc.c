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
#include <timers.h>
#include <stddef.h>
#include <stdio.h>

/* Externs */
extern volatile uint32_t timer_quantum;
extern void rdtsc(uint64_t *value);
extern void _yield(void);

/* Globals */
volatile uint32_t glb_alarm_tick = 0;
volatile uint64_t glb_ns_counter = 0;
uint32_t glb_ns_step = 0;
spinlock_t stall_lock;

/* The Clock Handler */
int clock_irq_handler(void *data)
{
	/* Unused */
	_CRT_UNUSED(data);

	/* Update Peroidic Tick Counter */
	glb_ns_counter += glb_ns_step;

	/* Apply Timer Time (roughly 2 ms) */
	timers_apply_time(2);

	/* Acknowledge Irq 8 by reading register C */
	clock_read_register(X86_CMOS_REGISTER_STATUS_C);

	return X86_IRQ_HANDLED;
}

/* Initialization */
void clock_init(void)
{
	interrupt_status_t int_state;
	uint8_t state_b = 0;
	uint8_t rate = 0x07; /* must be between 3 and 15 */

	/* Ms is 1.95, 512 ints per sec */
	/* Frequency = 32768 >> (rate-1), 15 = 2, 14 = 4, 13 = 8/s (125 ms) */
	glb_ns_step = 1950;
	glb_ns_counter = 0;
	glb_alarm_tick = 0;

	/* Reset lock */
	spinlock_reset(&stall_lock);

	/* Disable IRQ's for this duration */
	int_state = interrupt_disable();

	/* Disable RTC Irq */
	state_b = clock_read_register(X86_CMOS_REGISTER_STATUS_B);
	state_b &= ~(0x70);
	clock_write_register(X86_CMOS_REGISTER_STATUS_B, state_b);

	/* Update state_b */
	state_b = clock_read_register(X86_CMOS_REGISTER_STATUS_B);

	/* Install ISA IRQ Handler using normal install function */
	interrupt_install(X86_CMOS_RTC_IRQ, INTERRUPT_RTC, clock_irq_handler, NULL);

	/* Set Frequency */
	clock_write_register(X86_CMOS_REGISTER_STATUS_A, 0x20 | rate);

	/* Clear pending interrupt */
	clock_read_register(X86_CMOS_REGISTER_STATUS_C);

	/* Enable Periodic Interrupts */
	state_b = clock_read_register(X86_CMOS_REGISTER_STATUS_B);
	state_b |= X86_CMOSB_RTC_PERIODIC;
	clock_write_register(X86_CMOS_REGISTER_STATUS_B, state_b);

	/* Done, reenable interrupts */
	interrupt_set_state(int_state);

	/* Clear pending interrupt again */
	clock_read_register(X86_CMOS_REGISTER_STATUS_C);
}

/* Rtc Ticks */
uint64_t clock_get_clocks(void)
{
	return glb_ns_counter;
}

/* Stall for ms */
void clock_stall(uint32_t ms)
{
	uint64_t ticks = (ms * 1000) + clock_get_clocks();

	/* If glb_clock_tick is 0, RTC failure */
	if (clock_get_clocks() == 0)
	{
		printf("Tried to use RTC while having a failure :/\n");
		clock_stall_noint(ms);
		return;
	}

	/* While */
	while (ticks >= clock_get_clocks())
		_yield();
}

/* Stall for ms */
void clock_stall_noint(uint32_t ms)
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