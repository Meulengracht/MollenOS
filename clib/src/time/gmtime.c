/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS C Library - Convert to globaltime struct
*/

/* Includes */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <internal/_time.h>
#include <os/Syscall.h>
#include <os/Thread.h>

/* Re-entrency */
tm *gmtime_r(const time_t *timer, tm *tmbuf) {
	time_t time = *timer;
	unsigned long dayclock, dayno;
	int year = EPOCH_YEAR;

	dayclock = (unsigned long)time % SECS_DAY;
	dayno = (unsigned long)time / SECS_DAY;

	tmbuf->tm_sec = dayclock % 60;
	tmbuf->tm_min = (dayclock % 3600) / 60;
	tmbuf->tm_hour = dayclock / 3600;
	tmbuf->tm_wday = (dayno + 4) % 7; // Day 0 was a thursday
	while (dayno >= (unsigned long)YEARSIZE(year)) {
		dayno -= YEARSIZE(year);
		year++;
	}
	tmbuf->tm_year = year - YEAR_BASE;
	tmbuf->tm_yday = dayno;
	tmbuf->tm_mon = 0;
	while (dayno >= (unsigned long)_ytab[isleap(year)][tmbuf->tm_mon]) {
		dayno -= _ytab[isleap(year)][tmbuf->tm_mon];
		tmbuf->tm_mon++;
	}
	tmbuf->tm_mday = dayno + 1;
	tmbuf->tm_isdst = 0;
	tmbuf->tm_gmtoff = 0;
	tmbuf->tm_zone = "UTC";
	return tmbuf;
}

/* gmtime
 * converts a time_t to the
 * timestructure with gmtime
 * format */
tm *gmtime(const time_t *timer) {
	tm *buf = &TLSGetCurrent()->TmBuffer;
	return gmtime_r(timer, buf);
}