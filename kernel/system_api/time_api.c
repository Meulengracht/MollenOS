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
 * System Calls
 */

#define __TRACE

#define __need_minmax
#include <arch/utils.h>
#include <component/timer.h>
#include <debug.h>
#include <machine.h>
#include <string.h>
#include <threading.h>

oserr_t
ScSystemClockTick(
        _In_ enum OSClockSource source,
        _In_ UInteger64_t*          tickOut)
{
    if (!tickOut) {
        return OS_EINVALPARAMS;
    }

    switch (source) {
        case OSClockSource_HPC:
            return SystemTimerGetPerformanceTick(tickOut);

        case OSClockSource_THREAD:
            SystemTimerGetClockTick(tickOut);
            Thread_t* Thread = ThreadCurrentForCore(ArchGetProcessorCoreId());
            if (Thread != NULL) {
                tickOut->QuadPart -= ThreadStartTime(Thread)->QuadPart;
            }
            break;

        default:
            SystemTimerGetClockTick(tickOut);
            break;
    }
    return OS_EOK;
}

oserr_t
ScSystemClockFrequency(
        _In_ enum OSClockSource source,
        _In_ UInteger64_t*       frequencyOut)
{
    if (!frequencyOut) {
        return OS_EINVALPARAMS;
    }

    if (source == OSClockSource_HPC) {
        return SystemTimerGetPerformanceFrequency(frequencyOut);
    }

    SystemTimerGetClockFrequency(frequencyOut);
    return OS_EOK;
}

oserr_t
ScSystemTime(
        _In_ enum OSTimeSource source,
        _In_ OSTimestamp_t*    time)
{
    if (time == NULL) {
        return OS_EINVALPARAMS;
    }

    switch (source) {
        case OSTimeSource_MONOTONIC:
            tick_t timestamp;
            SystemTimerGetTimestamp(&timestamp);
            time->Seconds     = (int64_t)(timestamp / NSEC_PER_SEC);
            time->Nanoseconds = (int64_t)(timestamp % NSEC_PER_SEC);
            break;
        case OSTimeSource_THREAD:
        case OSTimeSource_PROCESS:
            // TODO: implement this conversion
            return OS_ENOTSUPPORTED;
        case OSTimeSource_UTC:
            SystemTimerGetWallClockTime(time);
            break;
        default:
            return OS_EINVALPARAMS;
    }

    return OS_EOK;
}

oserr_t
ScTimeSleep(
        _In_      OSTimestamp_t* deadline,
        _Out_Opt_ OSTimestamp_t* remainingOut)
{
    oserr_t oserr;

    if (deadline == NULL) {
        return OS_EINVALPARAMS;
    }
    TRACE("ScTimeSleep(duration=xx)");

    oserr = SchedulerSleep(deadline);
    if (oserr == OS_EINTERRUPTED && remainingOut) {
        OSTimestamp_t now;
        SystemTimerGetWallClockTime(&now);
        OSTimestampSubtract(remainingOut, &now, deadline);
    }
    TRACE("ScTimeSleep returns=%u", oserr);
    return oserr;
}

oserr_t
ScTimeStall(
        _In_ UInteger64_t* duration)
{
    clock_t current;
    clock_t end;
    if (!duration) {
        return OS_EINVALPARAMS;
    }

    SystemTimerGetTimestamp(&current);
    end = current + duration->QuadPart;
    while (current < end) {
        SystemTimerGetTimestamp(&current);
    }
    return OS_EOK;
}
