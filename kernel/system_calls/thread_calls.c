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

#include <arch/thread.h>
#include <arch/utils.h>
#include <assert.h>
#include <os/mollenos.h>
#include <threading.h>
#include <scheduler.h>
#include <timers.h>
#include <string.h>
#include <debug.h>
#include <heap.h>

OsStatus_t
ScThreadCreate(
    _In_  ThreadEntry_t       Entry,
    _In_  void*               Arguments,
    _In_  ThreadParameters_t* Parameters,
    _Out_ UUId_t*             HandleOut)
{
    Flags_t     ThreadFlags       = ThreadingGetCurrentMode();
    UUId_t      MemorySpaceHandle = UUID_INVALID;
    const char* Name              = NULL;
    if (Entry == NULL) {
        return OsInvalidParameters;
    }
    
    // Handle additional paramaters
    if (Parameters != NULL) {
        Name              = Parameters->Name;
        ThreadFlags      |= Parameters->Configuration;
        MemorySpaceHandle = Parameters->MemorySpaceHandle;
    }
    
    // If a memory space is not provided, then we inherit
    if (MemorySpaceHandle == UUID_INVALID) {
        ThreadFlags |= THREADING_INHERIT;
    }
    return CreateThread(Name, Entry, Arguments, ThreadFlags, MemorySpaceHandle, HandleOut);
}

OsStatus_t
ScThreadExit(
    _In_ int ExitCode)
{
    TRACE("ScThreadExit(%" PRIiIN ")", ExitCode);
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
    TRACE("ScThreadJoin(%" PRIuIN ") => %" PRIuIN "", ThreadId, Result);
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
    _In_ UUId_t ThreadId,
    _In_ int    SignalCode)
{
    OsStatus_t Result = AreThreadsRelated(ThreadId, GetCurrentThreadId());
    if (Result == OsSuccess) {
        Result = SignalSend(ThreadId, SignalCode, NULL);
    }
    return Result;
}

OsStatus_t
ScThreadSleep(
    _In_  time_t  Milliseconds,
    _Out_ time_t* MillisecondsSlept)
{
    clock_t Start   = 0;
    clock_t End     = 0;

    TimersGetSystemTick(&Start);
    if (SchedulerSleep(Milliseconds, &End) != SCHEDULER_SLEEP_INTERRUPTED) {
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

UUId_t
ScThreadCookie(void)
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

OsStatus_t
ScThreadGetContext(
    _In_ Context_t* Context)
{
    MCoreThread_t* Thread = GetCurrentThreadForCore(ArchGetProcessorCoreId());
    if (Thread == NULL || Context == NULL) {
        return OsError;
    }
    memcpy(Context, Thread->ContextActive, sizeof(Context_t));
    return OsSuccess;
}

OsStatus_t
ScGetSignalOriginalContext(
    _In_ Context_t* Context)
{
    MCoreThread_t* Thread;
    
    if (Context == NULL) {
        return OsInvalidParameters;
    }
    
    Thread = GetCurrentThreadForCore(ArchGetProcessorCoreId());
    assert(Thread != NULL);
    
    // Either we have the original context stored because we are currently
    // handling a signal, or the userspace should locally get its context
    if (!Thread->Signaling.OriginalContext) {
        return OsDoesNotExist;
    }
    
    memcpy(Context, Thread->Signaling.OriginalContext, sizeof(Context_t));
    return OsSuccess;
}
