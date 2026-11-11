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
 */

#ifndef __OS_XUNIT_H__
#define __OS_XUNIT_H__

#include <os/usched/types.h>

/**
 * @brief Initializes the current kernel thread as the primary execution unit.
 * After this call, use usched_xunit_main_loop or a custom scheduling loop.
 */
CRTDECL(void, usched_xunit_init(void));

/**
 * @brief Runs the primary execution unit's scheduling loop until program exit.
 * This function does not return.
 *
 * @param startFn Initial job that should be scheduled for this execution unit.
 * @param argument Argument for the initial job.
 */
CRTDECL(_Noreturn void, usched_xunit_main_loop(usched_task_fn startFn, void* argument));

/**
 * @brief Retrieves the requested number of pooled execution units.
 * Units finishing a stop request are excluded from this count.
 *
 * @return Number of execution units, or -1 if any error occurred. Check errno for details.
 */
CRTDECL(int, usched_xunit_count(void));

/**
 * @brief Sets the number of pooled execution units for the process. The count
 * should not exceed the number of logical CPUs on the system.
 * Reducing the count asks the selected units to stop, and returns without waiting
 * for their kernel threads to exit. A unit stops at its next scheduling point,
 * so jobs must yield or block before it can stop. Waiting jobs stay on the
 * shared scheduler and can resume on another unit. Until the units have stopped,
 * the number of running kernel threads may be higher than count.
 *
 * @param[In] count The requested number of pooled execution units.
 *                  Set to -1 to use the maximum number.
 * @return The result of the operation. If this returns != 0, check errno for details.
 */
CRTDECL(int, usched_xunit_set_count(int count));

#endif //!__OS_XUNIT_H__
