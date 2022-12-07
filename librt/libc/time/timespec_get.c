/**
 * Copyright 2022, Philip Meulengracht
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
 */

#include <errno.h>
#include <os/time.h>
#include <os/mollenos.h>
#include <time.h>
#include "local.h"

static enum OSTimeSource
__OSTimeSource(
        _In_ int base)
{
    if (base == TIME_THREAD) {
        return OSTimeSource_THREAD;
    } else if (base == TIME_PROCESS) {
        return OSTimeSource_PROCESS;
    } else if (base == TIME_UTC || base == TIME_TAI) {
        return OSTimeSource_UTC;
    }
    return OSTimeSource_MONOTONIC;
}

int
timespec_get(
    _In_ struct timespec* ts,
    _In_ int              base)
{
    OSTimestamp_t timeValue;
    oserr_t       oserr;

    if (!ts) {
        _set_errno(EINVAL);
        return -1;
    }

    oserr = OSGetTime(__OSTimeSource(base), &timeValue);
    if (oserr != OS_EOK) {
        return OsErrToErrNo(oserr);
    }

    ts->tv_sec  = timeValue.Seconds;
    ts->tv_nsec = timeValue.Nanoseconds;
    if (base == TIME_TAI) {
        // TODO adjust for leap seconds
    }
    return 0;
}
