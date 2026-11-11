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
 * Thread system calls. Thread IDs refer to the global thread table. Keeping a
 * record allocated and checking permission to use it are separate operations.
 * Join and detach check the process relationship in their thread helpers.
 * Signal checks it here before submitting work to the target core.
 */
#define __MODULE "SCIF"
//#define __TRACE

#include <arch/thread.h>
#include <arch/utils.h>
#include <assert.h>
#include <os/types/thread.h>
#include <threading.h>
#include <string.h>
#include <debug.h>
#include <handles/global.h>

/**
 * @brief Creates a thread using the caller's run mode and optional parameters.
 * Parameters may be NULL. With no explicit memory space, the thread inherits
 * from its creator. Stack sizes of zero let ThreadCreate select the defaults.
 * HandleOut must be writable. It receives an ID without an extra reference.
 *
 * @return OS_EINVALPARAMS for a NULL entry, otherwise the ThreadCreate result.
 */
oserr_t
ScThreadCreate(
        _In_  ThreadEntry_t       Entry,
        _In_  void*               Arguments,
        _In_  OSThreadParameters_t* Parameters,
        _Out_ uuid_t*             HandleOut)
{
    unsigned int threadCurrentMode = ThreadCurrentMode();
    uuid_t       memorySpaceHandle = UUID_INVALID;
    const char*  name              = NULL;
    if (Entry == NULL) {
        return OS_EINVALPARAMS;
    }
    
    // Optional parameters can add configuration flags or select a memory space.
    if (Parameters != NULL) {
        name               = Parameters->Name;
        threadCurrentMode |= Parameters->Configuration;
        memorySpaceHandle  = Parameters->MemorySpaceHandle;
    }
    
    // If a memory space is not provided, then we inherit
    if (memorySpaceHandle == UUID_INVALID) {
        threadCurrentMode |= THREADING_INHERIT;
    }

    // Leave stack sizes at zero to use the kernel and user defaults.
    return ThreadCreate(name, Entry, Arguments, threadCurrentMode, memorySpaceHandle,
                        0, 0, HandleOut);
}

// The system call table uses an oserr_t signature, but ThreadExit never returns
// to complete this call. The scheduler finishes the thread after leaving its stack.
oserr_t
ScThreadExit(
    _In_ int ExitCode)
{
    TRACE("ScThreadExit(%" PRIiIN ")", ExitCode);

    // Does not return. The syscall is never completed for the exiting thread.
    ThreadExit(ExitCode);
}

/**
 * @brief Waits for a thread and optionally copies its exit code to the caller.
 * ThreadJoin checks the target and keeps it allocated across the wait. ExitCode
 * may be NULL when only completion matters. A failed join leaves it unchanged.
 *
 * @return The ThreadJoin status. The target's exit code is a separate result.
 */
oserr_t
ScThreadJoin(
        _In_  uuid_t ThreadId,
        _Out_ int*   ExitCode)
{
    int     resultCode;
    oserr_t result;

    // ThreadJoin records the target reference in the waiting thread. If the
    // waiter is terminated, ThreadingFinish can release it without returning here.
    result = ThreadJoin(ThreadId, &resultCode);
    if (result == OS_EOK && ExitCode != NULL) {
        *ExitCode = resultCode;
    }
    return result;
}

// ThreadDetach checks that the caller is the creator or shares the target's
// process memory group before removing it from the parent's child list.
oserr_t
ScThreadDetach(
        _In_ uuid_t ThreadId)
{
    return ThreadDetach(ThreadId);
}

/**
 * @brief Sends a signal to a thread sharing the caller's process memory group.
 * The target stays referenced from the relationship check through submission.
 * A submitted signal passes a second reference to the core callback. That
 * reference is released when the callback is done, possibly after this call returns.
 *
 * @return A relationship or lookup error, or the SignalSend result. OS_EOK
 *         reports submission, without waiting for the user handler to run.
 */
oserr_t
ScThreadSignal(
        _In_ uuid_t ThreadId,
        _In_ int    SignalCode)
{
    Thread_t* target;
    oserr_t   result;

    // Keep the ID tied to the same thread while checking permission and sending.
    // Without a reference, cleanup could remove it between those two operations.
    result = AcquireGlobalHandle(ThreadId, HandleTypeThread, (void**)&target);
    if (result != OS_EOK) {
        return result;
    }
    
    result = ThreadIsRelated(ThreadId, ThreadCurrentHandle());
    if (result == OS_EOK) {
        result = SignalSend(ThreadId, SignalCode, NULL);
    }
    ReleaseGlobalHandle(ThreadId);
    return result;
}

uuid_t
ScThreadGetCurrentId(void)
{
    return ThreadCurrentHandle();
}

oserr_t
ScThreadYield(void)
{
    ArchThreadYield();
    return OS_EOK;
}

uuid_t
ScThreadCookie(void)
{
    return ThreadCookie(ThreadCurrentForCore(ArchGetProcessorCoreId()));
}

oserr_t
ScThreadSetCurrentName(const char* ThreadName)
{
    Thread_t* thread = ThreadCurrentForCore(ArchGetProcessorCoreId());
    return ThreadSetName(thread, ThreadName);
}

// The caller supplies room for MaxLength bytes. strncpy pads a short name but
// does not add a terminator when the name fills the entire requested length.
oserr_t
ScThreadGetCurrentName(char* ThreadNameBuffer, size_t MaxLength)
{
    const char* threadName = ThreadName(ThreadCurrentForCore(ArchGetProcessorCoreId()));
    if (!threadName || !ThreadNameBuffer) {
        return OS_EUNKNOWN;
    }

    strncpy(ThreadNameBuffer, threadName, MaxLength);
    return OS_EOK;
}
