/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
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
timespec_getres(
    _In_ struct timespec* ts,
    _In_ int              base)
{
    SystemTime_t    systemTime = { { { 0 } } };
	struct tm       temporary = { 0 };
    LargeUInteger_t tick      = { { 0 } };

    if (ts == NULL) {
        return -1;
    }

    // Update based on type
    switch (base) {
        case TIME_TAI:
        case TIME_UTC: {
            if (VaGetWallClock(&systemTime) == OsSuccess) {
                if (base == TIME_UTC) {
                    temporary.tm_sec  = systemTime.Second;
                    temporary.tm_min  = systemTime.Minute;
                    temporary.tm_hour = systemTime.Hour;
                    temporary.tm_mday = systemTime.DayOfMonth;
                    temporary.tm_mon  = systemTime.Month - 1;
                    temporary.tm_year = systemTime.Year - YEAR_BASE;
                    ts->tv_sec        = mktime(&temporary);
                }
                else {
                    ts->tv_sec = systemTime.Second + (systemTime.Minute * SECSPERMIN) +
                                 (systemTime.Hour * SECSPERHOUR) + ((systemTime.DayOfMonth - 1) * SECSPERDAY) +
                                 ((systemTime.Month - 1) * (SECSPERDAY * 30)) + ((systemTime.Year * 365) * SECSPERDAY);
                }
                ts->tv_nsec = (long)systemTime.Nanoseconds.QuadPart;
            }
            else {
                return -1;
            }
        } break;
        case TIME_MONOTONIC:
        case TIME_PROCESS:
        case TIME_THREAD: {
            VaGetTimeTick(base, &tick);
            ts->tv_sec  = (time_t)(tick.QuadPart / CLOCKS_PER_SEC);
            ts->tv_nsec = (long)((tick.QuadPart % CLOCKS_PER_SEC) * NSEC_PER_MSEC);
        } break;

        default:
            break;
    }
    return 0;
}
