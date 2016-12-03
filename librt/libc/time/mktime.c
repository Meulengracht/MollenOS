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
* MollenOS C Library - Make Time
*/

/* Includes */
#include <internal/_time.h>
#include <time.h>
#include <os/Syscall.h>

/* Year tab, one without leap year
 * and one with */
const int _ytab[2][12] = {
	{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
	{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};

/* These are the globals */
int _daylight = 0;                  // Non-zero if daylight savings time is used
long _dstbias = 0;                  // Offset for Daylight Saving Time
long _timezone = 0;                 // Difference in seconds between GMT and local time
char *_tzname[2] = { "GMT", "GMT" };  // Standard/daylight savings time zone names

/* mktime
 * converts the time structure
 * to a time_t variable */
time_t mktime(tm *tmbuf) 
{
	long day, year;
	int tm_year;
	int yday, month;
	/*unsigned*/ long seconds;
	int overflow;
	long dst;

	tmbuf->tm_min += tmbuf->tm_sec / 60;
	tmbuf->tm_sec %= 60;
	if (tmbuf->tm_sec < 0) {
		tmbuf->tm_sec += 60;
		tmbuf->tm_min--;
	}
	tmbuf->tm_hour += tmbuf->tm_min / 60;
	tmbuf->tm_min = tmbuf->tm_min % 60;
	if (tmbuf->tm_min < 0) {
		tmbuf->tm_min += 60;
		tmbuf->tm_hour--;
	}
	day = tmbuf->tm_hour / 24;
	tmbuf->tm_hour = tmbuf->tm_hour % 24;
	if (tmbuf->tm_hour < 0) {
		tmbuf->tm_hour += 24;
		day--;
	}
	tmbuf->tm_year += tmbuf->tm_mon / 12;
	tmbuf->tm_mon %= 12;
	if (tmbuf->tm_mon < 0) {
		tmbuf->tm_mon += 12;
		tmbuf->tm_year--;
	}
	day += (tmbuf->tm_mday - 1);
	while (day < 0) {
		if (--tmbuf->tm_mon < 0) {
			tmbuf->tm_year--;
			tmbuf->tm_mon = 11;
		}
		day += _ytab[isleap(YEAR_BASE + tmbuf->tm_year)][tmbuf->tm_mon];
	}
	while (day >= _ytab[isleap(YEAR_BASE + tmbuf->tm_year)][tmbuf->tm_mon]) {
		day -= _ytab[isleap(YEAR_BASE + tmbuf->tm_year)][tmbuf->tm_mon];
		if (++(tmbuf->tm_mon) == 12) {
			tmbuf->tm_mon = 0;
			tmbuf->tm_year++;
		}
	}
	tmbuf->tm_mday = day + 1;
	year = EPOCH_YEAR;
	if (tmbuf->tm_year < year - YEAR_BASE) return (time_t)-1;
	seconds = 0;
	day = 0;                      // Means days since day 0 now
	overflow = 0;

	// Assume that when day becomes negative, there will certainly
	// be overflow on seconds.
	// The check for overflow needs not to be done for leapyears
	// divisible by 400.
	// The code only works when year (1970) is not a leapyear.
	tm_year = tmbuf->tm_year + YEAR_BASE;

	if (TIME_MAX / 365 < tm_year - year) overflow++;
	day = (tm_year - year) * 365;
	if (TIME_MAX - day < (tm_year - year) / 4 + 1) overflow++;
	day += (tm_year - year) / 4 + ((tm_year % 4) && tm_year % 4 < year % 4);
	day -= (tm_year - year) / 100 + ((tm_year % 100) && tm_year % 100 < year % 100);
	day += (tm_year - year) / 400 + ((tm_year % 400) && tm_year % 400 < year % 400);

	yday = month = 0;
	while (month < tmbuf->tm_mon) {
		yday += _ytab[isleap(tm_year)][month];
		month++;
	}
	yday += (tmbuf->tm_mday - 1);
	if (day + yday < 0) overflow++;
	day += yday;

	tmbuf->tm_yday = yday;
	tmbuf->tm_wday = (day + 4) % 7;               // Day 0 was thursday (4)

	seconds = ((tmbuf->tm_hour * 60L) + tmbuf->tm_min) * 60L + tmbuf->tm_sec;

	if ((TIME_MAX - seconds) / SECS_DAY < day) overflow++;
	seconds += day * SECS_DAY;

	// Now adjust according to timezone and daylight saving time
	if (((_timezone > 0) && (TIME_MAX - _timezone < seconds)) ||
		((_timezone < 0) && (seconds < -_timezone))) {
		overflow++;
	}
	seconds += _timezone;

	if (tmbuf->tm_isdst) {
		dst = _dstbias;
	}
	else {
		dst = 0;
	}

	if (dst > seconds) overflow++;        // dst is always non-negative
	seconds -= dst;

	if (overflow) return (time_t)-1;

	if ((time_t)seconds != seconds) return (time_t)-1;
	return (time_t)seconds;
}