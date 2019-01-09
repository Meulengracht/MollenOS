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
 * MollenOS MCore - Threading Support Definitions & Structures
 * - This header describes the base threading-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>
#include <threads.h>
#include <stddef.h>
#include <stdlib.h>
#include <signal.h>
#include "tls.h"

CRTDECL(void, __cxa_threadinitialize(void));

typedef struct _ThreadPackage {
    thrd_start_t    Entry;
    void*           Data;
} ThreadPackage_t;

/* thrd_initialize
 * All new threads inherit this start function */
void
thrd_initialize(
    _In_ void *Data)
{
    thread_storage_t    Tls;
    ThreadPackage_t*    Tp;
    int                 ExitCode;

    tls_create(&Tls);
    __cxa_threadinitialize();
    
    Tp          = (ThreadPackage_t*)Data;
    ExitCode    = Tp->Entry(Tp->Data);

    free(Tp);
    thrd_exit(ExitCode);
}

/* call_once
 * Calls function func exactly once, even if invoked from several threads. 
 * The completion of the function func synchronizes with all previous or subsequent 
 * calls to call_once with the same flag variable. */
void
call_once(
    _In_ once_flag* flag, 
    _In_ void (*func)(void))
{
    // Use interlocked exchange for this operation
    int RunOnce = atomic_exchange(flag, 0);
    if (RunOnce != 0) {
        func();
    }
}

/* thrd_create
 * Creates a new thread executing the function func. The function is invoked as func(arg).
 * If successful, the object pointed to by thr is set to the identifier of the new thread.
 * The completion of this function synchronizes-with the beginning of the thread. */
int
thrd_create(
    _Out_ thrd_t*       thr,
    _In_  thrd_start_t  func,
    _In_  void*         arg)
{
    ThreadPackage_t *Tp = NULL;
    thrd_t Result       = UUID_INVALID;

    // Allocate a new startup-package
    Tp = (ThreadPackage_t*)malloc(sizeof(ThreadPackage_t));
    if (Tp == NULL) {
        return thrd_nomem;
    }
    
    Tp->Entry = func;
    Tp->Data  = arg;

    // Redirect to operating system to handle rest
    Result = (thrd_t)Syscall_ThreadCreate((thrd_start_t)thrd_initialize, Tp, 0, UUID_INVALID);
    if (Result == UUID_INVALID) {
        free(Tp);
        return thrd_error;
    }
    *thr = Result;
    return thrd_success;
}

/* thrd_equal
 * Checks whether lhs and rhs refer to the same thread. */
int
thrd_equal(
    _In_ thrd_t lhs,
    _In_ thrd_t rhs)
{
    if (lhs == rhs) {
        return 1;
    }
    return 0;
}

/* thrd_current
 * Returns the identifier of the calling thread. */
thrd_t
thrd_current(void)
{
    // If it's already cached, use that
    if (tls_current()->thr_id != UUID_INVALID) {
        return tls_current()->thr_id;
    }

    // Otherwise invoke OS to refresh id
    tls_current()->thr_id = (thrd_t)Syscall_ThreadId();
    return tls_current()->thr_id;
}

/* thrd_sleep
 * Blocks the execution of the current thread for at least until the TIME_UTC 
 * based time point pointed to by time_point has been reached.
 * The sleep may resume earlier if a signal that is not ignored is received. 
 * In such case, if remaining is not NULL, the remaining time duration is stored 
 * into the object pointed to by remaining. */
int
thrd_sleep(
    _In_ __CONST struct timespec* time_point,
    _In_Opt_ struct timespec* remaining)
{
    // Add up to msec granularity, we don't support sub-ms
    time_t msec         = time_point->tv_sec * MSEC_PER_SEC;
    time_t msec_slept   = 0;
    if (time_point->tv_nsec != 0) {
        msec += ((time_point->tv_nsec - 1) / NSEC_PER_MSEC) + 1;
    }

    // Sanitize just in case - to save a syscall
    if (time_point->tv_sec == 0 && time_point->tv_nsec == 0) {
        return 0;
    }

    // Redirect the call
    Syscall_ThreadSleep(msec, &msec_slept);

    // Update out if any
    if (remaining != NULL && msec > msec_slept) {
        msec -= msec_slept;
        remaining->tv_nsec = (msec % MSEC_PER_SEC) * NSEC_PER_MSEC;
        remaining->tv_sec = msec / MSEC_PER_SEC;
    }
    return 0;
}

/* thrd_sleep
 * Blocks the execution of the current thread for at least given milliseconds */
int
thrd_sleepex(
    _In_ size_t msec)
{
    time_t msec_slept = 0;
    Syscall_ThreadSleep(msec, &msec_slept);
    return 0;
}

/* thrd_yield
 * Provides a hint to the implementation to reschedule the execution of threads, 
 * allowing other threads to run. */
void
thrd_yield(void) {
    (void)Syscall_ThreadYield();
}

/* thrd_exit
 * First, for every thread-specific storage key which was created with a non-null 
 * destructor and for which the associated value is non-null (see tss_create), thrd_exit 
 * sets the value associated with the key to NULL and then invokes the destructor with 
 * the previous value of the key. The order in which the destructors are invoked is unspecified.
 * If, after this, there remain keys with both non-null destructors and values 
 * (e.g. if a destructor executed tss_set), the process is repeated up to TSS_DTOR_ITERATIONS times.
 * Finally, the thrd_exit function terminates execution of the calling thread and sets its result code to res.
 * If the last thread in the program is terminated with thrd_exit, the entire program 
 * terminates as if by calling exit with EXIT_SUCCESS as the argument (so the functions 
 * registered by atexit are executed in the context of that last thread) */
_Noreturn void 
thrd_exit(
    _In_ int res)
{
    tls_cleanup(thrd_current(), NULL, res);
    tls_destroy(tls_current());
    Syscall_ThreadExit(res);
    for(;;);
}

/* thrd_join
 * Blocks the current thread until the thread identified by thr finishes execution.
 * If res is not a null pointer, the result code of the thread is put to the location pointed to by res.
 * The termination of the thread synchronizes-with the completion of this function.
 * The behavior is undefined if the thread was previously detached or joined by another thread. */
int
thrd_join(
    _In_ thrd_t thr,
    _Out_ int *res)
{
    if (Syscall_ThreadJoin(thr, res) == OsSuccess) {
        return thrd_success;
    }
    return thrd_error;
}

/* thrd_detach
 * Detaches the thread identified by thr from the current environment. 
 * The resources held by the thread will be freed automatically once the thread exits. */
int
thrd_detach(
    _In_ thrd_t thr)
{
    // The syscall actually does most of the work
    if (Syscall_ThreadDetach(thr) == OsSuccess) {
        return thrd_success;
    }
    return thrd_error;
}

/* thrd_signal
 * Invokes a signal on the given thread id, for security reasons
 * it's only possible to signal threads local to the running process. */
int
thrd_signal(
    _In_ thrd_t thr,
    _In_ int    sig)
{
    return Syscall_ThreadSignal(thr, sig);
}
