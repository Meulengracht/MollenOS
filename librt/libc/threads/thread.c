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
 * Threading Support Definitions & Structures
 * - This header describes the base threading-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <assert.h>
#include <errno.h>
#include <internal/_syscalls.h>
#include <os/mollenos.h>
#include "tls.h"
#include <stdlib.h>
#include <threads.h>

CRTDECL(void, __cxa_threadinitialize(void));
CRTDECL(void, __cxa_threadfinalize(void));

typedef struct ThreadPackage {
    thrd_start_t Entry;
    void*        Data;
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

void
call_once(
    _In_ once_flag* flag, 
    _In_ void (*func)(void))
{
    assert(flag != NULL);
    
    mtx_lock(&flag->syncobject);
    if (!flag->value) {
        flag->value = 1;
        func();
    }
    mtx_unlock(&flag->syncobject);
}

int
thrd_create(
    _Out_ thrd_t*      thr,
    _In_  thrd_start_t func,
    _In_  void*        arg)
{
    ThreadParameters_t Paramaters;
    ThreadPackage_t*   Package;
    OsStatus_t         Status;
    assert(thr != NULL);

    // Allocate a new startup-package
    Package = (ThreadPackage_t*)malloc(sizeof(ThreadPackage_t));
    if (Package == NULL) {
        _set_errno(ENOMEM);
        return thrd_nomem;
    }
    *thr = UUID_INVALID;
    
    Package->Entry = func;
    Package->Data  = arg;
    InitializeThreadParameters(&Paramaters);

    Status = Syscall_ThreadCreate((thrd_start_t)thrd_initialize, Package, &Paramaters, (UUId_t*)thr);
    if (Status != OsSuccess) {
        OsStatusToErrno(Status);
        free(Package);
        return thrd_error;
    }
    return thrd_success;
}

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

    // Otherwise, invoke OS to refresh id
    tls_current()->thr_id = (thrd_t)Syscall_ThreadId();
    return tls_current()->thr_id;
}

int
thrd_sleep(
    _In_     const struct timespec* time_point,
    _In_Opt_ struct timespec*       remaining)
{
    LargeUInteger_t nanoseconds;
    LargeUInteger_t nanosecondsPassed = { 0 };

    // Sanitize just in case - to save a syscall
    if (time_point->tv_sec == 0 && time_point->tv_nsec == 0) {
        return thrd_error;
    }

    // Add up to msec granularity, we don't support sub-ms
    nanoseconds.QuadPart = time_point->tv_sec * NSEC_PER_SEC;
    nanoseconds.QuadPart += time_point->tv_nsec;

    // Redirect the call
    Syscall_ThreadSleep(&nanoseconds, &nanosecondsPassed);

    // Update out if any
    if (remaining != NULL && nanoseconds.QuadPart > nanosecondsPassed.QuadPart) {
        nanoseconds.QuadPart -= nanosecondsPassed.QuadPart;
        remaining->tv_nsec = (time_t)(nanoseconds.QuadPart % NSEC_PER_SEC);
        remaining->tv_sec = (time_t)(nanoseconds.QuadPart / NSEC_PER_SEC);
    }
    return 0;
}

int
thrd_sleepex(
    _In_ size_t msec)
{
    LargeUInteger_t nanoseconds;
    LargeUInteger_t nanosecondsPassed;

    nanoseconds.QuadPart = msec * NSEC_PER_MSEC;
    Syscall_ThreadSleep(&nanoseconds, &nanosecondsPassed);
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
