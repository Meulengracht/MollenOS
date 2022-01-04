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

#include <os/osdefs.h>
#include <ds/list.h>

typedef uint64_t tick_t;

enum SystemTimeAttributes {
    SystemTimeAttributes_COUNTER    = 0x1,
    SystemTimeAttributes_IRQ        = 0x2,
    SystemTimeAttributes_CALIBRATED = 0x4
};

typedef struct SystemTimerOperations {
    void (*Read)(void*, tick_t*);
    void (*GetFrequency)(void*, tick_t*);
    void (*Recalibrate)(void*);
} SystemTimerOperations_t;

typedef struct SystemTimer {
    element_t                 ListHeader;
    SystemTimerOperations_t   Operations;
    enum SystemTimeAttributes Attributes;
    UUId_t                    Interrupt;
    void*                     Context;
} SystemTimer_t;

/**
 * @brief Registers a new system timer with the Machine.
 *
 * @param[In] operations A pointer to various operations the timer can support
 * @param[In] attributes The attributes/features of the timer
 * @param[In] interrupt  If the timer is occupying an interrupt, this is its interrupt handle
 * @return
 */
KERNELAPI OsStatus_t KERNELABI
SystemTimerRegister(
        _In_ SystemTimerOperations_t*  operations,
        _In_ enum SystemTimeAttributes attributes,
        _In_ UUId_t                    interrupt,
        _In_ void*                     context);

/**
 * @brief Retrieves the system tick counter
 *
 * @param[Out] tickOut The current system tick will be stored here
 */
KERNELAPI void KERNELABI
SystemTimerGetTick(
        _In_ tick_t* tickOut);

/**
 * @brief Stalls the CPU for the a specified amount of time in nanoseconds resolution.
 *
 * @param[In] ns The minimum number of nanoseconds to stall for.
 */
KERNELAPI void KERNELABI
SystemTimerStall(
        _In_ tick_t ns);

#endif // !__COMPONENT_MEMORY__
