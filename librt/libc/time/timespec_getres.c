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

static long
__calculate_resolution(
        _In_ UInteger64_t* frequency)
{
    if (frequency->QuadPart <= MSEC_PER_SEC) {
        // ms resolution
        return NSEC_PER_MSEC * (MSEC_PER_SEC / frequency->QuadPart);
    }

    if (frequency->QuadPart <= USEC_PER_SEC) {
        // us resolution
        return NSEC_PER_USEC * (USEC_PER_SEC / frequency->QuadPart);
    }

    // ns resolution
    return NSEC_PER_SEC / frequency->QuadPart;
}


static enum OSClockSource
__get_va_type(
        _In_ int base)
{
    if (base == TIME_THREAD) {
        return OSClockSource_THREAD;
    }
    else if (base == TIME_PROCESS) {
        return OSClockSource_PROCESS;
    }
    return OSClockSource_MONOTONIC;
}

int
timespec_getres(
    _In_ struct timespec* ts,
    _In_ int              base)
{
    UInteger64_t frequency;

    if (!ts) {
        _set_errno(EINVAL);
        return -1;
    }

    OSGetClockFrequency(__get_va_type(base), &frequency);

    ts->tv_sec = 0;
    ts->tv_nsec = __calculate_resolution(&frequency);
    return 0;
}
