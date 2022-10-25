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
 *
 * MollenOS System Component Infrastructure
 * - The Memory component. This component has the task of managing
 *   different memory regions that map to physical components
 */

#define __TRACE

#include <assert.h>
#include <component/timer.h>
#include <debug.h>
#include <heap.h>
#include <machine.h>
#include <string.h>

// for time macros and length of months
#include "../../librt/libc/time/local.h"

static tick_t
__CalculateResolution(
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

    if (frequency->QuadPart <= NSEC_PER_SEC) {
        // ns resolution
        return NSEC_PER_SEC / frequency->QuadPart;
    }
    return frequency->QuadPart / NSEC_PER_SEC;
}

oserr_t
SystemTimerRegister(
        _In_ SystemTimerOperations_t*  operations,
        _In_ enum SystemTimeAttributes attributes,
        _In_ uuid_t                    interrupt,
        _In_ void*                     context)
{
    SystemTimer_t*  systemTimer;
    UInteger64_t frequency;
    TRACE("SystemTimerRegister(attributes=0x%x)", attributes);

    systemTimer = (SystemTimer_t*)kmalloc(sizeof(SystemTimer_t));
    if (!systemTimer) {
        return OS_EOOM;
    }

    // query frequency immediately to calculate the resolution of the timer
    operations->GetFrequency(context, &frequency);
    TRACE("SystemTimerRegister frequency=0x%llx", frequency.QuadPart);

    ELEMENT_INIT(&systemTimer->ListHeader, 0, systemTimer);
    memcpy(&systemTimer->Operations, operations, sizeof(SystemTimerOperations_t));
    systemTimer->Attributes = attributes;
    systemTimer->Interrupt  = interrupt;
    systemTimer->Context    = context;
    systemTimer->Resolution = __CalculateResolution(&frequency);
    TRACE("SystemTimerRegister resolution=0x%llx", systemTimer->Resolution);

    // See if the timer can be used for anything
    if (attributes & SystemTimeAttributes_HPC) {
        GetMachine()->SystemTimers.Hpc = systemTimer;
    }
    else if (attributes & SystemTimeAttributes_COUNTER) {
        operations->Read(context, &systemTimer->InitialTick);

        // Might be a new tick counter, lets compare resolution of
        // what we already have
        if (!GetMachine()->SystemTimers.Clock) {
            GetMachine()->SystemTimers.Clock = systemTimer;
        }
        else {
            // always prefer the clock with the highest resolution
            if (GetMachine()->SystemTimers.Clock->Resolution > systemTimer->Resolution) {
                GetMachine()->SystemTimers.Clock = systemTimer;
            }
        }
    }

    // Store it in the list of available system timers
    list_append(&GetMachine()->SystemTimers.Timers, &systemTimer->ListHeader);
    return OS_EOK;
}

// Our system wall clock is valid down to microseconds precision, which allows us for
// a precision of 292.277 years in either direction. This should be sufficient for our
// needs.
static void __LinearTime(SystemTime_t* time, Integer64_t* linear)
{
    // Ok we convert the format of SystemTime to a total second count. We count
    // time starting from January 1, 2000. (UTC) from the value of 0.
    int isLeap = isleap(time->Year);
    int seconds;
    int days;

    if (time->Year < 2000) {
        // calculate the time left in the day, and the days left in the current year
        seconds = SECSPERDAY - (time->Second + (time->Minute * (int)SECSPERMIN) + (time->Hour * (int)SECSPERHOUR));
        days    = DAYSPERYEAR(time->Year) - (__days_before_month[isLeap][time->Month] + time->DayOfMonth);

        // date is in the past, we must count backwards from that date, start by calculating
        // the full number of days
        for (int i = 1999; i > time->Year; i--) {
            days += DAYSPERYEAR(i);
        }

        // do the last conversion of days to seconds and return that value as a negative
        linear->QuadPart = -(((days * SECSPERDAY) + seconds) * USEC_PER_SEC);
    } else {
        seconds = time->Second + (time->Minute * (int)SECSPERMIN) + (time->Hour * (int)SECSPERHOUR);
        days    = __days_before_month[isLeap][time->Month] + time->DayOfMonth;
        for (int i = 2000; i < time->Year; i++) {
            days += DAYSPERYEAR(i);
        }
        linear->QuadPart = ((days * SECSPERDAY) + seconds) * USEC_PER_SEC;
    }
}

oserr_t
SystemWallClockRegister(
        _In_ SystemWallClockOperations_t* operations,
        _In_ void*                        context)
{
    SystemWallClock_t* clock;

    if (GetMachine()->SystemTimers.WallClock != NULL) {
        return OS_EEXISTS;
    }

    clock = kmalloc(sizeof(SystemWallClock_t));
    if (clock == NULL) {
        return OS_EOOM;
    }

    clock->BaseTick.QuadPart = 0;
    clock->Context = context;
    memcpy(&clock->Operations, operations, sizeof(SystemWallClockOperations_t));

    // store it as our primary wall clock
    GetMachine()->SystemTimers.WallClock = clock;
    return OS_EOK;
}

