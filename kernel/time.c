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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
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
TimeWallClockAddTime(
        _In_ int seconds)
{
    SystemTime_t* systemTime = &GetMachine()->SystemTime;
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
