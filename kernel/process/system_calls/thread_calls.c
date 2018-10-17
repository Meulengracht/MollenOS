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
 * MollenOS MCore - System Calls
 */
#define __MODULE "SCIF"
//#define __TRACE

#include <os/osdefs.h>
#include <system/thread.h>
#include <system/utils.h>
#include <threading.h>
#include <scheduler.h>
#include <timers.h>
#include <debug.h>
#include <heap.h>

/* ScThreadCreate
 * Creates a new thread bound to 
 * the calling process, with the given entry point and arguments */
UUId_t
ScThreadCreate(
    _In_ ThreadEntry_t      Entry, 
    _In_ void*              Data, 
    _In_ Flags_t            Flags)
{
    if (Entry == NULL) {
        return UUID_INVALID;
    }
    return ThreadingCreateThread(NULL, Entry, Data, ThreadingGetCurrentMode() | THREADING_INHERIT | Flags);
}

/* ScThreadExit
 * Exits the current thread and instantly yields control to scheduler */
OsStatus_t
ScThreadExit(
    _In_ int ExitCode)
{
    return ThreadingTerminateThread(ThreadingGetCurrentThreadId(), ExitCode, 1);
}

/* ScThreadJoin
 * Thread join, waits for a given
 * thread to finish executing, and returns it's exit code */
OsStatus_t
ScThreadJoin(
    _In_  UUId_t    ThreadId,
    _Out_ int*      ExitCode)
{
    int     ResultCode      = 0;
    UUId_t  PId             = ThreadingGetCurrentThread(CpuGetCurrentId())->AshId;
    MCoreThread_t*  Thread  = ThreadingGetThread(ThreadId);
    
    if (Thread == NULL || Thread->AshId != PId) {
        return OsError;
    }

    ResultCode = ThreadingJoinThread(ThreadId);
    if (ExitCode != NULL) {
        *ExitCode = ResultCode;
    }
    return OsSuccess;
}

/* ScThreadDetach
 * Unattaches a running thread from a process, making sure it lives
 * past the lifetime of a process unless killed. */
OsStatus_t
ScThreadDetach(
    _In_  UUId_t    ThreadId)
{
    return ThreadingDetachThread(ThreadId);
}

/* ScThreadSignal
 * Kills the thread with the given id, owner must be same process */
OsStatus_t
ScThreadSignal(
    _In_ UUId_t     ThreadId,
    _In_ int        SignalCode)
{
    UUId_t          PId     = ThreadingGetCurrentThread(CpuGetCurrentId())->AshId;
    MCoreThread_t*  Thread  = ThreadingGetThread(ThreadId);

    // Perform security checks
    if (Thread == NULL || Thread->AshId != PId) {
        ERROR("Thread does not belong to same process");
        return OsError;
    }
    return SignalCreate(ThreadId, SignalCode);
}

/* ScThreadSleep
 * Sleeps the current thread for the given milliseconds. */
OsStatus_t
ScThreadSleep(
    _In_  time_t    Milliseconds,
    _Out_ time_t*   MillisecondsSlept)
{
    clock_t Start   = 0;
    clock_t End     = 0;
    
    // Initiate start
    TimersGetSystemTick(&Start);
    if (SchedulerThreadSleep(NULL, Milliseconds) == SCHEDULER_SLEEP_INTERRUPTED) {
        End = ThreadingGetCurrentThread(CpuGetCurrentId())->Sleep.InterruptedAt;
    }
    else {
        TimersGetSystemTick(&End);
    }

    // Update outs
    if (MillisecondsSlept != NULL) {
        *MillisecondsSlept = (time_t)(End - Start);
    }
    return OsSuccess;
}

/* ScThreadGetCurrentId
 * Retrieves the thread id of the calling thread */
UUId_t
ScThreadGetCurrentId(void) {
    return ThreadingGetCurrentThreadId();
}

/* ScThreadYield
 * This yields the current thread and gives cpu time to another thread */
OsStatus_t
ScThreadYield(void) {
    ThreadingYield();
    return OsSuccess;
}

/* ScThreadSetCurrentName
 * Changes the name of the current thread to the one specified by ThreadName. */
OsStatus_t
ScThreadSetCurrentName(const char *ThreadName) 
{
    MCoreThread_t*  Thread          = ThreadingGetCurrentThread(CpuGetCurrentId());
    const char*     PreviousName    = NULL;

    if (Thread == NULL || ThreadName == NULL) {
        return OsError;
    }
    PreviousName = Thread->Name;
    Thread->Name = strdup(ThreadName);
    kfree((void*)PreviousName);
    return OsSuccess;
}

/* ScThreadGetCurrentName
 * Retrieves the name of the current thread, up to max specified bytes. */
OsStatus_t
ScThreadGetCurrentName(char *ThreadNameBuffer, size_t MaxLength)
{
    MCoreThread_t* Thread = ThreadingGetCurrentThread(CpuGetCurrentId());

    if (Thread == NULL || ThreadNameBuffer == NULL) {
        return OsError;
    }
    strncpy(ThreadNameBuffer, Thread->Name, MaxLength);
    return OsSuccess;
}
