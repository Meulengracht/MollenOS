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
        _In_ LargeUInteger_t* frequency)
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

OsStatus_t
SystemTimerRegister(
        _In_ SystemTimerOperations_t*  operations,
        _In_ enum SystemTimeAttributes attributes,
        _In_ UUId_t                    interrupt,
        _In_ void*                     context)
{
    SystemTimer_t*  systemTimer;
    LargeUInteger_t frequency;
    TRACE("SystemTimerRegister(attributes=0x%x)", attributes);

    systemTimer = (SystemTimer_t*)kmalloc(sizeof(SystemTimer_t));
    if (!systemTimer) {
        return OsOutOfMemory;
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
    return OsSuccess;
}

void
SystemTimerGetTimestamp(
        _Out_ tick_t* timestampOut)
{
    SystemTimer_t*  clock = GetMachine()->SystemTimers.Clock;
    LargeUInteger_t frequency;
    LargeUInteger_t tick;

    // guard against early calls from the log
    if (!clock) {
        *timestampOut = 0;
        return;
    }

    // get clock precision metrics
    clock->Operations.Read(clock->Context, &tick);
    clock->Operations.GetFrequency(clock->Context, &frequency);

    if (frequency.QuadPart <= MSEC_PER_SEC) {
        *timestampOut = (MSEC_PER_SEC / frequency.QuadPart) * tick.QuadPart * NSEC_PER_MSEC;
    }
    else if (frequency.QuadPart <= USEC_PER_SEC) {
        *timestampOut = ((USEC_PER_SEC / frequency.QuadPart) * tick.QuadPart) * NSEC_PER_USEC;
    }
    else { // we assume ns
        *timestampOut = ((NSEC_PER_SEC / frequency.QuadPart) * tick.QuadPart);
    }
}

void
SystemTimerGetClockTick(
        _In_ LargeUInteger_t* tickOut)
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
        _In_ LargeUInteger_t* frequencyOut)
{
    SystemTimer_t* clock = GetMachine()->SystemTimers.Clock;
    if (!clock) {
        frequencyOut->QuadPart = 0;
        return;
    }

    clock->Operations.GetFrequency(clock->Context, frequencyOut);
}

OsStatus_t
SystemTimerGetPerformanceFrequency(
        _Out_ LargeUInteger_t* frequency)
{
    SystemTimer_t* hpc = GetMachine()->SystemTimers.Hpc;
    if (!hpc) {
        return OsNotSupported;
    }
    hpc->Operations.GetFrequency(hpc->Context, frequency);
    return OsSuccess;
}

OsStatus_t
SystemTimerGetPerformanceTick(
        _Out_ LargeUInteger_t* tick)
{
    SystemTimer_t* hpc = GetMachine()->SystemTimers.Hpc;
    if (!hpc) {
        return OsNotSupported;
    }
    hpc->Operations.Read(hpc->Context, tick);
    return OsSuccess;
}

void
SystemTimerStall(
        _In_ tick_t ns)
{
    SystemTimer_t*  clock = GetMachine()->SystemTimers.Clock;
    LargeUInteger_t frequency;
    LargeUInteger_t tick;
    LargeUInteger_t tickEnd;
    uint64_t        vPerTicks;

    assert(clock != NULL);

    // get clock precision metrics
    clock->Operations.Read(clock->Context, &tick);
    clock->Operations.GetFrequency(clock->Context, &frequency);

    // calculate end tick
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

    // wait for it
    while (tick.QuadPart < tickEnd.QuadPart) {
        clock->Operations.Read(clock->Context, &tick);
    }
}

void
SystemTimerWallClockAddTime(
        _In_ int seconds)
{
    SystemTime_t* systemTime = &GetMachine()->SystemTimers.WallClock;
    int           IsLeap;
    int           DaysInMonth;

    systemTime->Second += seconds;
    if (systemTime->Second >= SECSPERMIN) {
        systemTime->Second %= SECSPERMIN;
        systemTime->Minute++;
        if (systemTime->Minute == MINSPERHOUR) {
            systemTime->Minute = 0;
            systemTime->Hour++;
            if (systemTime->Hour == HOURSPERDAY) {
                systemTime->Hour = 0;
                IsLeap      = isleap(systemTime->Year);
                DaysInMonth = __month_lengths[IsLeap][systemTime->Month - 1];
                systemTime->DayOfMonth++;
                if (systemTime->DayOfMonth > DaysInMonth) {
                    systemTime->DayOfMonth = 1;
                    systemTime->Month++;
                    if (systemTime->Month > MONSPERYEAR) {
                        systemTime->Month = 0;
                        systemTime->Year++;
                    }
                }
            }
        }
    }
}
