/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Get System Time
 *  - Retrieves the system time in sec/min/day/mon/year format, and converts it to local
 *    time in time_t format.
 */

#include <os/mollenos.h>
#include <stddef.h>
#include <time.h>

time_t time(time_t* Timer)
{
    SystemTime_t SystemTime = { { { 0 } } };
	struct tm    Temporary  = { 0 };
	time_t       Result     = 0;

    // Retrieve structure in our format, convert and mktime
	if (GetSystemTime(&SystemTime) == OsSuccess) {
        Temporary.tm_sec  = SystemTime.Second;
        Temporary.tm_min  = SystemTime.Minute;
        Temporary.tm_hour = SystemTime.Hour;
        Temporary.tm_mday = SystemTime.DayOfMonth;
        Temporary.tm_mon  = SystemTime.Month - 1;
        Temporary.tm_year = SystemTime.Year;
        Result = mktime(&Temporary);
        if (Timer != NULL) {
            *Timer = Result;
        }
	}
	return Result;
}
