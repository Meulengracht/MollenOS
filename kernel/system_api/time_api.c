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

#include <arch/utils.h>
#include <component/timer.h>
#include <debug.h>
#include <machine.h>
#include <os/mollenos.h>
#include <string.h>
#include <threading.h>

OsStatus_t
ScSystemClockTick(
    _In_ enum VaClockSourceType source,
    _In_ LargeUInteger_t*       tickOut)
{
    if (!tickOut) {
        return OsInvalidParameters;
    }

    switch (source) {
        case VaClockSourceType_HPC:
            return SystemTimerGetPerformanceTick(tickOut);

        case VaClockSourceType_THREAD:
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
    return OsSuccess;
}

OsStatus_t
ScSystemClockFrequency(
        _In_ enum VaClockSourceType source,
        _In_ LargeUInteger_t*       frequencyOut)
{
    if (!frequencyOut) {
        return OsInvalidParameters;
    }

    if (source == VaClockSourceType_HPC) {
        return SystemTimerGetPerformanceFrequency(frequencyOut);
    }

    SystemTimerGetClockFrequency(frequencyOut);
    return OsSuccess;
}

OsStatus_t
ScSystemWallClock(
        _In_ LargeInteger_t* time)
{
    if (time == NULL) {
        return OsInvalidParameters;
    }
    SystemTimerGetWallClockTime(time);
    return OsSuccess;
}

OsStatus_t
ScTimeSleep(
        _In_      LargeUInteger_t* duration,
        _Out_Opt_ LargeUInteger_t* remainingOut)
{
    OsStatus_t osStatus;
    clock_t    start;
    clock_t    end;

    if (!duration) {
        return OsInvalidParameters;
    }
    TRACE("ScTimeSleep(duration=%llu)", duration->QuadPart);

    SystemTimerGetTimestamp(&start);
    osStatus = SchedulerSleep(duration->QuadPart, &end);
    if (osStatus == OsInterrupted && remainingOut) {
        SystemTimerGetTimestamp(&end);
        remainingOut->QuadPart = duration->QuadPart - MAX((end - start), duration->QuadPart);
    }
    TRACE("ScTimeSleep returns=%u", osStatus);
    return osStatus;
}

OsStatus_t
ScTimeStall(
        _In_ LargeUInteger_t* duration)
{
    clock_t current;
    clock_t end;
    if (!duration) {
        return OsInvalidParameters;
    }

    SystemTimerGetTimestamp(&current);
    end = current + duration->QuadPart;
    while (current < end) {
        SystemTimerGetTimestamp(&current);
    }
    return OsSuccess;
}
