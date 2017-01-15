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
 * MollenOS X86 CMOS & RTC (Clock) Driver
 * http://wiki.osdev.org/CMOS#The_Real-Time_Clock
 */

/* Includes 
 * - System */
#include <os/driver/contracts/clock.h>
#include <os/driver/device.h>
#include <os/driver/acpi.h>
#include <os/driver/io.h>
#include "cmos.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Since there only exists a single cmos
 * chip on-board we keep some static information
 * in this driver */
#pragma pack(push, 1)
typedef struct _Cmos {
	DeviceIoSpace_t		IoSpace;
	uint8_t				AcpiCentury;
	int					UseRTC;
} Cmos_t;
#pragma pack(pop)

/* Globals 
 * We keep a global copy of the cmos data */
static Cmos_t *GlbCmos = NULL;

/* CmosRead
 * Read the byte at given register offset
 * from the CMOS-Chip */
uint8_t CmosRead(uint8_t Register)
{
	/* Variables for reading */
	uint8_t Tmp = 0;
	
	/* Keep NMI if disabled */
	Tmp = ReadIoSpace(&GlbCmos->IoSpace, CMOS_IO_SELECT, 1) & CMOS_NMI_BIT;

	/* Select Register (but do not change NMI) */
	WriteIoSpace(&GlbCmos->IoSpace, CMOS_IO_SELECT, (Tmp | (Register & CMOS_ALLBITS_NONMI)), 1);

	/* Done */
	return (uint8_t)ReadIoSpace(&GlbCmos->IoSpace, CMOS_IO_DATA, 1);
}

/* CmosRead
 * Writes a byte to the given register offset
 * from the CMOS-Chip */
void CmosWrite(uint8_t Register, uint8_t Data)
{
	/* Variables for writing */
	uint8_t Tmp = 0;

	/* Keep NMI if disabled */
	Tmp = (uint8_t)ReadIoSpace(&GlbCmos->IoSpace, CMOS_IO_SELECT, 1) & CMOS_NMI_BIT;

	/* Select Register (but do not change NMI) */
	WriteIoSpace(&GlbCmos->IoSpace, CMOS_IO_SELECT, (Tmp | (Register & CMOS_ALLBITS_NONMI)), 1);

	/* Write Data */
	WriteIoSpace(&GlbCmos->IoSpace, CMOS_IO_DATA, Data, 1);
}

