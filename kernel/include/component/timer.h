/**
 * Copyright 2018, Philip Meulengracht
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

#ifndef __COMPONENT_TIMER__
#define __COMPONENT_TIMER__

#include <os/mollenos.h>
#include <ds/list.h>
#include <time.h>

typedef clock_t tick_t;

/**
 * SystemTime is a helper structure that represents onboard time in UTC.
 */
typedef struct SystemTime {
    int Second;     // Range 0-59
    int Minute;     // Range 0-59
    int Hour;       // Range 0-23
    int DayOfMonth; // Range 0-31
    int Month;      // Range 0-11
    int Year;
} SystemTime_t;

enum SystemTimeAttributes {
    SystemTimeAttributes_COUNTER    = 0x1,
    SystemTimeAttributes_IRQ        = 0x2,
    SystemTimeAttributes_CALIBRATED = 0x4,
    SystemTimeAttributes_HPC        = 0x8
};

typedef struct SystemTimerOperations {
    void (*Read)(void*, UInteger64_t*);
    void (*GetFrequency)(void*, UInteger64_t*);
    void (*Recalibrate)(void*);
} SystemTimerOperations_t;

typedef struct SystemTimer {
    element_t                 ListHeader;
    SystemTimerOperations_t   Operations;
    enum SystemTimeAttributes Attributes;
    uuid_t                    Interrupt;
    tick_t                    Resolution;
    UInteger64_t           InitialTick;
    void*                     Context;
} SystemTimer_t;

typedef struct SystemWallClockOperations {
    void (*Read)(void*, SystemTime_t*);
} SystemWallClockOperations_t;

typedef struct SystemWallClock {
    SystemWallClockOperations_t Operations;
    Integer64_t              BaseTick;
    void*                       Context;
} SystemWallClock_t;

typedef struct SystemTimers {
    list_t             Timers; // list<SystemTimer_t*>
    SystemWallClock_t* WallClock;
    SystemTimer_t*     Clock;
    SystemTimer_t*     Hpc;
} SystemTimers_t;
#define SYSTEM_TIMERS_INIT { LIST_INIT, NULL, NULL, NULL }

/**
 * @brief Registers a new system timer with the Machine.
 *
 * @param[In] operations A pointer to various operations the timer supports
 * @param[In] attributes The attributes/features of the timer
 * @param[In] interrupt  If the timer is occupying an interrupt, this is its interrupt handle
 * @param[In] context    A context pointer that will be passed to operations
 * @return
 */
KERNELAPI oserr_t KERNELABI
SystemTimerRegister(
        _In_ SystemTimerOperations_t*  operations,
        _In_ enum SystemTimeAttributes attributes,
        _In_ uuid_t                    interrupt,
        _In_ void*                     context);

/**
 * @brief Registers a new wall clock source with the machine instance. The wall clock will
 * be immediately synchronized with a timer source once threading is up and running. Only one
 * wall clock can be registered.
 *
 * @param[In] operations A pointer to various operations the wall clock supports
 * @param[In] context    A context pointer that will be passed to operations
 * @return OsExists if a clock source is already registered
 */
KERNELAPI oserr_t KERNELABI
SystemWallClockRegister(
        _In_ SystemWallClockOperations_t* operations,
        _In_ void*                        context);

/**
 * @brief Retrieves the current system wall clock. The wall clock is retrieved as
 * a 64 bit signed timestamp in microseconds since January 1, 2000 UTC.
 *
 * @param[In] time A pointer to where to store the time.
 */
KERNELAPI void KERNELABI
SystemTimerGetWallClockTime(
        _In_ Integer64_t* time);

/**
 * @brief Retrieves the current system tick as a timestamp
 *
 * @param[Out] timestampOut A pointer to where to store the timestamp.
 */
KERNELAPI void KERNELABI
SystemTimerGetTimestamp(
        _Out_ tick_t* timestampOut);

/**
 * @brief Retrieves the system tick counter
 *
 * @param[Out] tickOut The current system tick will be stored here
 */
KERNELAPI void KERNELABI
SystemTimerGetClockTick(
        _In_ UInteger64_t* tickOut);

/**
 * @brief Retrieves the system tick frequency
 *
 * @param[Out] tickOut The current system frequency will be stored here
 */
KERNELAPI void KERNELABI
SystemTimerGetClockFrequency(
        _In_ UInteger64_t* frequencyOut);

/**
 * @brief Retrieves the frequency of the high performance counter (if present)
 *
 * @param[Out] frequency A pointer where to store the frequency.
 * @return     Returns OsNotSupported if HPC is not supported, otherwise OS_EOK.
 */
KERNELAPI oserr_t KERNELABI
SystemTimerGetPerformanceFrequency(
        _Out_ UInteger64_t* frequency);

/**
 * @brief Retrieves the current tick of the high performance counter (if present)
 *
 * @param[Out] tick A pointer where to store the current tick.
 * @return     Returns OsNotSupported if HPC is not supported, otherwise OS_EOK.
 */
KERNELAPI oserr_t KERNELABI
SystemTimerGetPerformanceTick(
        _Out_ UInteger64_t* tick);

/**
 * @brief Stalls the CPU for the a specified amount of time in nanoseconds resolution.
 *
 * @param[In] ns The minimum number of nanoseconds to stall for.
 */
KERNELAPI void KERNELABI
SystemTimerStall(
        _In_ tick_t ns);

#endif // !__COMPONENT_MEMORY__
