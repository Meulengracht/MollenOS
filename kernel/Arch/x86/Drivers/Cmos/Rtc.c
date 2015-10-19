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
#include <Arch.h>
#include <Drivers\Cmos\Cmos.h>
#include <Timers.h>
#include <stddef.h>
#include <stdio.h>

/* Externs */
extern volatile uint32_t GlbTimerQuantum;
extern void rdtsc(uint64_t *value);
extern void _yield(void);

/* Globals */
volatile uint32_t GlbRtcAlarmTicks = 0;
volatile uint64_t GlbRtcNsCounter = 0;
uint32_t GlbRtcNsTick = 0;

/* The Clock Handler */
int RtcIrqHandler(void *Data)
{
	/* Unused */
	_CRT_UNUSED(Data);

	/* Update Peroidic Tick Counter */
	GlbRtcNsCounter += GlbRtcNsTick;

	/* Apply Timer Time (roughly 1 ms) */
	TimersApplyMs(1);

	/* Acknowledge Irq 8 by reading register C */
	CmosReadRegister(X86_CMOS_REGISTER_STATUS_C);

	return X86_IRQ_HANDLED;
}

/* Initialization */
OsStatus_t RtcInit(void)
{
	IntStatus_t IntrState;
	uint8_t StateB = 0;
	uint8_t Rate = 0x08; /* must be between 3 and 15 */

	/* Ms is .97, 1024 ints per sec */
	/* Frequency = 32768 >> (rate-1), 15 = 2, 14 = 4, 13 = 8/s (125 ms) */
	GlbRtcNsTick = 976;
	GlbRtcNsCounter = 0;
	GlbRtcAlarmTicks = 0;

	/* Disable IRQ's for this duration */
	IntrState = InterruptDisable();

	/* Disable RTC Irq */
	StateB = CmosReadRegister(X86_CMOS_REGISTER_STATUS_B);
	StateB &= ~(0x70);
	CmosWriteRegister(X86_CMOS_REGISTER_STATUS_B, StateB);
	
	/* Update state_b */
	StateB = CmosReadRegister(X86_CMOS_REGISTER_STATUS_B);

	/* Install ISA IRQ Handler using normal install function */
	InterruptInstallISA(X86_CMOS_RTC_IRQ, INTERRUPT_RTC, RtcIrqHandler, NULL);

	/* Set Frequency */
	CmosWriteRegister(X86_CMOS_REGISTER_STATUS_A, 0x20 | Rate);

	/* Clear pending interrupt */
	CmosReadRegister(X86_CMOS_REGISTER_STATUS_C);

	/* Enable Periodic Interrupts */
	StateB = CmosReadRegister(X86_CMOS_REGISTER_STATUS_B);
	StateB |= X86_CMOSB_RTC_PERIODIC;
	CmosWriteRegister(X86_CMOS_REGISTER_STATUS_B, StateB);

	/* Done, reenable interrupts */
	InterruptRestoreState(IntrState);

	/* Clear pending interrupt again */
	CmosReadRegister(X86_CMOS_REGISTER_STATUS_C);

	/* Done */
	return OS_STATUS_OK;
}

/* Rtc Ticks */
uint64_t RtcGetClocks(void)
{
	return GlbRtcNsCounter;
}

/* Stall for ms */
void RtcStallBackup(uint32_t MilliSeconds)
{
	uint64_t RdTicks = 0;
	uint64_t TickEnd = 0;

	/* Read Time Stamp Counter */
	rdtsc(&RdTicks);

	/* Calculate ticks */
	TickEnd = RdTicks + (MilliSeconds * GlbTimerQuantum);

	/* Wait */
	while (TickEnd > RdTicks)
		rdtsc(&RdTicks);
}

/* Sleep for ms */
void RtcSleep(uint32_t MilliSeconds)
{
	/* Calculate TickEnd in NanoSeconds */
	uint64_t TickEnd = (MilliSeconds * 1000) + RtcGetClocks();

	/* If glb_clock_tick is 0, RTC failure */
	if (RtcGetClocks() == 0)
	{
		RtcStallBackup(MilliSeconds);
		return;
	}

	/* While */
	while (TickEnd >= RtcGetClocks())
		_yield();
}

/* Stall for ms */
void RtcStall(uint32_t MilliSeconds)
{
	/* Calculate TickEnd in NanoSeconds */
	uint64_t TickEnd = (MilliSeconds * 1000) + RtcGetClocks();

	/* If glb_clock_tick is 0, RTC failure */
	if (RtcGetClocks() == 0)
	{
		RtcStallBackup(MilliSeconds);
		return;
	}

	/* While */
	while (TickEnd >= RtcGetClocks())
		_asm nop;
}