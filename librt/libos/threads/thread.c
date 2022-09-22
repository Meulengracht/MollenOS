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

#include <assert.h>
#include <errno.h>
#include <internal/_syscalls.h>
#include <internal/_tls.h>
#include <os/mollenos.h>
#include <os/threads.h>
#include <stdlib.h>
#include "../../libc/threads/tss.h"

CRTDECL(void, __cxa_threadinitialize(void));
CRTDECL(void, __cxa_threadfinalize(void));

struct ThreadStartupContext {
    ThreadEntry_t Entry;
    void*         Data;
};

void
__ThreadStartup(
    _In_ void* context)
{
    thread_storage_t             threadStorage;
    struct ThreadStartupContext* startupContext;
    int                          exitCode;

    __tls_initialize(&threadStorage);
    __cxa_threadinitialize();

    startupContext = (struct ThreadStartupContext*)context;
    exitCode = startupContext->Entry(startupContext->Data);

    free(startupContext);
    thrd_exit(exitCode);
}

oserr_t
ThreadsCreate(
        _Out_ uuid_t*             threadId,
        _In_  ThreadParameters_t* parameters,
        _In_  ThreadEntry_t       function,
        _In_  void*               argument)
{
    struct ThreadStartupContext* startupContext;
    oserr_t                      oserr;
    assert(threadId != NULL);

    // Allocate a new startup-package
    startupContext = malloc(sizeof(struct ThreadStartupContext));
    if (startupContext == NULL) {
        return OsOutOfMemory;
    }
    *threadId = UUID_INVALID;

    startupContext->Entry = function;
    startupContext->Data  = argument;

    oserr = Syscall_ThreadCreate(
            (ThreadEntry_t)__ThreadStartup,
            startupContext,
            parameters,
            (uuid_t*)threadId
    );
    if (oserr != OsOK) {
        free(startupContext);
    }
    return oserr;
}

uuid_t
ThreadsCurrentId(void)
{
    // If it's already cached, use that
    if (__tls_current()->thread_id != UUID_INVALID) {
        return __tls_current()->thread_id;
    }

    // Otherwise, invoke OS to refresh id
    __tls_current()->thread_id = Syscall_ThreadId();
    return __tls_current()->thread_id;
}

oserr_t
ThreadsSleep(
        _In_      const struct timespec* until,
        _Out_Opt_ struct timespec*       remaining)
{
    UInteger64_t    ns;
    UInteger64_t    nsRemaining = {0 };
    struct timespec current;
    oserr_t         oserr;

    if (until == NULL) {
        return OsInvalidParameters;
    }

    // the duration value is actually a timepoint specified in UTC. So we actually need to
    // convert this to a relative value here.
    timespec_get(&current, TIME_UTC);

    // make sure that we haven't already stepped over the timeline
    if (current.tv_sec > until->tv_sec || (current.tv_sec == until->tv_sec && current.tv_nsec >= until->tv_nsec)) {
        return thrd_success;
    }

    // calculate duration
    ns.QuadPart = (until->tv_sec * NSEC_PER_SEC) + until->tv_nsec;
    ns.QuadPart -= (current.tv_sec * NSEC_PER_SEC) + current.tv_nsec;

    oserr = Syscall_Sleep(&ns, &nsRemaining);
    if (oserr == OsInterrupted) {
        if (remaining) {
            remaining->tv_sec  = (time_t)(nsRemaining.QuadPart / NSEC_PER_SEC);
            remaining->tv_nsec = (long)(nsRemaining.QuadPart % NSEC_PER_SEC);
        }
    }
    return oserr;
}

void
ThreadsYield(void)
{
    (void)Syscall_ThreadYield();
}

_Noreturn void
ThreadsExit(
        _In_ int exitCode)
{
    tss_cleanup(ThreadsCurrentId());
    __tls_destroy(__tls_current());
    __cxa_threadfinalize();
    Syscall_ThreadExit(exitCode);
    for(;;);
}

oserr_t
ThreadsJoin(
        _In_  uuid_t threadId,
        _Out_ int*   exitCode)
{
    if (exitCode == NULL) {
        return OsInvalidParameters;
    }
    return Syscall_ThreadJoin(threadId, exitCode);
}

oserr_t
ThreadsDetach(
        _In_ uuid_t threadId)
{
    return Syscall_ThreadDetach(threadId);
}

oserr_t
ThreadsSignal(
        _In_ uuid_t threadId,
        _In_ int    signal)
{
    return Syscall_ThreadSignal(threadId, signal);
}

void
ThreadParametersInitialize(
        _In_ ThreadParameters_t* parameters)
{
    parameters->Name              = NULL;
    parameters->Configuration     = 0;
    parameters->MemorySpaceHandle = UUID_INVALID;
    parameters->MaximumStackSize  = __MASK;
}

oserr_t
ThreadsSetName(
        _In_ const char* name)
{
    return Syscall_ThreadSetCurrentName(name);
}

oserr_t
ThreadsGetName(
        _In_ char*  buffer,
        _In_ size_t maxLength)
{
    return Syscall_ThreadGetCurrentName(buffer, maxLength);
}
