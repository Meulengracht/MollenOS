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
 * MollenOS Threading Interface
 * - Contains the shared kernel threading interface
 *   that all sub-layers / architectures must conform to
 */

#ifndef _MCORE_SYSTHREADS_H_
#define _MCORE_SYSTHREADS_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <os/spinlock.h>

/* Includes
 * - System */
#include <arch.h>

typedef struct _MCoreThread MCoreThread_t;

/* ThreadingCreateArch
 * Initializes a new arch-specific thread context
 * for the given threading flags, also initializes
 * the yield interrupt handler first time its called */
KERNELAPI
void*
KERNELABI
ThreadingCreateArch(
    _In_ Flags_t ThreadFlags,
    _In_ uintptr_t EntryPoint);

/* ThreadingImpersonate
 * This function switches the current runtime-context
 * out with the given thread context, this should only
 * be used as a temporary way of impersonating another thread */
KERNELAPI
void
KERNELABI
ThreadingImpersonate(
    _In_ MCoreThread_t *Thread);

/* IThreadSetupUserMode
 * Initializes user-mode data for the given thread, and
 * allocates all neccessary resources (x86 specific) for
 * usermode operations */
__EXTERN void IThreadSetupUserMode(MCoreThread_t *Thread, uintptr_t StackAddress);

/* IThreadDestroy
 * Free's all the allocated resources for x86
 * specific threading operations */
__EXTERN void IThreadDestroy(MCoreThread_t *Thread);

/* ThreadingWakeCpu
 * Wake's the target cpu from an idle thread
 * by sending it an yield IPI */
KERNELAPI
void
KERNELABI
ThreadingWakeCpu(
    _In_ UUId_t Cpu);

/* ThreadingYield
 * Yields the current thread control to the scheduler */
KERNELAPI
void
KERNELABI
ThreadingYield(void);

#endif //!_MCORE_SYSTHREADS_H_
