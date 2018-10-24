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

#include <ds/collection.h>
#include <process/ash.h>
#include <interrupts.h>
#include <scheduler.h>
#include <machine.h>
#include <timers.h>
#include <stdlib.h>
#include <debug.h>
#include <heap.h>

static MCoreTimePerformanceOps_t PerformanceTimer   = { 0 };
static CriticalSection_t TimersSyncObject           = CRITICALSECTION_INITIALIZE(CRITICALSECTION_PLAIN);
static MCoreSystemTimer_t *ActiveSystemTimer        = NULL;
static Collection_t SystemTimers                    = COLLECTION_INIT(KeyInteger);
static Collection_t Timers                          = COLLECTION_INIT(KeyInteger);
static _Atomic(UUId_t) TimerIdGenerator             = ATOMIC_VAR_INIT(0);

/* TimersStart 
 * Creates a new standard timer for the requesting process. */
UUId_t
TimersStart(
    _In_ size_t         IntervalNs,
    _In_ int            Periodic,
    _In_ const void*    Data)
{
    // Variables
    MCoreTimer_t *Timer = NULL;
    DataKey_t Key;

    // Sanity
    if (GetCurrentProcess() == NULL) {
        return UUID_INVALID;
    }

    // Allocate a new instance and initialize
    Timer = (MCoreTimer_t*)kmalloc(sizeof(MCoreTimer_t));
    Timer->Id       = atomic_fetch_add(&TimerIdGenerator, 1);
    Timer->AshId    = GetCurrentProcess()->Id;
    Timer->Data     = Data;
    Timer->Interval = IntervalNs;
    Timer->Current  = 0;
    Timer->Periodic = Periodic;
    Key.Value       = (int)Timer->Id;

    // Add to list of timers
    CollectionAppend(&Timers, CollectionCreateNode(Key, Timer));
    return Timer->Id;
}

/* TimersStop
 * Destroys a existing standard timer, owner must be the requesting
 * process. Otherwise access fault. */
OsStatus_t
TimersStop(
    _In_ UUId_t TimerId)
{
    // Variables
    OsStatus_t Result = OsError;

    // Now loop through timers registered
    CriticalSectionEnter(&TimersSyncObject);
    foreach(tNode, &Timers) {
        // Initiate pointer
        MCoreTimer_t *Timer = (MCoreTimer_t*)tNode->Data;
        
        // Does it match the id? + Owner must match
        if (Timer->Id == TimerId && Timer->AshId == GetCurrentProcess()->Id) {
            CollectionRemoveByNode(&Timers, tNode);
            kfree(Timer);
            kfree(tNode);
            Result = OsSuccess;
            break;
        }
    }
    CriticalSectionLeave(&TimersSyncObject);
    return Result;
}

/* TimersTick
 * This method actually applies the new tick-delta to all
 * active timers registered, and decreases the total */
void
TimersTick(
    _In_ size_t Tick)
{
    CollectionItem_t *i;
    size_t MilliTicks = DIVUP(Tick, NSEC_PER_MSEC);

    // Update scheduler ticks, handle timers
    SchedulerTick(MilliTicks);
    _foreach_nolink(i, &Timers) {
        MCoreTimer_t *Timer  = (MCoreTimer_t*)i->Data;
        Timer->Current      -= MIN(Timer->Current, Tick);

        if (Timer->Current == 0) {
            __KernelTimeoutDriver(Timer->AshId, Timer->Id, (void*)Timer->Data);
            if (Timer->Periodic) {
                Timer->Current = Timer->Interval;
            }
            else {
                CollectionItem_t* Temp = i;
                i = CollectionUnlinkNode(&Timers, Temp);
                kfree(Timer);
                kfree(Temp);
                continue;
            }
        }
        i = CollectionNext(i);
    }
}

/* TimersRegistrate
 * Registrates a interrupt timer source with the
 * timer management, which keeps track of which interrupts
 * are available for time-keeping */
