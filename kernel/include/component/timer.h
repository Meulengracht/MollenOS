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

enum SystemTimeAttributes {
    SystemTimeAttributes_COUNTER    = 0x1,
    SystemTimeAttributes_IRQ        = 0x2,
    SystemTimeAttributes_CALIBRATED = 0x4,
    SystemTimeAttributes_HPC        = 0x8
};

typedef struct SystemTimerOperations {
    void (*Read)(void*, LargeUInteger_t*);
    void (*GetFrequency)(void*, LargeUInteger_t*);
    void (*Recalibrate)(void*);
} SystemTimerOperations_t;

typedef struct SystemTimer {
    element_t                 ListHeader;
    SystemTimerOperations_t   Operations;
    enum SystemTimeAttributes Attributes;
    UUId_t                    Interrupt;
    tick_t                    Resolution;
    LargeUInteger_t           InitialTick;
    void*                     Context;
} SystemTimer_t;

typedef struct SystemTimers {
    list_t         Timers; // list<SystemTimer_t*>
    SystemTime_t   WallClock;
    SystemTimer_t* Clock;
    SystemTimer_t* Hpc;
} SystemTimers_t;
#define SYSTEM_TIMERS_INIT { LIST_INIT, { 0 }, NULL, NULL }

/**
 * @brief Registers a new system timer with the Machine.
 *
 * @param[In] operations A pointer to various operations the timer can support
 * @param[In] attributes The attributes/features of the timer
 * @param[In] interrupt  If the timer is occupying an interrupt, this is its interrupt handle
 * @param[In] context    A context pointer that will be passed to operations
 * @return
 */
KERNELAPI OsStatus_t KERNELABI
SystemTimerRegister(
        _In_ SystemTimerOperations_t*  operations,
        _In_ enum SystemTimeAttributes attributes,
        _In_ UUId_t                    interrupt,
        _In_ void*                     context);

/**
 * @brief Adds a number of seconds to the system wallclock
 *
 * @param[In] seconds The number of seconds to add to the wallclock
 */
KERNELAPI void KERNELABI
SystemTimerWallClockAddTime(
        _In_ int seconds);

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
        _In_ LargeUInteger_t* tickOut);

/**
 * @brief Retrieves the system tick frequency
 *
 * @param[Out] tickOut The current system frequency will be stored here
 */
KERNELAPI void KERNELABI
SystemTimerGetClockFrequency(
        _In_ LargeUInteger_t* frequencyOut);

/**
 * @brief Retrieves the frequency of the high performance counter (if present)
 *
 * @param[Out] frequency A pointer where to store the frequency.
 * @return     Returns OsNotSupported if HPC is not supported, otherwise OsSuccess.
 */
KERNELAPI OsStatus_t KERNELABI
SystemTimerGetPerformanceFrequency(
        _Out_ LargeUInteger_t* frequency);

/**
 * @brief Retrieves the current tick of the high performance counter (if present)
 *
 * @param[Out] tick A pointer where to store the current tick.
 * @return     Returns OsNotSupported if HPC is not supported, otherwise OsSuccess.
 */
KERNELAPI OsStatus_t KERNELABI
SystemTimerGetPerformanceTick(
        _Out_ LargeUInteger_t* tick);

/**
 * @brief Stalls the CPU for the a specified amount of time in nanoseconds resolution.
 *
 * @param[In] ns The minimum number of nanoseconds to stall for.
 */
KERNELAPI void KERNELABI
SystemTimerStall(
        _In_ tick_t ns);

#endif // !__COMPONENT_MEMORY__
