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

#include <process/process.h>
#include <process/pe.h>
#include <system/utils.h>
#include <ds/mstring.h>
#include <threading.h>
#include <scheduler.h>
#include <handle.h>
#include <debug.h>
#include <heap.h>

/* ScProcessSpawn
 * Spawns a new process with the given path
 * and the given arguments, returns UUID_INVALID on failure */
UUId_t
ScProcessSpawn(
    _In_ const char*                    Path,
    _In_ ProcessStartupInformation_t*   StartupInformation)
{
    MString_t*  mPath;
    UUId_t      Result = UUID_INVALID;
    
    if (Path == NULL || StartupInformation == NULL) {
        return Result;
    }

    mPath = MStringCreate((void*)Path, StrUTF8);
    if (CreateProcess(mPath, StartupInformation, ProcessNormal, &Result) != OsSuccess) {
        ERROR(" > failed to spawn process %s", Path);
    }
    MStringDestroy(mPath);
    return Result;
}

/* ScProcessJoin
 * Wait for a given process to exit, optionaly specifying a timeout.
 * The exit-code for the process will be returned. */
OsStatus_t
ScProcessJoin(
    _In_  UUId_t    ProcessHandle,
    _In_  size_t    Timeout,
    _Out_ int*      ExitCode)
{
    SystemProcess_t* Process = GetProcess(ProcessHandle);
    if (Process != NULL) {
        TRACE("Waiting for handle %u", ProcessHandle);
        WaitForHandles(&ProcessHandle, 1, 1, Timeout);
        if (ExitCode != NULL) {
            *ExitCode = Process->Code;
        }
        TRACE("We've been waken up on handle %u", ProcessHandle);
        return OsSuccess;
    }
    return OsError;
}

/* ScProcessKill
 * Kills the given process-id, it does not guarantee instant kill. */
OsStatus_t
ScProcessKill(
    _In_ UUId_t ProcessHandle)
{
    SystemProcess_t*    Process = GetProcess(ProcessHandle);
    OsStatus_t          Status  = OsDoesNotExist;
    // @security checks
    if (Process != NULL) {
        WARNING("Process %s killed", MStringRaw(Process->Name));
        Process->Code = 0;
        Status = ThreadingTerminateThread(Process->MainThreadId, 0, 1);
    }
    return Status;
}

/* ScProcessExit
 * Kills the entire process and all non-detached threads that has been spawned by the process. */
OsStatus_t
ScProcessExit(
    _In_ int ExitCode)
{
    MCoreThread_t*      Thread  = ThreadingGetCurrentThread(CpuGetCurrentId());
    SystemProcess_t*    Process = GetCurrentProcess();
    OsStatus_t          Status  = OsError;
    if (Process != NULL) {
        WARNING("Process %s terminated with code %i", MStringRaw(Process->Name), ExitCode);
        Process->Code = ExitCode;

        // Are we detached? Then call only thread cleanup
        if (Thread->ParentThreadId == UUID_INVALID) {
            Status = ThreadingTerminateThread(Thread->Id, ExitCode, 1);
        }
        else {
            Status = ThreadingTerminateThread(Process->MainThreadId, ExitCode, 1);
        }
    }
    return Status;
}

/* ScProcessGetCurrentId 
 * Retrieves the current process identifier. */
OsStatus_t 
ScProcessGetCurrentId(
    _In_ UUId_t* ProcessHandle)
{
    *ProcessHandle = ThreadingGetCurrentThread(CpuGetCurrentId())->ProcessHandle;
    return OsSuccess;
}

/* ScProcessGetCurrentName
 * Retreieves the current process name */
OsStatus_t
ScProcessGetCurrentName(const char *Buffer, size_t MaxLength)
{
    SystemProcess_t* Process = GetCurrentProcess();
    if (Process == NULL) {
        return OsError;
    }
    memset((void*)Buffer, 0, MaxLength);
    memcpy((void*)Buffer, MStringRaw(Process->Name), MIN(MStringSize(Process->Name) + 1, MaxLength));
    return OsSuccess;
}

/* ScProcessSignal
 * Installs a default signal handler for the given process. */
OsStatus_t
ScProcessSignal(
    _In_ uintptr_t Handler) 
{
    SystemProcess_t* Process = GetCurrentProcess();
    if (Process == NULL) {
        return OsError;
    }
    Process->SignalHandler = Handler;
    return OsSuccess;
}

/* Dispatches a signal to the target process id 
 * It will get handled next time it's selected for execution 
 * so we yield instantly as well. If ProcessHandle is -1, we select self */
OsStatus_t
ScProcessRaise(
    _In_ UUId_t ProcessHandle, 
    _In_ int    Signal)
{
    SystemProcess_t* Process = GetProcess(ProcessHandle);
    if (Process == NULL) {
        return OsError;
    }
    return SignalCreate(Process->MainThreadId, Signal);
}

/* ScProcessGetStartupInformation
 * Retrieves information passed about process startup. */
OsStatus_t
ScProcessGetStartupInformation(
    _In_ ProcessStartupInformation_t* StartupInformation)
{
    SystemProcess_t* Process;
    if (StartupInformation == NULL) {
        return OsError;
    }

    Process = GetCurrentProcess();
    if (Process == NULL) {
        return OsError;
    }

    if (Process->StartupInformation.ArgumentPointer != NULL) {
        if (StartupInformation->ArgumentPointer != NULL) {
            memcpy((void*)StartupInformation->ArgumentPointer, 
                Process->StartupInformation.ArgumentPointer,
                MIN(StartupInformation->ArgumentLength, Process->StartupInformation.ArgumentLength));
            StartupInformation->ArgumentLength = MIN(StartupInformation->ArgumentLength, Process->StartupInformation.ArgumentLength);
        }
        else {
            StartupInformation->ArgumentLength = Process->StartupInformation.ArgumentLength;
        }
    }
    else {
        StartupInformation->ArgumentLength = 0;
    }
    if (Process->StartupInformation.InheritanceBlockPointer != NULL) {
        if (StartupInformation->InheritanceBlockPointer != NULL) {
            size_t BytesToCopy = MIN(StartupInformation->InheritanceBlockLength, 
                    Process->StartupInformation.InheritanceBlockLength);
            memcpy((void*)StartupInformation->InheritanceBlockPointer, 
                Process->StartupInformation.InheritanceBlockPointer, BytesToCopy);
            StartupInformation->InheritanceBlockLength = BytesToCopy;
        }
        else {
            StartupInformation->InheritanceBlockLength = Process->StartupInformation.InheritanceBlockLength;
        }
    }
    else {
        StartupInformation->InheritanceBlockLength = 0;
    }
    return OsSuccess;
}

/* ScProcessGetModuleHandles
 * Retrieves a list of loaded module handles. Handles can be queried
 * for various application-image data. */
OsStatus_t
ScProcessGetModuleHandles(
    _In_ Handle_t ModuleList[PROCESS_MAXMODULES])
{
    SystemProcess_t* Process = GetCurrentProcess();
    if (Process == NULL) {
        return OsError;
    }
    return PeGetModuleHandles(Process->Executable, ModuleList);
}

/* ScProcessGetModuleEntryPoints
 * Retrieves a list of loaded module entry points. */
OsStatus_t
ScProcessGetModuleEntryPoints(
    _In_ Handle_t ModuleList[PROCESS_MAXMODULES])
{
    SystemProcess_t* Process = GetCurrentProcess();
    if (Process == NULL) {
        return OsError;
    }
    return PeGetModuleEntryPoints(Process->Executable, ModuleList);
}
