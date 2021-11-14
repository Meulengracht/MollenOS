/**
 * Copyright 2021, Philip Meulengracht
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
 * User threads implementation. Implements support for multiple tasks running entirely
 * in userspace. This is supported by additional synchronization primitives in the usched_
 * namespace.
 *
 * TODO:
 *  - Multiple execution unit support
 *  - Task configuration support (size of stack etc)
 */

#ifndef __OS_USCHED_H__
#define __OS_USCHED_H__

#include <os/spinlock.h>

typedef void (*usched_task_fn)(void*, void*);

/**
 * @brief Initializes the scheduler for single unit scheduling. Currently usched only supports
 * a single execution unit.
 */
CRTDECL(void,usched_init(void));

/**
 * @brief Yields control of the current task and executes the next task in line. If no tasks
 * are ready to execute, control is returned to original caller of this function.
 * @return Returns the number of milliseconds untill the next timed event should occur.
 */
CRTDECL(int,usched_yield(void));

/**
 * @brief Schedules a new task in the scheduler for current execution unit.
 *
 * @param entry    The function to execute with the usched_task_fn signature.
 * @param argument The argument that should be passed to the function.
 * @return         A cancellation token value that can be used to signal cancellation to the task.
 *                 The cancellation token is passed to the task entry as the second parameter.
 */
CRTDECL(void*, usched_task_queue(usched_task_fn entry, void* argument));

/**
 * @brief Signals to the task's cancellation token that the a cancel operation has been requested.
 *
 * @param cancellationToken The cancellation token that should be signalled.
 */
CRTDECL(void, usched_task_cancel(void* cancellationToken));

/**
 * @brief For tasks to implement check of whether or not a cancellation token has been signalled.
 *
 * @param cancellationToken The token to check.
 * @return                  Returns 1 if the token is signaled. Returns 0 if not.
 */
CRTDECL(int, usched_ct_is_cancelled(void* cancellationToken));

#endif //!__OS_USCHED_H__