OsStatus_t
TimersRegisterSystemTimer(
    _In_ UUId_t     Source, 
    _In_ size_t     TickNs,
    _In_ clock_t    (*SystemTickHandler)(void))
{
    // Variables
    MCoreSystemTimer_t* SystemTimer;
    SystemInterrupt_t* Interrupt;
    DataKey_t tKey;
    int Delta = abs(1000 - (int)TickNs);

    // Trace
    TRACE("TimersRegisterSystemTimer()");

    // Do some validation about the timer source 
    // the only system timers we want are fast_interrupts
    Interrupt = InterruptGet(Source);
    if (Interrupt == NULL) {
        TRACE("Interrupt was not found for source %u", Source);
        return OsError;
    }

    // Create a new instance of a system timer
    SystemTimer = (MCoreSystemTimer_t*)kmalloc(sizeof(MCoreSystemTimer_t));
    SystemTimer->Source     = Source;
    SystemTimer->Tick       = TickNs;
    SystemTimer->Ticks      = 0;
    SystemTimer->SystemTick = SystemTickHandler;

    // Add the new timer to the list
    tKey.Value = 0;
    CollectionAppend(&SystemTimers, CollectionCreateNode(tKey, SystemTimer));

    // Ok, for a system timer we want something optimum
    // of 1 ms per interrupt
    if (ActiveSystemTimer != NULL) {
        int ActiveDelta = abs(1000 - ActiveSystemTimer->Tick);
        if (ActiveDelta > Delta) {
            ActiveSystemTimer = SystemTimer;
        }
    }
    else {
        ActiveSystemTimer = SystemTimer;
    }

    // Trace
    TRACE("New system timer: %u", ActiveSystemTimer->Source);
    return OsSuccess;
}

/* TimersRegisterPerformanceTimer
 * Registers a high performance timer that can be seperate
 * from the system timer. */
OsStatus_t
TimersRegisterPerformanceTimer(
    _In_ MCoreTimePerformanceOps_t *Operations)
{
    PerformanceTimer.ReadFrequency = Operations->ReadFrequency;
    PerformanceTimer.ReadTimer = Operations->ReadTimer;
    return OsSuccess;
}

/* TimersRegisterClock
 * Registers a new time clock source. Must use the standard
 * C library definitions of time. */
OsStatus_t
TimersRegisterClock(
    _In_ void (*SystemTimeHandler)(struct tm *SystemTime))
{
    PerformanceTimer.ReadSystemTime = SystemTimeHandler;
    return OsSuccess;
}

/* TimersInterrupt
 * Called by the interrupt-code to tell the timer-management system
 * a new interrupt has occured from the given source. This allows
 * the timer-management system to tell us if that was the active
 * timer-source */
OsStatus_t
TimersInterrupt(
    _In_ UUId_t Source)
{
    // Sanitize if the source is ok
    if (ActiveSystemTimer != NULL) {
        if (ActiveSystemTimer->Source == Source) {
            TimersTick(ActiveSystemTimer->Tick);
            return OsSuccess;
        }
    }
    return OsError;
}

/* TimersGetSystemTime
 * Retrieves the system time. This is only ticking if a system clock has been initialized. */
OsStatus_t
TimersGetSystemTime(
    _Out_ struct tm *SystemTime)
{
    if (PerformanceTimer.ReadSystemTime == NULL) {
        return OsError;
    }
    PerformanceTimer.ReadSystemTime(SystemTime);
    return OsSuccess;
}

/* TimersGetSystemTick 
 * Retrieves the system tick counter. This is only ticking if a system timer has been initialized. 
 * Otherwise the GetSystemTick will always return 1. */
OsStatus_t
TimersGetSystemTick(
    _Out_ clock_t *SystemTick)
{
    // Sanitize
    if (ActiveSystemTimer == NULL || ActiveSystemTimer->SystemTick == NULL) {
        *SystemTick = 1;
        return OsError;
    }
    *SystemTick = ActiveSystemTimer->SystemTick();
    return OsSuccess;
}

/* TimersQueryPerformanceFrequency
 * Returns how often the performance timer fires every
 * second, the value will never be 0 */
OsStatus_t
TimersQueryPerformanceFrequency(
    _Out_ LargeInteger_t *Frequency)
{
    if (PerformanceTimer.ReadFrequency == NULL) {
        return OsError;
    }
    PerformanceTimer.ReadFrequency(Frequency);
    return OsSuccess;
}

/* TimersQueryPerformanceTick 
 * Retrieves the system performance tick counter. This is only ticking
 * if a system performance timer has been initialized. */
OsStatus_t
TimersQueryPerformanceTick(
    _Out_ LargeInteger_t *Value)
{
    if (PerformanceTimer.ReadTimer == NULL) {
        return OsError;
    }
    PerformanceTimer.ReadTimer(Value);
    return OsSuccess;
}
