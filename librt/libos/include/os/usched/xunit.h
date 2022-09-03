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
 * @brief Initializes the xunit system, and initializes the current kernel thread
 * as the initial execution unit for the userspace threads. After this call it's possible
 * to either use a custom loop for the primary execution unit, or use the pre-provided
 * usched_xunit_main_loop
 */
CRTDECL(void, usched_xunit_init(void));

/**
 * @brief Runs the execution units primary loop. There is no return from this call, as
 * it will run until program exit
 *
 * @param[In] startFn  Initial job that should be scheduled for this execution unit
 * @param[In] argument Argument for the initial job
 */
CRTDECL(_Noreturn void, usched_xunit_main_loop(usched_task_fn startFn, void* argument));

/**
 * @brief Retrieves the current number of execution units.
 *
 * @return Number of execution units, or -1 if any error occurred. Check errno for details.
 */
CRTDECL(int, usched_xunit_count(void));

/**
 * @brief Sets the number of execution units available for the process. This should be
 * not be larger then number of logical CPUs on the system.
 *
 * @param[In] count The number of execution units.  To request the maximum number
 *                  of execution units, set this to -1.
 * @return The result of the operation. If this returns != 0, check errno for details.
 */
CRTDECL(int, usched_xunit_set_count(int count));

#endif //!__OS_XUNIT_H__
