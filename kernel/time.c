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
 * MollenOS MCore - Timer Mangement Interface
 * - Contains the timer interface system and its implementation
 *   keeps track of system timers
 */
#define __MODULE        "TMIF"
//#define __TRACE

#include "../librt/libc/time/local.h"
#include <arch/interrupts.h>
#include <arch/utils.h>
#include <arch/time.h>
#include <ds/list.h>
#include <interrupts.h>
#include <scheduler.h>
#include <threading.h>
#include <machine.h>
#include <timers.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include <heap.h>

static SystemPerformanceTimerOps_t PerformanceTimer  = { 0 };
static SystemTimer_t*              ActiveSystemTimer = NULL;
static list_t                      SystemTimers      = LIST_INIT;
static long                        AccumulatedDrift  = 0;

void
TimersSynchronizeTime(void)
{
    // InterruptDisable();
    // ArchSynchronizeSystemTime();
    ArchGetSystemTime(&GetMachine()->SystemTime);
    if (ActiveSystemTimer != NULL) {
        ActiveSystemTimer->ResetTick();
    }
    GetMachine()->SystemTime.Nanoseconds.QuadPart = 0;
    // InterruptEnable();
}

OsStatus_t
TimersRegisterSystemTimer(
    _In_ UUId_t  Source, 
    _In_ size_t  TickNs,
    _In_ clock_t (*GetTickFn)(void),
    _In_ void    (*ResetTickFn)(void))
{
    SystemTimer_t*     SystemTimer;
    SystemInterrupt_t* Interrupt;
    int                Delta = abs(NSEC_PER_MSEC - (int)TickNs);

    TRACE("TimersRegisterSystemTimer()");

    // Do some validation about the timer source 
    // the only system timers we want are fast_interrupts
    Interrupt = InterruptGet(Source);
    if (Interrupt == NULL) {
        TRACE("Interrupt was not found for source %" PRIuIN "", Source);
        return OsError;
    }

    // Create a new instance of a system timer
    SystemTimer = (SystemTimer_t*)kmalloc(sizeof(SystemTimer_t));
    if (!SystemTimer) {
        return OsOutOfMemory;
    }
    
    memset(SystemTimer, 0, sizeof(SystemTimer_t));
    ELEMENT_INIT(&SystemTimer->Header, 0, SystemTimer);
    SystemTimer->Source    = Source;
    SystemTimer->TickInNs  = TickNs;
    SystemTimer->Ticks     = 0;
    SystemTimer->GetTick   = GetTickFn;
    SystemTimer->ResetTick = ResetTickFn;
    list_append(&SystemTimers, &SystemTimer->Header);

    // Ok, for a system timer we want something optimum of 1 ms per interrupt
    if (ActiveSystemTimer != NULL) {
        int ActiveDelta = abs(NSEC_PER_MSEC - ActiveSystemTimer->TickInNs);
        if (ActiveDelta > Delta) {
            ActiveSystemTimer = SystemTimer;
        }
    }
    else {
        ActiveSystemTimer = SystemTimer;
    }
    TRACE("New system timer: %" PRIuIN "", ActiveSystemTimer->Source);
    return OsSuccess;
}

OsStatus_t
TimersRegisterPerformanceTimer(
    _In_ SystemPerformanceTimerOps_t* Operations)
{
    PerformanceTimer.ReadFrequency = Operations->ReadFrequency;
    PerformanceTimer.ReadTimer = Operations->ReadTimer;
    return OsSuccess;
}

OsStatus_t
TimersGetSystemTick(
    _Out_ clock_t* SystemTick)
{
    // Sanitize
    if (ActiveSystemTimer == NULL || ActiveSystemTimer->GetTick == NULL) {
        *SystemTick = 0;
        return OsError;
    }
    *SystemTick = ActiveSystemTimer->GetTick();
    return OsSuccess;
}

OsStatus_t
TimersQueryPerformanceFrequency(
    _Out_ LargeInteger_t* Frequency)
{
    if (PerformanceTimer.ReadFrequency == NULL) {
        return OsError;
    }
    PerformanceTimer.ReadFrequency(Frequency);
    return OsSuccess;
}

OsStatus_t
TimersQueryPerformanceTick(
    _Out_ LargeInteger_t* Value)
{
    if (PerformanceTimer.ReadTimer == NULL) {
        return OsError;
    }
    PerformanceTimer.ReadTimer(Value);
    return OsSuccess;
}

void
UpdateSystemTime(size_t Nanoseconds)
{
    SystemTime_t* Time        = &GetMachine()->SystemTime;
    int           IsLeap      = 0;
    int           DaysInMonth = 0;

    // Update the nanoseconds and handle rollover
    Time->Nanoseconds.QuadPart += Nanoseconds;
    if (Time->Nanoseconds.QuadPart > NSEC_PER_SEC) {
        Time->Nanoseconds.QuadPart -= NSEC_PER_SEC;
        Time->Second++;
        if (Time->Second == SECSPERMIN) {
            Time->Second = 0;
            Time->Minute++;
            if (Time->Minute == MINSPERHOUR) {
                Time->Minute = 0;
                Time->Hour++;
                if (Time->Hour == HOURSPERDAY) {
                    Time->Hour  = 0;
                    IsLeap      = isleap(Time->Year);
                    DaysInMonth = __month_lengths[IsLeap][Time->Month - 1];
                    Time->DayOfMonth++;
                    if (Time->DayOfMonth > DaysInMonth) {
                        Time->DayOfMonth = 1;
                        Time->Month++;
                        if (Time->Month > MONSPERYEAR) {
                            Time->Month = 0;
                            Time->Year++;
                        }
                    }
                }
            }
        }
    }
}

OsStatus_t
TimersInterrupt(
    _In_ UUId_t Source)
{
    if (ActiveSystemTimer != NULL) {
        if (ActiveSystemTimer->Source == Source) {
            size_t MilliTicks = DIVUP(ActiveSystemTimer->TickInNs, NSEC_PER_MSEC);
            AccumulatedDrift += NSEC_PER_MSEC - (long)ActiveSystemTimer->TickInNs;
            if (AccumulatedDrift >= NSEC_PER_MSEC)       { MilliTicks++; AccumulatedDrift -= NSEC_PER_MSEC; }
            else if (AccumulatedDrift <= -NSEC_PER_MSEC) { MilliTicks--; AccumulatedDrift += NSEC_PER_MSEC; }
            if (MilliTicks != 0)                         { /* */ }
            UpdateSystemTime(ActiveSystemTimer->TickInNs);
            return OsSuccess;
        }
    }
    return OsError;
}
