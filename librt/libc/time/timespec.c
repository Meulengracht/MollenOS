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
 * MollenOS MCore - TimeSpec Support Definitions & Structures
 * - This header describes the base timespec-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <os/mollenos.h>
#include <time.h>

/* timespec_get
 * 1. Modifies the timespec object pointed to by ts to hold the current calendar 
 *    time in the time base base.
 * 2. Expands to a value suitable for use as the base argument of timespec_get
 * Other macro constants beginning with TIME_ may be provided by the implementation 
 * to indicate additional time bases. If base is TIME_UTC, then
 * 1. ts->tv_sec is set to the number of seconds since an implementation defined epoch, 
 *    truncated to a whole value
 * 2. ts->tv_nsec member is set to the integral number of nanoseconds, rounded to the 
 *    resolution of the system clock*/
int
timespec_get(
    _In_ struct timespec*   ts,
    _In_ int                base)
{
    clock_t tick = 0;

    // Sanitize input
    if (ts == NULL) {
        return -1;
    }

    // Update based on type
    switch (base) {
        case TIME_UTC: {
            ts->tv_sec  = time(NULL);
            ts->tv_nsec = 0;
        } break;
        case TIME_TAI: {
            // @todo
        } break;
        case TIME_MONOTONIC:
        case TIME_PROCESS:
        case TIME_THREAD: {
            SystemTick(base, &tick);
            ts->tv_sec  = tick / CLOCKS_PER_SEC;
            ts->tv_nsec = (tick % CLOCKS_PER_SEC) * NSEC_PER_MSEC;
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
