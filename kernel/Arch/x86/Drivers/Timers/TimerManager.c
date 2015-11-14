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
#include <Cpu.h>
#include <Drivers\Cmos\Cmos.h>
#include <Drivers\Timers\Pit\Pit.h>
#include <Drivers\Timers\Hpet\Hpet.h>

/* Globals */
uint32_t GlbTimerMode = 0;

/* Externs */
extern x86CpuObject_t GlbBootCpuInfo;
extern volatile uint32_t GlbTimerQuantum;
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

/* Stall-No-Int (BACKUP) */
void DelayMs(uint32_t MilliSeconds)
{
	/* Keep value in this */
	volatile uint64_t Counter = 0;
	volatile uint64_t TimeOut = 0;
	volatile uint64_t CpuFrequency = GlbTimerQuantum * 1000;

	/* Sanity */
	if (!(GlbBootCpuInfo.EdxFeatures & CPUID_FEAT_EDX_TSC))
	{
		printf("DELAY CALLED AND RDTSC IS NOT SUPPORTED WTF");
		Idle();
	}

	/* Use rdtsc for this */
	rdtsc(&Counter);
	TimeOut = Counter + (uint64_t)(((MilliSeconds / 1000)) * CpuFrequency);

	while (Counter < TimeOut) { rdtsc(&Counter);  }
}

