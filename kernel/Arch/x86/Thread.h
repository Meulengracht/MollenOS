/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS X86-32 Threads
*/

#ifndef _X86_THREAD_H_
#define _X86_THREAD_H_

/* Includes */
#include <Arch.h>
#include <crtdefs.h>

/* Definitions */
#define X86_THREAD_USEDFPU			0x1
#define X86_THREAD_FPU_INITIALISED	0x2

#define X86_THREAD_EFLAGS			0x202

/* Prototypes */

/* Switches active thread */
_CRT_EXTERN Registers_t *_ThreadingSwitch(Registers_t *Regs, int PreEmptive,
										 uint32_t *TimeSlice, uint32_t *TaskQueue);

/* Context Manipulation */
_CRT_EXTERN Registers_t *ContextCreate(Addr_t Eip);
_CRT_EXTERN Registers_t *ContextUserCreate(Addr_t StackStartAddr, Addr_t Eip, Addr_t *Args);

#endif // !_MCORE_THREAD_H_