void
SystemTimerGetWallClockTime(
        _In_ Integer64_t* time)
{
    tick_t timestamp;

    // The wall clock and default system timer are synchronized. Which means
    // we use the BaseTick of the wall clock, and then add clock timestamp
    // to that to get the final time.
    time->QuadPart = GetMachine()->SystemTimers.WallClock->BaseTick.QuadPart;

    // The timestamp is in nanosecond precision, however we want microsecond
    // precision here, so we adjust
    SystemTimerGetTimestamp(&timestamp);
    timestamp /= 1000UL;
    time->QuadPart += (int64_t)timestamp;
}

void
SystemTimerGetTimestamp(
        _Out_ tick_t* timestampOut)
{
    SystemTimer_t*  clock = GetMachine()->SystemTimers.Clock;
    UInteger64_t frequency;
    UInteger64_t tick;

    // guard against early calls from the log
    if (!clock) {
        *timestampOut = 0;
        return;
    }

    // get clock precision metrics
    clock->Operations.Read(clock->Context, &tick);
    clock->Operations.GetFrequency(clock->Context, &frequency);

    // subtract initial timestamp
    tick.QuadPart -= clock->InitialTick.QuadPart;

    if (frequency.QuadPart <= MSEC_PER_SEC) {
        *timestampOut = (MSEC_PER_SEC / frequency.QuadPart) * tick.QuadPart * NSEC_PER_MSEC;
    }
    else if (frequency.QuadPart <= USEC_PER_SEC) {
        *timestampOut = ((USEC_PER_SEC / frequency.QuadPart) * tick.QuadPart) * NSEC_PER_USEC;
    }
    else if (frequency.QuadPart < NSEC_PER_SEC) {
        *timestampOut = ((NSEC_PER_SEC / frequency.QuadPart) * tick.QuadPart);
    }
    else {
        *timestampOut = tick.QuadPart;
    }
}

void
SystemTimerGetClockTick(
        _In_ UInteger64_t* tickOut)
{
    SystemTimer_t* clock = GetMachine()->SystemTimers.Clock;
    if (!clock) {
        tickOut->QuadPart = 0;
        return;
    }

    clock->Operations.Read(clock->Context, tickOut);
}

void
SystemTimerGetClockFrequency(
        _In_ UInteger64_t* frequencyOut)
{
    SystemTimer_t* clock = GetMachine()->SystemTimers.Clock;
    if (!clock) {
        frequencyOut->QuadPart = 0;
        return;
    }

    clock->Operations.GetFrequency(clock->Context, frequencyOut);
}

oserr_t
SystemTimerGetPerformanceFrequency(
        _Out_ UInteger64_t* frequency)
{
    SystemTimer_t* hpc = GetMachine()->SystemTimers.Hpc;
    if (!hpc) {
        return OS_ENOTSUPPORTED;
    }
    hpc->Operations.GetFrequency(hpc->Context, frequency);
    return OS_EOK;
}

oserr_t
SystemTimerGetPerformanceTick(
        _Out_ UInteger64_t* tick)
{
    SystemTimer_t* hpc = GetMachine()->SystemTimers.Hpc;
    if (!hpc) {
        return OS_ENOTSUPPORTED;
    }
    hpc->Operations.Read(hpc->Context, tick);
    return OS_EOK;
}

void
SystemTimerStall(
        _In_ tick_t ns)
{
    SystemTimer_t*  clock = GetMachine()->SystemTimers.Clock;
    UInteger64_t frequency;
    UInteger64_t tick;
    UInteger64_t tickEnd;
    uint64_t        vPerTicks;

    assert(clock != NULL);

    // get clock precision metrics
    clock->Operations.Read(clock->Context, &tick);
    clock->Operations.GetFrequency(clock->Context, &frequency);

    // calculate end tick
    if (NSEC_PER_SEC >= frequency.QuadPart) {
        tickEnd.QuadPart = tick.QuadPart + ns;
    }
    else {
        vPerTicks = NSEC_PER_SEC / frequency.QuadPart;
        if (vPerTicks >= NSEC_PER_USEC) { // USEC precision
            vPerTicks = USEC_PER_SEC / frequency.QuadPart;
            if (vPerTicks >= USEC_PER_MSEC) { // MS precision
                vPerTicks = MSEC_PER_SEC / frequency.QuadPart;
                tickEnd.QuadPart = tick.QuadPart + ((ns / NSEC_PER_MSEC) / vPerTicks) + 1;
            }
            else {
                tickEnd.QuadPart = tick.QuadPart + ((ns / NSEC_PER_USEC) / vPerTicks) + 1;
            }
        }
        else {
            tickEnd.QuadPart = tick.QuadPart + (ns / vPerTicks) + 1;
        }
    }

    // wait for it
    while (tick.QuadPart < tickEnd.QuadPart) {
        clock->Operations.Read(clock->Context, &tick);
    }
}