/* Gets current time and stores it in a time structure */
void CmosGetTime(struct tm *TimeStructure)
{
	int oSec, n;
	uint8_t Century = 0;

	/* Do we support century? */
	if (Cmos->AcpiCentury != 0)
		Century = CmosRead(Cmos->AcpiCentury);

	/* Get Clock (Stable, thats why we loop) */
	while (CmosRead(CMOS_REGISTER_SECONDS) != TimeStructure->tm_sec
		|| CmosRead(CMOS_REGISTER_MINUTES) != TimeStructure->tm_min
		|| CmosRead(CMOS_REGISTER_HOURS) != TimeStructure->tm_hour
		|| CmosRead(CMOS_REGISTER_DAYS) != TimeStructure->tm_mday
		|| CmosRead(CMOS_REGISTER_MONTHS) != TimeStructure->tm_mon
		|| CmosRead(CMOS_REGISTER_YEARS) != TimeStructure->tm_year)
	{
		oSec = -1;
		n = 0;

		/* Update Seconds */
		while (n < 2)
		{
			/* Clock update in progress? */
			if (CmosRead(CMOS_REGISTER_STATUS_A) & CMOSA_UPDATE_IN_PROG)
				continue;

			TimeStructure->tm_sec = CmosReadRegister(CMOS_REGISTER_SECONDS);
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
		TimeStructure->tm_min = CmosReadRegister(CMOS_REGISTER_MINUTES);
		TimeStructure->tm_hour = CmosReadRegister(CMOS_REGISTER_HOURS);
		TimeStructure->tm_mday = CmosReadRegister(CMOS_REGISTER_DAYS);
		TimeStructure->tm_mon = CmosReadRegister(CMOS_REGISTER_MONTHS);
		TimeStructure->tm_year = CmosReadRegister(CMOS_REGISTER_YEARS);
	}

	/* Convert Time Format? */
	if (!(CmosReadRegister(CMOS_REGISTER_STATUS_B) & CMOSB_BCD_FORMAT))
	{
		/* Convert BCD to binary (default RTC mode). */
		TimeStructure->tm_year = CMOS_BCD_TO_DEC(TimeStructure->tm_year);
		TimeStructure->tm_mon = CMOS_BCD_TO_DEC(TimeStructure->tm_mon);
		TimeStructure->tm_mday =CMOS_BCD_TO_DEC(TimeStructure->tm_mday);
		TimeStructure->tm_hour =CMOS_BCD_TO_DEC(TimeStructure->tm_hour);
		TimeStructure->tm_min = CMOS_BCD_TO_DEC(TimeStructure->tm_min);
		TimeStructure->tm_sec = CMOS_BCD_TO_DEC(TimeStructure->tm_sec);

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
		TimeStructure->tm_year += (CMOS_CURRENT_YEAR / 100) * 100;

		if (TimeStructure->tm_year < CMOS_CURRENT_YEAR)
			TimeStructure->tm_year += 100;
	}
}

/* OnInterrupt
 * Is called when one of the registered devices
 * produces an interrupt. On successful handled
 * interrupt return OsNoError, otherwise the interrupt
 * won't be acknowledged */
OsStatus_t OnInterrupt(void)
{
	/* Since the cmos doesn't use
	 * interrupts, but the RTC does, we 
	 * will redirect the interrupt to RTC code */
}

/* OnLoad
 * The entry-point of a driver, this is called
 * as soon as the driver is loaded in the system */
OsStatus_t OnLoad(void)
{
	/* Variables */
	AcpiDescriptor_t Acpi;

	/* Allocate a new instance of the cmos-data */
	GlbCmos = (Cmos_t*)malloc(sizeof(Cmos_t));

	/* Create the io-space, again we have to create
	 * the io-space ourselves */
	GlbCmos->IoSpace.Id = 0;
	GlbCmos->IoSpace.Type = IO_SPACE_IO;
	GlbCmos->IoSpace.PhysicalBase = CMOS_IO_BASE;
	GlbCmos->IoSpace.Size = CMOS_IO_LENGTH;
	GlbCmos->AcpiCentury = 0;
	GlbCmos->UseRTC = 1;

	/* Create the io-space in system */
	if (CreateIoSpace(&GlbCmos->IoSpace) != OsNoError) {
		return OsError;
	}

	/* Query the system for acpi-information 
	 * - Check for century register
	 * - Check if we should disable rtc */
	if (AcpiQueryStatus(&Acpi) == OsNoError) {
		GlbCmos->AcpiCentury = Acpi.Century;
		if (Acpi.BootFlags & ACPI_IA_NO_CMOS_RTC) {
			GlbCmos->UseRTC = 0;
		}
	}

	/* No problem, last thing is to acquire the
	 * io-space, and just return that as result */
	if (AcquireIoSpace(&GlbCmos->IoSpace) != OsNoError) {
		return OsError;
	}

	/* Break */
	return OsNoError;
}

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t OnUnload(void)
{
	/* Destroy the io-space we created */
	if (GlbCmos->IoSpace.Id != 0) {
		ReleaseIoSpace(&GlbCmos->IoSpace);
		DestroyIoSpace(GlbCmos->IoSpace.Id);
	}

	/* Free up allocated resources */
	free(GlbCmos);

	/* Wuhuu */
	return OsNoError;
}

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
OsStatus_t OnRegister(MCoreDevice_t *Device)
{
	/* The CMOS/RTC is a fixed device
	 * and thus we don't support multiple instances */
	_CRT_UNUSED(Device);
	return OsNoError;
}

/* OnUnregister
 * Is called when the device-manager wants to unload
 * an instance of this driver from the system */
OsStatus_t OnUnregister(MCoreDevice_t *Device)
{
	/* The CMOS/RTC is a fixed device
	 * and thus we don't support multiple instances */
	_CRT_UNUSED(Device);
	return OsNoError;
}
