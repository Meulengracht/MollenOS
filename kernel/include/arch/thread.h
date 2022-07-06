/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Threading Support Interface
 * - Contains the shared kernel threading interface
 *   that all sub-layers / architectures must conform to
 */

#ifndef __ARCH_THREAD_H__
#define __ARCH_THREAD_H__

#include <os/osdefs.h>
#include <os/context.h>

DECL_STRUCT(Thread);
DECL_STRUCT(SystemCpuCore);

/**
 * @brief Called whenever a new thread is allocated for the platform to initialize.
 *
 * @param thread
 * @return
 */
KERNELAPI oscode_t KERNELABI
ArchThreadInitialize(
    _In_ Thread_t* thread);

/**
 * @brief Destroys any resources allocated that the platform needed for the thread.
 *
 * @param thread
 * @return
 */
KERNELAPI oscode_t KERNELABI
ArchThreadDestroy(
    _In_ Thread_t* thread);

/**
 * @brief Yields the current thread control to the scheduler
 */
KERNELAPI void KERNELABI
ArchThreadYield(void);

/**
 * @brief Saves the current state for the thread passed as parameter.
 *
 * @param thread
 */
KERNELAPI void KERNELABI
ArchThreadLeave(
    _In_ Thread_t* thread);

/**
 * @brief Restores the thread state to allow the thread to run next.
 *
 * @param[In] cpuCore
 * @param[In] thread
 */
KERNELAPI void KERNELABI
ArchThreadEnter(
        _In_ SystemCpuCore_t* cpuCore,
        _In_ Thread_t*        thread);

/**
 * @brief Creates a new context for a thread, a type and the flags for which
 * the thread is been created under is passed.
 *
 * @param contextType
 * @param contextSize
 * @return
 */
KERNELAPI Context_t* KERNELABI
ArchThreadContextCreate(
    _In_ int    contextType,
    _In_ size_t contextSize);

/**
 * @brief Destroys the context for the thread and releases resources.
 *
 * @param context
 * @param contextType
 * @param contextSize
 */
KERNELAPI void KERNELABI
ArchThreadContextDestroy(
    _In_ Context_t* context,
    _In_ int        contextType,
    _In_ size_t     contextSize);

/**
 * @brief Resets an already existing context to new with the given parameters.
 *
 * @param context
 * @param contextType
 * @param address
 * @param argument
 */
KERNELAPI void KERNELABI
ArchThreadContextReset(
    _In_ Context_t* context,
    _In_ int        contextType,
    _In_ uintptr_t  address,
    _In_ uintptr_t  argument);

/**
 * @brief Adds an interceptor function that gets executed upon return of
 * of thread. This will then be the next thing executed. Optionally a safe
 * stack can be provided that will be used for execution.
 *
 * @param Context
 * @param TemporaryStack
 * @param Address
 * @param Argument0
 * @param Argument1
 * @param Argument2
 */
KERNELAPI void KERNELABI
ArchThreadContextPushInterceptor(
    _In_ Context_t* Context,
    _In_ uintptr_t  TemporaryStack,
    _In_ uintptr_t  Address,
    _In_ uintptr_t  Argument0,
    _In_ uintptr_t  Argument1,
    _In_ uintptr_t  Argument2);

/**
 * @brief Dumps the contents of the given thread context for debugging.
 *
 * @param context
 * @return
 */
KERNELAPI oscode_t KERNELABI
ArchThreadContextDump(
        _In_ Context_t *context);

#endif //!__ARCH_THREAD_H__
