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
 * System API - Threading calls
 */
#define __MODULE "SCIF"
//#define __TRACE

#include <arch/thread.h>
#include <arch/utils.h>
#include <assert.h>
#include <os/mollenos.h>
#include <threading.h>
#include <string.h>
#include <debug.h>

oserr_t
ScThreadCreate(
        _In_  ThreadEntry_t       Entry,
        _In_  void*               Arguments,
        _In_  ThreadParameters_t* Parameters,
        _Out_ uuid_t*             HandleOut)
{
    unsigned int threadCurrentMode = ThreadCurrentMode();
    uuid_t       memorySpaceHandle = UUID_INVALID;
    const char*  name              = NULL;
    if (Entry == NULL) {
        return OsInvalidParameters;
    }
    
    // Handle additional paramaters
    if (Parameters != NULL) {
        name               = Parameters->Name;
        threadCurrentMode |= Parameters->Configuration;
        memorySpaceHandle  = Parameters->MemorySpaceHandle;
    }
    
    // If a memory space is not provided, then we inherit
    if (memorySpaceHandle == UUID_INVALID) {
        threadCurrentMode |= THREADING_INHERIT;
    }

    // Assert max stack size
    return ThreadCreate(name, Entry, Arguments, threadCurrentMode, memorySpaceHandle,
                        0, 0, HandleOut);
}

oserr_t
ScThreadExit(
    _In_ int ExitCode)
{
    TRACE("ScThreadExit(%" PRIiIN ")", ExitCode);
    return ThreadTerminate(ThreadCurrentHandle(), ExitCode, 1);
}

oserr_t
ScThreadJoin(
        _In_  uuid_t ThreadId,
        _Out_ int*   ExitCode)
{
    int        ResultCode;
    oserr_t Result = ThreadIsRelated(ThreadId, ThreadCurrentHandle());

    if (Result == OsOK) {
        ResultCode = ThreadJoin(ThreadId);
        if (ExitCode != NULL) {
            *ExitCode = ResultCode;
        }
    }
    TRACE("ScThreadJoin(%" PRIuIN ") => %" PRIuIN "", ThreadId, Result);
    return Result;
}

oserr_t
ScThreadDetach(
        _In_ uuid_t ThreadId)
{
    return ThreadDetach(ThreadId);
}

oserr_t
ScThreadSignal(
        _In_ uuid_t ThreadId,
        _In_ int    SignalCode)
{
    oserr_t Result = ThreadIsRelated(ThreadId, ThreadCurrentHandle());
    if (Result == OsOK) {
        Result = SignalSend(ThreadId, SignalCode, NULL);
    }
    return Result;
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
    return OsOK;
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

oserr_t
ScThreadGetCurrentName(char* ThreadNameBuffer, size_t MaxLength)
{
    const char* threadName = ThreadName(ThreadCurrentForCore(ArchGetProcessorCoreId()));
    if (!threadName || !ThreadNameBuffer) {
        return OsError;
    }

    strncpy(ThreadNameBuffer, threadName, MaxLength);
    return OsOK;
}
