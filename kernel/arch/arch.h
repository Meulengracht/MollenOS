/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS Architecture Header
*/

#ifndef _MCORE_MAIN_ARCH_
#define _MCORE_MAIN_ARCH_

/* Includes */
#include <os/osdefs.h>

/* Select Correct ARCH file */
#if defined(_X86_32)
#include "x86\x32\Arch.h"
#elif defined(_X86_64)
#include "x86\x64\Arch.h"
#else
#error "Unsupported Architecture :("
#endif

/************************
 * Threading            *
 * Used for abstracting *
 * arch specific thread *
 ************************/
typedef struct _MCoreThread MCoreThread_t;

/* IThreadCreate
 * Initializes a new x86-specific thread context
 * for the given threading flags, also initializes
 * the yield interrupt handler first time its called */
__EXTERN void *IThreadCreate(Flags_t ThreadFlags, Addr_t EntryPoint);

/* IThreadSetupUserMode
 * Initializes user-mode data for the given thread, and
 * allocates all neccessary resources (x86 specific) for
 * usermode operations */
__EXTERN void IThreadSetupUserMode(MCoreThread_t *Thread, Addr_t StackAddress);

/* IThreadDestroy
 * Free's all the allocated resources for x86
 * specific threading operations */
__EXTERN void IThreadDestroy(MCoreThread_t *Thread);

/* IThreadWakeCpu
 * Wake's the target cpu from an idle thread
 * by sending it an yield IPI */
__EXTERN void IThreadWakeCpu(Cpu_t Cpu);

/* IThreadYield
 * Yields the current thread control to the scheduler */
__EXTERN void IThreadYield(void);

/***********************
* Device Interface     *
***********************/
__EXTERN int DeviceAllocateInterrupt(void *mCoreDevice);

#endif