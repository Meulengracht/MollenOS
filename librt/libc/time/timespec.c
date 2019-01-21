/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * TimeSpec Support Definitions & Structures
 * - This header describes the base timespec-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <os/mollenos.h>
#include <time.h>
#include "local.h"

int
timespec_get(
    _In_ struct timespec* ts,
    _In_ int              base)
{
    SystemTime_t    SystemTime = { { { 0 } } };
	struct tm       Temporary  = { 0 };
    LargeUInteger_t Tick       = { { 0 } };

    if (ts == NULL) {
        return -1;
    }

    // Update based on type
    switch (base) {
        case TIME_TAI:
        case TIME_UTC: {
            if (GetSystemTime(&SystemTime) == OsSuccess) {
                if (base == TIME_UTC) {
                    Temporary.tm_sec  = SystemTime.Second;
                    Temporary.tm_min  = SystemTime.Minute;
                    Temporary.tm_hour = SystemTime.Hour;
                    Temporary.tm_mday = SystemTime.DayOfMonth;
                    Temporary.tm_mon  = SystemTime.Month - 1;
                    Temporary.tm_year = SystemTime.Year;
                    ts->tv_sec        = mktime(&Temporary);
                }
                else {
                    ts->tv_sec = SystemTime.Second + (SystemTime.Minute * SECSPERMIN) +
                        (SystemTime.Hour * SECSPERHOUR) + ((SystemTime.DayOfMonth - 1) * SECSPERDAY) +
                        ((SystemTime.Month - 1) * (SECSPERDAY * 30)) + ((SystemTime.Year * DAYSPERYEAR) * SECSPERDAY);
                }
                ts->tv_nsec = (long)SystemTime.Nanoseconds.QuadPart;
            }
            else {
                return -1;
            }
        } break;
        case TIME_MONOTONIC:
        case TIME_PROCESS:
        case TIME_THREAD: {
            GetSystemTick(base, &Tick);
            ts->tv_sec  = (time_t)(Tick.QuadPart / CLOCKS_PER_SEC);
            ts->tv_nsec = (long)((Tick.QuadPart % CLOCKS_PER_SEC) * NSEC_PER_MSEC);
        } break;

        default:
            break;
    }
    return 0;
}

/* timespec_diff
 * The difference between two timespec with the same base. Result
 * is stored in static storage provided by user. */
void
timespec_diff(
    _In_ const struct timespec* start,
    _In_ const struct timespec* stop,
    _In_ struct timespec*       result)
{
    if ((stop->tv_nsec - start->tv_nsec) < 0) {
        result->tv_sec = stop->tv_sec - start->tv_sec - 1;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
    } else {
        result->tv_sec = stop->tv_sec - start->tv_sec;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec;
    }
}
