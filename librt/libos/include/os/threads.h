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
 */

#ifndef __OS_THREADS_H__
#define __OS_THREADS_H__

// imported from time.h
struct timespec;

#include <os/osdefs.h>
#include <os/types/thread.h>

typedef int (*ThreadEntry_t)(void*);

_CODE_BEGIN
/**
 * @brief Creates a new thread executing the function func. The function is invoked as func(arg).
 * If successful, the object pointed to by thr is set to the identifier of the new thread.
 * The completion of this function synchronizes-with the beginning of the thread
 * @param threadId
 * @param parameters
 * @param function
 * @param argument
 * @return
 */
CRTDECL(oserr_t,
ThreadsCreate(
        _Out_ uuid_t*             threadId,
        _In_  ThreadParameters_t* parameters,
        _In_  ThreadEntry_t       function,
        _In_  void*               argument));

/**
 *
 * @param Paramaters
 */
CRTDECL(void,
ThreadParametersInitialize(
        _In_ ThreadParameters_t* parameters));

/**
 * @brief First, for every thread-specific storage key which was created with a non-null
 * destructor and for which the associated value is non-null (see tss_create), ThreadsExit
 * sets the value associated with the key to NULL and then invokes the destructor with
 * the previous value of the key. The order in which the destructors are invoked is unspecified.
 * If, after this, there remain keys with both non-null destructors and values
 * (e.g. if a destructor executed tss_set), the process is repeated up to TSS_DTOR_ITERATIONS times.
 * Finally, the ThreadsExit function terminates execution of the calling thread and sets its result code to res.
 * If the last thread in the program is terminated with thrd_exit, the entire program
 * terminates as if by calling exit with EXIT_SUCCESS as the argument (so the functions
 * registered by atexit are executed in the context of that last thread)
 * @param exitCode
 */
CRTDECL(void,
ThreadsExit(
        _In_ int exitCode));

/**
 * @brief Returns the identifier of the calling thread.
 * @return
 */
CRTDECL(uuid_t,
ThreadsCurrentId(void));

/**
 * @brief Blocks the execution of the current thread for at least until the TIME_UTC
 * based time point pointed to by until has been reached.
 *
 * The sleep may resume earlier if a signal that is not ignored is received.
 * In such case, if remaining is not NULL, the remaining time duration is stored
 * into the object pointed to by remaining.
 *
 * @param[In]            until     Pointer to the point in time to sleep until
 * @param[Out, Optional] remaining Pointer to the object to put the remaining time on interruption. May be NULL, in which case it is ignored
 * @return OsOK on successful sleep, OsInterrupted if a signal occurred, other if an error occurred.
 */
CRTDECL(oserr_t,
ThreadsSleep(
        _In_      const struct timespec* until,
        _Out_Opt_ struct timespec*       remaining));

/**
 * @brief rovides a hint to the to reschedule the execution of the current thread,
 * allowing other threads to run.
 */
CRTDECL(void,
ThreadsYield(void));

/**
 * @brief Detaches the thread identified by thr from the current environment.
 * The resources held by the thread will be freed automatically once the thread exits.
 * @param threadId
 * @return
 */
CRTDECL(oserr_t,
ThreadsDetach(
        _In_ uuid_t threadId));

/**
 * @brief Blocks the current thread until the thread identified by thr finishes execution.
 * If res is not a null pointer, the result code of the thread is put to the location pointed to by res.
 * The termination of the thread synchronizes-with the completion of this function.
 * The behavior is undefined if the thread was previously detached or joined by another thread.
 * @param threadId
 * @param exitCode
 * @return
 */
CRTDECL(oserr_t,
ThreadsJoin(
        _In_  uuid_t threadId,
        _Out_ int*   exitCode));

/**
 * @brief nvokes a signal on the given thread id, for security reasons
 * it's only possible to signal threads local to the running process.
 * @param threadId
 * @param signal
 * @return
 */
CRTDECL(oserr_t,
ThreadsSignal(
        _In_ uuid_t threadId,
        _In_ int    signal));

/**
 * @brief
 * @param name
 * @return
 */
CRTDECL(oserr_t,
ThreadsSetName(
        _In_ const char* name));

/**
 * @brief
 * @param buffer
 * @param maxLength
 * @return
 */
CRTDECL(oserr_t,
ThreadsGetName(
        _In_ char*  buffer,
        _In_ size_t maxLength));

_CODE_END
#endif //!__OS_THREADS_H__
