/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Threading Support Interface
 * - Contains the shared kernel threading interface
 *   that all sub-layers / architectures must conform to
 */

#ifndef __VALI_THREADING_SUPPORT_H__
#define __VALI_THREADING_SUPPORT_H__

#include <os/osdefs.h>
#include <os/context.h>

typedef struct _MCoreThread MCoreThread_t;

/* ThreadingRegister
 * Initializes a new arch-specific thread context
 * for the given threading flags, also initializes
 * the yield interrupt handler first time its called */
KERNELAPI OsStatus_t KERNELABI
ThreadingRegister(
    _In_ MCoreThread_t *Thread);

/* ThreadingUnregister
 * Unregisters the thread from the system and cleans up any 
 * resources allocated by ThreadingRegister */
KERNELAPI OsStatus_t KERNELABI
ThreadingUnregister(
    _In_ MCoreThread_t *Thread);

/* ThreadingYield
 * Yields the current thread control to the scheduler */
KERNELAPI void KERNELABI
ThreadingYield(void);

/* SaveThreadState
 * Saves the current state for the thread passed as parameter. */
void
SaveThreadState(
    _In_ MCoreThread_t* Thread);

/* RestoreThreadState
 * Restores the thread state to allow the thread to run next. */
void
RestoreThreadState(
    _In_ MCoreThread_t* Thread);

/* ContextCreate
 * Creates a new context for a thread, a type and the flags for which
 * the thread is been created under is passed. */
KERNELAPI Context_t* KERNELABI
ContextCreate(
    _In_ int ContextType);

/* ContextReset
 * Resets an already existing context to new with the given parameters. */
KERNELAPI void KERNELABI
ContextReset(
    _In_ Context_t* Context,
    _In_ int        ContextType,
    _In_ uintptr_t  Address,
    _In_ uintptr_t  Argument0,
    _In_ uintptr_t  Argument1,
    _In_ uintptr_t  Argument2); 

/* ContextPushInterceptor
 * Adds an interceptor function that gets executed upon return of 
 * of thread. This will then be the next thing executed. Optionally a safe
 * stack can be provided that will be used for execution. */
void
ContextPushInterceptor(
    _In_ Context_t* Context,
    _In_ uintptr_t  Address,
    _In_ uintptr_t  Argument0,
    _In_ uintptr_t  Argument1,
    _In_ uintptr_t  Argument2);

/* ContextDestroy
 * Destroys the context for the thread and releases resources. */
KERNELAPI void KERNELABI
ContextDestroy(
    _In_ Context_t* Context,
    _In_ int        ContextType);

#endif //!__VALI_THREADING_SUPPORT_H__
