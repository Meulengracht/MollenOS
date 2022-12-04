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

#include <errno.h>
#include <os/mollenos.h>
#include <os/time.h>
#include <time.h>
#include "local.h"

static enum OSClockSource
__get_va_type(
        _In_ int base)
{
    if (base == TIME_THREAD) {
        return OSClockSource_THREAD;
    } else if (base == TIME_PROCESS) {
        return OSClockSource_PROCESS;
    }
    return OSClockSource_MONOTONIC;
}

static void
__calculate_timestamp(
        _In_ struct timespec* ts,
        _In_ UInteger64_t*    tick)
{
    uint64_t freq = CLOCKS_PER_SEC;
    uint64_t mult = NSEC_PER_SEC / freq;
    ts->tv_sec = (time_t)(tick->QuadPart / freq);
    ts->tv_nsec = (long)((tick->QuadPart % freq) * mult);
}

int
timespec_get(
    _In_ struct timespec* ts,
    _In_ int              base)
{
    oserr_t osStatus;

    if (!ts) {
        _set_errno(EINVAL);
        return -1;
    }

    // Update based on type
    switch (base) {
        case TIME_TAI:
        case TIME_UTC: {
            OSTimestamp_t timeValue;

            osStatus = OSGetWallClock(&timeValue);
            if (osStatus != OS_EOK) {
                return OsErrToErrNo(osStatus);
            }

            ts->tv_sec  = timeValue.Seconds;
            ts->tv_nsec = timeValue.Nanoseconds;
            if (base == TIME_TAI) {
                // TODO adjust for leap seconds
            }
        } break;
        case TIME_MONOTONIC:
        case TIME_PROCESS:
        case TIME_THREAD: {
            UInteger64_t tick;

            // TODO: introduce a new syscall for this case
            osStatus = OSGetClockTick(__get_va_type(base), &tick);
            if (osStatus != OS_EOK) {
                return OsErrToErrNo(osStatus);
            }
            __calculate_timestamp(ts, &tick);
        } break;

        default:
            break;
    }
    return 0;
}
