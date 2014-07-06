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
#include <arch.h>
#include <acpi.h>
#include <drivers\clock\clock.h>

/* Helpers, I/O */
uint8_t clock_read_register(uint8_t reg)
{
	/* Select Register */
	outb(X86_CMOS_IO_SELECT, reg);

	/* Get Data */
	return inb(X86_CMOS_IO_DATA);
}

void clock_write_register(uint8_t reg, uint8_t data)
{
	/* Select Register */
	outb(X86_CMOS_IO_SELECT, reg);

	/* Send Data */
	outb(X86_CMOS_IO_DATA, data);
}

/* Gets current time and stores it in a time structure */
void clock_get_time(tm *t)
{
	int osec, n;
	uint8_t century = 0;

	/* Do we support century? */
	if (AcpiGbl_FADT.Century != 0)
		century = clock_read_register(AcpiGbl_FADT.Century);

	/* Get Clock (Stable, thats why we loop) */
	while (clock_read_register(X86_CMOS_REGISTER_SECONDS) != t->tm_sec
		|| clock_read_register(X86_CMOS_REGISTER_MINUTES) != t->tm_min
		|| clock_read_register(X86_CMOS_REGISTER_HOURS) != t->tm_hour
		|| clock_read_register(X86_CMOS_REGISTER_DAYS) != t->tm_mday
		|| clock_read_register(X86_CMOS_REGISTER_MONTHS) != t->tm_mon
		|| clock_read_register(X86_CMOS_REGISTER_YEARS) != t->tm_year)
	{
		osec = -1;
		n = 0;

		/* Update Seconds */
		while (n < 2)
		{
			/* Clock update in progress? */
			if (clock_read_register(X86_CMOS_REGISTER_STATUS_A) & X86_CMOSA_UPDATE_IN_PROG) 
				continue;

			t->tm_sec = clock_read_register(X86_CMOS_REGISTER_SECONDS);
			if (t->tm_sec != osec) 
			{
				/* Seconds changed.  First from -1, then because the
				* clock ticked, which is what we're waiting for to
				* get a precise reading.
				*/
				osec = t->tm_sec;
				n++;
			}

		}

		/* Read the other registers. */
		t->tm_min = clock_read_register(X86_CMOS_REGISTER_MINUTES);
		t->tm_hour = clock_read_register(X86_CMOS_REGISTER_HOURS);
		t->tm_mday = clock_read_register(X86_CMOS_REGISTER_DAYS);
		t->tm_mon = clock_read_register(X86_CMOS_REGISTER_MONTHS);
		t->tm_year = clock_read_register(X86_CMOS_REGISTER_YEARS);
	}

	/* Convert Time Format? */
	if (!(clock_read_register(X86_CMOS_REGISTER_STATUS_B) & X86_CMOSB_BCD_FORMAT))
	{
		/* Convert BCD to binary (default RTC mode). */
		t->tm_year = X86_CMOS_BCD_TO_DEC(t->tm_year);
		t->tm_mon = X86_CMOS_BCD_TO_DEC(t->tm_mon);
		t->tm_mday = X86_CMOS_BCD_TO_DEC(t->tm_mday);
		t->tm_hour = X86_CMOS_BCD_TO_DEC(t->tm_hour);
		t->tm_min = X86_CMOS_BCD_TO_DEC(t->tm_min);
		t->tm_sec = X86_CMOS_BCD_TO_DEC(t->tm_sec);

		/* Convert Century */
		if (century != 0)
			century = X86_CMOS_BCD_TO_DEC(century);
	}

	/* Counts from 0. */
	t->tm_mon--;

	/* Correct the year */
	if (century != 0) {
		t->tm_year += century * 100;
	}
	else {
		t->tm_year += (X86_CMOS_CURRENT_YEAR / 100) * 100;
		if (t->tm_year < X86_CMOS_CURRENT_YEAR) t->tm_year += 100;
	}
}