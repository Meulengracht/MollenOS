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
#define __MODULE "CMOS"
#define __TRACE

/* Includes 
 * - System */
#include <system/io.h>
#include <acpiinterface.h>
#include <timers.h>
#include <debug.h>
#include "../cmos.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Since there only exists a single cmos
 * chip on-board we keep some static information
 * in this driver */
static Cmos_t CmosUnit;

/* CmosRead
 * Read the byte at given register offset
 * from the CMOS-Chip */
uint8_t
CmosRead(
    _In_ uint8_t Register)
{
	// Variables
    size_t Storage  = 0;
	uint8_t Tmp     = 0;
	
	// Keep NMI if disabled
    ReadDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_SELECT, 1, &Storage);
    Storage     &= CMOS_NMI_BIT;
    Tmp         = Storage & 0xFF;

	// Select Register (but do not change NMI)
	WriteDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_SELECT, 1, 
        (Tmp | (Register & CMOS_ALLBITS_NONMI)));
    ReadDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_DATA, 1, &Storage);
    Tmp         = Storage & 0xFF;
	return Tmp;
}

/* CmosWrite
 * Writes a byte to the given register offset
 * from the CMOS-Chip */
void
CmosWrite(
    _In_ uint8_t Register,
    _In_ uint8_t Data)
{
	// Variables
    size_t Storage  = 0;
	uint8_t Tmp     = 0;

	// Keep NMI if disabled
    ReadDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_SELECT, 1, &Storage);
    Storage     &= CMOS_NMI_BIT;
    Tmp         = Storage & 0xFF;

	// Select Register (but do not change NMI)
	WriteDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_SELECT, 1,
		(Tmp | (Register & CMOS_ALLBITS_NONMI)));
	WriteDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_DATA, 1, Data);
}

/* CmosGetTicks
 * Retrieves the number of ticks done by the RTC. */
clock_t
CmosGetTicks(void)
{
    return CmosUnit.Ticks;
}

/* CmosGetTime
 * Retrieves the current time and stores it in
 * the c-library time structure */
void
CmosGetTime(
    _Out_ struct tm *Time)
{
	// Variables
	int Sec, Counter;
	uint8_t Century = 0;

	// Do we support century?
	if (CmosUnit.AcpiCentury != 0) {
		Century = CmosRead(CmosUnit.AcpiCentury);
	}	

	// Get Clock (Stable, thats why we loop)
	while (CmosRead(CMOS_REGISTER_SECONDS) != Time->tm_sec
		|| CmosRead(CMOS_REGISTER_MINUTES) != Time->tm_min
		|| CmosRead(CMOS_REGISTER_HOURS) != Time->tm_hour
		|| CmosRead(CMOS_REGISTER_DAYS) != Time->tm_mday
		|| CmosRead(CMOS_REGISTER_MONTHS) != Time->tm_mon
		|| CmosRead(CMOS_REGISTER_YEARS) != Time->tm_year) {
		// Reset variables
		Sec = -1;
		Counter = 0;

		// Update Seconds
		while (Counter < 2) {
			if (CmosRead(CMOS_REGISTER_STATUS_A) & CMOSA_UPDATE_IN_PROG) {
				continue;
			}
			Time->tm_sec = CmosRead(CMOS_REGISTER_SECONDS);

			// Seconds changed.  First from -1, then because the
			// clock ticked, which is what we're waiting for to
			// get a precise reading.
			if (Time->tm_sec != Sec) {
				Sec = Time->tm_sec;
				Counter++;
			}
		}

		// Read the other registers.
		Time->tm_min = CmosRead(CMOS_REGISTER_MINUTES);
		Time->tm_hour = CmosRead(CMOS_REGISTER_HOURS);
		Time->tm_mday = CmosRead(CMOS_REGISTER_DAYS);
		Time->tm_mon = CmosRead(CMOS_REGISTER_MONTHS);
		Time->tm_year = CmosRead(CMOS_REGISTER_YEARS);
	}

	// Convert time format? 
	// - Convert BCD to binary (default RTC mode).
	if (!(CmosRead(CMOS_REGISTER_STATUS_B) & CMOSB_BCD_FORMAT)) {
		Time->tm_year = CMOS_BCD_TO_DEC(Time->tm_year);
		Time->tm_mon = CMOS_BCD_TO_DEC(Time->tm_mon);
		Time->tm_mday = CMOS_BCD_TO_DEC(Time->tm_mday);
		Time->tm_hour = CMOS_BCD_TO_DEC(Time->tm_hour);
		Time->tm_min = CMOS_BCD_TO_DEC(Time->tm_min);
		Time->tm_sec = CMOS_BCD_TO_DEC(Time->tm_sec);
		if (Century != 0) {
			Century = CMOS_BCD_TO_DEC(Century);
		}	
	}

	// Counts from 0
	Time->tm_mon--;

	// Correct the year
	if (Century != 0) {
		Time->tm_year += Century * 100;
	}
	else {
		Time->tm_year += (CMOS_CURRENT_YEAR / 100) * 100;
		if (Time->tm_year < CMOS_CURRENT_YEAR) {
			Time->tm_year += 100;
		}
	}
}

/* CmosInitialize
 * Initializes the cmos, and if set, the RTC as-well. */
OsStatus_t
CmosInitialize(
    _In_ int InitializeRtc)
{
    // Debug
    TRACE("CmosInitialize(rtc %i)", InitializeRtc);

	// Allocate a new instance of the cmos-data
	memset(&CmosUnit, 0, sizeof(Cmos_t));

    // Check out century register
    if (AcpiAvailable() == ACPI_AVAILABLE) {
        CmosUnit.AcpiCentury = AcpiGbl_FADT.Century;
    }
	CmosUnit.RtcAvailable = InitializeRtc;

    // Register clock
    TimersRegisterClock(CmosGetTime);

	// Last part is to initialize the rtc
	// chip if it present in system
	if (CmosUnit.RtcAvailable) {
		return RtcInitialize(&CmosUnit);
	}
	else {
		return OsSuccess;
	}
}
