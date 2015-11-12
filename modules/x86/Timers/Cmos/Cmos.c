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
* MollenOS X86-32 CMOS (Time) Driver
* http://wiki.osdev.org/CMOS#The_Real-Time_Clock
*/

/* Includes */
#include <Module.h>
#include <Cmos.h>

/* Mutex */
Mutex_t *GlbCmosLock = NULL;
uint8_t AcpiCenturyRegister = 0;
MCoreModuleDescriptor_t *GlbDescriptor = NULL;

/* Entry point of a module */
MODULES_API void ModuleInit(MCoreModuleDescriptor_t *DriverDescriptor, void *DeviceData)
{
	/* Init lock */
	GlbDescriptor = DriverDescriptor;
	GlbCmosLock = GlbDescriptor->MutexCreate();
	AcpiCenturyRegister = (uint8_t)DeviceData;uint8_t

	/* Init Rtc */
	RtcInit();
}

/* Helpers, I/O */
uint8_t CmosReadRegister(uint8_t Register)
{
	/* Vars */
	uint8_t Tmp = 0, RetValue;

	/* Acquire Mutex */
	GlbDescriptor->MutexLock(GlbCmosLock);

	/* Keep NMI if disabled */
	Tmp = GlbDescriptor->PortReadByte(X86_CMOS_IO_SELECT) & X86_CMOS_NMI_BIT;

	/* Select Register (but do not change NMI) */
	GlbDescriptor->PortWriteByte(X86_CMOS_IO_SELECT, (Tmp | (Register & X86_CMOS_ALLBITS_NONMI)));

	/* Get Data */
	RetValue = GlbDescriptor->PortReadByte(X86_CMOS_IO_DATA);

	/* Unlock */
	GlbDescriptor->MutexUnlock(GlbCmosLock);

	/* Done */
	return RetValue;
}

void CmosWriteRegister(uint8_t Register, uint8_t Data)
{
	/* Vars */
	uint8_t Tmp = 0;

	/* Acquire Mutex */
	GlbDescriptor->MutexLock(GlbCmosLock);

	/* Keep NMI if disabled */
	Tmp = GlbDescriptor->PortReadByte(X86_CMOS_IO_SELECT)& X86_CMOS_NMI_BIT;

	/* Select Register (but do not change NMI) */
	GlbDescriptor->PortWriteByte(X86_CMOS_IO_SELECT, (Tmp | (Register & X86_CMOS_ALLBITS_NONMI)));

	/* Write Data */
	GlbDescriptor->PortWriteByte(X86_CMOS_IO_DATA, Data);

	/* Unlock */
	GlbDescriptor->MutexUnlock(GlbCmosLock);
}

/* Gets current time and stores it in a time structure */
void CmosGetTime(tm *TimeStructure)
{
	int oSec, n;
	uint8_t Century = 0;

	/* Do we support century? */
	if (AcpiCenturyRegister != 0)
		Century = CmosReadRegister(AcpiCenturyRegister);

	/* Get Clock (Stable, thats why we loop) */
	while (CmosReadRegister(X86_CMOS_REGISTER_SECONDS) != TimeStructure->tm_sec
		|| CmosReadRegister(X86_CMOS_REGISTER_MINUTES) != TimeStructure->tm_min
		|| CmosReadRegister(X86_CMOS_REGISTER_HOURS) != TimeStructure->tm_hour
		|| CmosReadRegister(X86_CMOS_REGISTER_DAYS) != TimeStructure->tm_mday
		|| CmosReadRegister(X86_CMOS_REGISTER_MONTHS) != TimeStructure->tm_mon
		|| CmosReadRegister(X86_CMOS_REGISTER_YEARS) != TimeStructure->tm_year)
	{
		oSec = -1;
		n = 0;

		/* Update Seconds */
		while (n < 2)
		{
			/* Clock update in progress? */
			if (CmosReadRegister(X86_CMOS_REGISTER_STATUS_A) & X86_CMOSA_UPDATE_IN_PROG)
				continue;

			TimeStructure->tm_sec = CmosReadRegister(X86_CMOS_REGISTER_SECONDS);
			if (TimeStructure->tm_sec != oSec)
			{
				/* Seconds changed.  First from -1, then because the
				* clock ticked, which is what we're waiting for to
				* get a precise reading.
				*/
				oSec = TimeStructure->tm_sec;
				n++;
			}

		}

		/* Read the other registers. */
		TimeStructure->tm_min = CmosReadRegister(X86_CMOS_REGISTER_MINUTES);
		TimeStructure->tm_hour = CmosReadRegister(X86_CMOS_REGISTER_HOURS);
		TimeStructure->tm_mday = CmosReadRegister(X86_CMOS_REGISTER_DAYS);
		TimeStructure->tm_mon = CmosReadRegister(X86_CMOS_REGISTER_MONTHS);
		TimeStructure->tm_year = CmosReadRegister(X86_CMOS_REGISTER_YEARS);
	}

	/* Convert Time Format? */
	if (!(CmosReadRegister(X86_CMOS_REGISTER_STATUS_B) & X86_CMOSB_BCD_FORMAT))
	{
		/* Convert BCD to binary (default RTC mode). */
		TimeStructure->tm_year = X86_CMOS_BCD_TO_DEC(TimeStructure->tm_year);
		TimeStructure->tm_mon = X86_CMOS_BCD_TO_DEC(TimeStructure->tm_mon);
		TimeStructure->tm_mday = X86_CMOS_BCD_TO_DEC(TimeStructure->tm_mday);
		TimeStructure->tm_hour = X86_CMOS_BCD_TO_DEC(TimeStructure->tm_hour);
		TimeStructure->tm_min = X86_CMOS_BCD_TO_DEC(TimeStructure->tm_min);
		TimeStructure->tm_sec = X86_CMOS_BCD_TO_DEC(TimeStructure->tm_sec);

		/* Convert Century */
		if (Century != 0)
			Century = X86_CMOS_BCD_TO_DEC(Century);
	}

	/* Counts from 0. */
	TimeStructure->tm_mon--;

	/* Correct the year */
	if (Century != 0) 
		TimeStructure->tm_year += Century * 100;
	else 
	{
		TimeStructure->tm_year += (X86_CMOS_CURRENT_YEAR / 100) * 100;
		
		if (TimeStructure->tm_year < X86_CMOS_CURRENT_YEAR) 
			TimeStructure->tm_year += 100;
	}
}