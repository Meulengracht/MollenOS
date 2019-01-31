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

UUId_t
ScThreadCreate(
    _In_ ThreadEntry_t Entry,
    _In_ void*         Arguments,
    _In_ Flags_t       Flags,
    _In_ UUId_t        MemorySpaceHandle)
{
    UUId_t  Handle      = UUID_INVALID;
    Flags_t ThreadFlags = ThreadingGetCurrentMode() | THREADING_INHERIT | Flags;
    if (Entry != NULL) {
        if (CreateThread(NULL, Entry, Arguments, ThreadFlags, MemorySpaceHandle, &Handle) != OsSuccess) {
            return UUID_INVALID;
        }
    }
    return Handle;
}

OsStatus_t
ScThreadExit(
    _In_ int ExitCode)
{
    TRACE("ScThreadExit(%i)", ExitCode);
    return TerminateThread(GetCurrentThreadId(), ExitCode, 1);
}

OsStatus_t
ScThreadJoin(
    _In_  UUId_t ThreadId,
    _Out_ int*   ExitCode)
{
    int        ResultCode = 0;
    OsStatus_t Result     = AreThreadsRelated(ThreadId, GetCurrentThreadId());

    if (Result == OsSuccess) {
        ResultCode = ThreadingJoinThread(ThreadId);
        if (ExitCode != NULL) {
            *ExitCode = ResultCode;
        }
    }
    TRACE("ScThreadJoin(%u) => %u", ThreadId, Result);
    return Result;
}

OsStatus_t
ScThreadDetach(
    _In_ UUId_t ThreadId)
{
    return ThreadingDetachThread(ThreadId);
}

OsStatus_t
ScThreadSignal(
    _In_ UUId_t     ThreadId,
    _In_ int        SignalCode)
{
    OsStatus_t Result = AreThreadsRelated(ThreadId, GetCurrentThreadId());
    if (Result == OsSuccess) {
        Result = SignalCreate(ThreadId, SignalCode);
    }
    return Result;
}

OsStatus_t
ScThreadSleep(
    _In_  time_t    Milliseconds,
    _Out_ time_t*   MillisecondsSlept)
{
    clock_t Start   = 0;
    clock_t End     = 0;

    TimersGetSystemTick(&Start);
    if (SchedulerThreadSleep(NULL, Milliseconds) == SCHEDULER_SLEEP_INTERRUPTED) {
        End = GetCurrentThreadForCore(ArchGetProcessorCoreId())->Sleep.InterruptedAt;
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

UUId_t
ScThreadGetCurrentId(void)
{
    return GetCurrentThreadId();
}

OsStatus_t
ScThreadYield(void)
{
    ThreadingYield();
    return OsSuccess;
}

UUId_t ScThreadCookie(void)
{
    return GetCurrentThreadForCore(ArchGetProcessorCoreId())->Cookie;
}

OsStatus_t
ScThreadSetCurrentName(const char *ThreadName) 
{
    MCoreThread_t* Thread       = GetCurrentThreadForCore(ArchGetProcessorCoreId());
    const char*    PreviousName = NULL;

    if (Thread == NULL || ThreadName == NULL) {
        return OsError;
    }
    PreviousName = Thread->Name;
    Thread->Name = strdup(ThreadName);
    kfree((void*)PreviousName);
    return OsSuccess;
}

OsStatus_t
ScThreadGetCurrentName(char *ThreadNameBuffer, size_t MaxLength)
{
    MCoreThread_t* Thread = GetCurrentThreadForCore(ArchGetProcessorCoreId());

    if (Thread == NULL || ThreadNameBuffer == NULL) {
        return OsError;
    }
    strncpy(ThreadNameBuffer, Thread->Name, MaxLength);
    return OsSuccess;
}
