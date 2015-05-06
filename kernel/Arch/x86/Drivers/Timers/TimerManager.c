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
* MollenOS x86-32 Timer Manager Header
*/

/* Includes */
#include <acpi.h>
#include <SysTimers.h>
#include <stdio.h>

/* Timer Drivers */
#include <Drivers\Cmos\Cmos.h>
#include <Drivers\Timers\Pit\Pit.h>
#include <Drivers\Timers\Hpet\Hpet.h>

/* Globals */
uint32_t GlbTimerMode = 0;

/* Externs */
extern void rdtsc(volatile uint64_t *value);

/* Enter Fallback Mode */
void TimerManagerEnterFallback(void)
{
	printf("Timers: Fallback Mode\n");

	/* First, try the PIT (It should like always exist anyway) */
	if (PitInit() == OS_STATUS_OK)
	{
		/* Yay */
		GlbTimerMode = 2;
		return;
	}

	/* Wtf??? Lets try the RTC */
	RtcInit();
	GlbTimerMode = 3;
}

/* Initializor */
void TimerManagerInit(void)
{
	ACPI_TABLE_HEADER *Header = NULL;

	/* Init Cmos */
	CmosInit();

	/* Ok, first of all we want to check for the HPET timer 
	 * so try to initialize that driver */
	if (ACPI_FAILURE(AcpiGetTable(ACPI_SIG_HPET, 0, &Header)))
	{
		/* Fallback */
		TimerManagerEnterFallback();
		
		/* Done */
		return;
	}

	/* If we reach this point, the HPET is present */
	if (HpetSetup((void*)Header) != OS_STATUS_OK)
	{
		/* Fallback */
		TimerManagerEnterFallback();

		/* Done */
		return;
	}

	/* We are in Hpet Mode */
	GlbTimerMode = 1;
}

/* Sleep function */
void SleepMs(uint32_t MilliSeconds)
{
	/* Call correct */
	if (GlbTimerMode == 3)
		RtcSleep(MilliSeconds);
	else if (GlbTimerMode == 2)
		PitSleep(MilliSeconds);
	else if (GlbTimerMode == 1)
		HpetSleep(MilliSeconds); /* Hpet */
	else
	{
		/* This most likely is called by Acpi */
		volatile uint64_t RdTicks = 0;
		uint64_t TickEnd = 0;

		/* Read Time Stamp Counter */
		rdtsc(&RdTicks);

		/* Calculate ticks */
		TickEnd = RdTicks + (MilliSeconds * 1000);

		/* Wait */
		while (TickEnd > RdTicks)
			rdtsc(&RdTicks);
	}
}

void SleepNs(uint32_t NanoSeconds)
{
	/* Only Hpet supports this */
	if (GlbTimerMode != 1)
		SleepMs(1);
}

/* Stall functions */
void StallMs(uint32_t MilliSeconds)
{
	/* Call correct */
	if (GlbTimerMode == 3)
		RtcStall(MilliSeconds);
	else if (GlbTimerMode == 2)
		PitStall(MilliSeconds);
	else if (GlbTimerMode == 1)
		HpetStall(MilliSeconds); /* Hpet */
	else
	{
		/* This most likely is called by Acpi */
		volatile uint64_t RdTicks = 0;
		uint64_t TickEnd = 0;

		/* Read Time Stamp Counter */
		rdtsc(&RdTicks);

		/* Calculate ticks */
		TickEnd = RdTicks + (MilliSeconds * 1000);

		/* Wait */
		while (TickEnd > RdTicks)
			rdtsc(&RdTicks);
	}
}

void StallNs(uint32_t NanoSeconds)
{
	/* Only Hpet supports this */
	if (GlbTimerMode != 1)
		StallMs(1);
}