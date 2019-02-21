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
 * Threading Support Definitions & Structures
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
CRTDECL(void, __cxa_threadfinalize(void));

typedef struct _ThreadPackage {
    thrd_start_t    Entry;
    void*           Data;
} ThreadPackage_t;

void
thrd_initialize(
    _In_ void *Data)
{
    thread_storage_t    Tls;
    ThreadPackage_t*    Tp;
    int                 ExitCode;

    tls_create(&Tls);
    __cxa_threadinitialize();
    
    Tp       = (ThreadPackage_t*)Data;
    ExitCode = Tp->Entry(Tp->Data);

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

int
thrd_sleep(
    _In_     const struct timespec* time_point,
    _In_Opt_ struct timespec*       remaining)
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

int
thrd_sleepex(
    _In_ size_t msec)
{
    time_t msec_slept = 0;
    Syscall_ThreadSleep(msec, &msec_slept);
    return 0;
}

void
thrd_yield(void)
{
    (void)Syscall_ThreadYield();
}

_Noreturn void 
thrd_exit(
    _In_ int res)
{
    tls_cleanup(thrd_current(), NULL, res);
    tls_destroy(tls_current());
    __cxa_threadfinalize();
    Syscall_ThreadExit(res);
    for(;;);
}

int
thrd_join(
    _In_  thrd_t thr,
    _Out_ int*   res)
{
    if (Syscall_ThreadJoin(thr, res) == OsSuccess) {
        return thrd_success;
    }
    return thrd_error;
}

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

int
thrd_signal(
    _In_ thrd_t thr,
    _In_ int    sig)
{
    return Syscall_ThreadSignal(thr, sig);
}
