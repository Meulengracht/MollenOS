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
#include <internal/_tls.h>
#include <os/mollenos.h>
#include <os/threads.h>
#include "tss.h"
#include <stdlib.h>

CRTDECL(void, __cxa_threadinitialize(void));
CRTDECL(void, __cxa_threadfinalize(void));

typedef struct ThreadPackage {
    thrd_start_t Entry;
    void*        Data;
} ThreadPackage_t;

void
thrd_initialize(
    _In_ void* context)
{
    thread_storage_t tls;
    ThreadPackage_t* package;
    int              exitCode;

    __tls_initialize(&tls);
    __tls_switch(&tls);
    __cxa_threadinitialize();

    package       = (ThreadPackage_t*)context;
    exitCode = package->Entry(package->Data);

    free(package);
    thrd_exit(exitCode);
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
    oserr_t         Status;
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
    ThreadParametersInitialize(&Paramaters);

    Status = Syscall_ThreadCreate((thrd_start_t)thrd_initialize, Package, &Paramaters, (uuid_t*)thr);
    if (Status != OsOK) {
        OsErrToErrNo(Status);
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
    if (__tls_current()->thr_id != UUID_INVALID) {
        return __tls_current()->thr_id;
    }

    // Otherwise, invoke OS to refresh id
    __tls_current()->thr_id = (thrd_t)Syscall_ThreadId();
    return __tls_current()->thr_id;
}

int
thrd_sleep(
    _In_     const struct timespec* duration,
    _In_Opt_ struct timespec*       remaining)
{
    UInteger64_t ns;
    UInteger64_t nsRemaining = {0 };
    struct timespec current;
    oserr_t      osStatus;

    if (!duration || (duration->tv_sec == 0 && duration->tv_nsec == 0)) {
        return thrd_error;
    }

    // the duration value is actually a timepoint specified in UTC. So we actually need to
    // convert this to a relative value here.
    timespec_get(&current, TIME_UTC);

    // make sure that we haven't already stepped over the timeline
    if (current.tv_sec > duration->tv_sec || (current.tv_sec == duration->tv_sec && current.tv_nsec >= duration->tv_nsec)) {
        return thrd_success;
    }

    // calculate duration
    ns.QuadPart = (duration->tv_sec * NSEC_PER_SEC) + duration->tv_nsec;
    ns.QuadPart -= (current.tv_sec * NSEC_PER_SEC) + current.tv_nsec;

    osStatus = Syscall_Sleep(&ns, &nsRemaining);
    if (osStatus == OsInterrupted) {
        if (remaining) {
            remaining->tv_sec  = (time_t)(nsRemaining.QuadPart / NSEC_PER_SEC);
            remaining->tv_nsec = (long)(nsRemaining.QuadPart % NSEC_PER_SEC);
        }
        return thrd_error;
    }
    return thrd_success;
}

int
thrd_sleepex(
    _In_ size_t msec)
{
    UInteger64_t nanoseconds;
    UInteger64_t remaining;

    nanoseconds.QuadPart = msec * NSEC_PER_MSEC;
    VaSleep(&nanoseconds, &remaining);
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
    tss_cleanup(thrd_current());
    __cxa_threadfinalize();
    __tls_destroy(__tls_current());
    Syscall_ThreadExit(res);
    for(;;);
}

int
thrd_join(
    _In_  thrd_t thr,
    _Out_ int*   res)
{
    if (Syscall_ThreadJoin(thr, res) == OsOK) {
        return thrd_success;
    }
    return thrd_error;
}

int
thrd_detach(
    _In_ thrd_t thr)
{
    // The syscall actually does most of the work
    if (Syscall_ThreadDetach(thr) == OsOK) {
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
