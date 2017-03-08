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

/* IThreadCreate
 * Initializes a new x86-specific thread context
 * for the given threading flags, also initializes
 * the yield interrupt handler first time its called */
__EXTERN void *IThreadCreate(Flags_t ThreadFlags, Addr_t EntryPoint);

/* This function switches the current runtime-context
 * out with the given thread context, this should only
 * be used as a temporary way of impersonating another
 * thread */
__EXTERN void IThreadImpersonate(MCoreThread_t *Thread);

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
__EXTERN void IThreadWakeCpu(UUId_t Cpu);

/* IThreadYield
 * Yields the current thread control to the scheduler */
__EXTERN void IThreadYield(void);

#endif //!_MCORE_SYSTHREADS_H_
