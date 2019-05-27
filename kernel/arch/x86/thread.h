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

#include <arch.h>
#include <os/osdefs.h>
#include <ddk/interrupt.h>

#define X86_THREAD_USEDFPU          0x1

/* Constants and magic values which set the correct
 * bits for x86-specific registers, especially eflags */
#define X86_THREAD_EFLAGS           0x202
#define X86_THREAD_SINGLESTEP       0x100

/* X86SwitchThread
 * Takes the current context, and the circumstances of why the switch
 * happens and performs a task switch to the next runnable thread. If none
 * are ready, it switches to Idle thread */
KERNELAPI void KERNELABI
X86SwitchThread(
    _In_ int    Preemptive,
    _In_ size_t MillisecondsPassed);

#endif // !_MCORE_THREAD_H_
