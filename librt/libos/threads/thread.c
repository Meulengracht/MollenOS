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
#include <internal/_syscalls.h>
#include <internal/_tls.h>
#include <os/threads.h>
#include <os/time.h>
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

    // Initialize the TLS system for the new kernel thread, remember to switch
    // to it immediately.
    __tls_initialize(&threadStorage);
    __tls_switch(&threadStorage);

    // Run any C/C++ initialization for the thread. Before this call
    // the tls must be set correctly. The TLS is set before the jump to this
    // entry function
    __cxa_threadinitialize();

    // Retrieve the startup context that should have been passed to us.
    startupContext = (struct ThreadStartupContext*)context;
    assert(startupContext != NULL);

    // Call the thread entry function
    exitCode = startupContext->Entry(startupContext->Data);

    // Cleanup the startup context, after getting passed to us, we now
    // own that piece of memory as the creator of this kernel thread shouldn't
    // need to keep track of this.
    free(startupContext);

    // Call the correct thread-exit function here. The thread exit function
    // will do the proper cleanup in order.
    ThreadsExit(exitCode);
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
        return OS_EOOM;
    }
    *threadId = UUID_INVALID;

    startupContext->Entry = function;
    startupContext->Data  = argument;

    oserr = Syscall_ThreadCreate(
            __ThreadStartup,
            startupContext,
            parameters,
            threadId
    );
    if (oserr != OS_EOK) {
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
    oserr_t       oserr;
    OSTimestamp_t tsRemaining;

    if (until == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = OSSleep(&(OSTimestamp_t) {
        .Seconds = until->tv_sec,
        .Nanoseconds = until->tv_nsec
    }, remaining == NULL ? NULL : &tsRemaining);
    if (oserr == OS_EINTERRUPTED && remaining != NULL) {
        remaining->tv_sec  = tsRemaining.Seconds;
        remaining->tv_nsec = tsRemaining.Nanoseconds;
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
        return OS_EINVALPARAMS;
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
