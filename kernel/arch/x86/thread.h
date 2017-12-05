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
 * MollenOS X86 Threading Interface
 * - Implements the neccessary threading support to get
 *   threading working for mcore on x86
 */

#ifndef _X86_THREAD_H_
#define _X86_THREAD_H_

/* Includes */
#include <arch.h>
#include <os/osdefs.h>

/* Definitions */
#define X86_THREAD_USEDFPU			0x1
#define X86_THREAD_FPU_INITIALISED	0x2

/* Constants and magic values which set the correct
 * bits for x86-specific registers, especially eflags */
#define X86_THREAD_EFLAGS			0x202
#define X86_THREAD_SINGLESTEP		0x100

/* _ThreadingSwitch
 * This function loads a new task from the scheduler, it
 * implements the task-switching functionality, which MCore leaves
 * up to the underlying architecture */
KERNELAPI
Context_t*
KERNELABI
_ThreadingSwitch(
    _In_ Context_t  *Context,
    _In_ int         PreEmptive,
    _Out_ size_t    *TimeSlice,
    _Out_ int       *TaskQueue);

#endif // !_MCORE_THREAD_H_
