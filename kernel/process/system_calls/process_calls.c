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
#include <process/phoenix.h>
#include <process/process.h>
#include <scheduler.h>
#include <debug.h>
#include <heap.h>

/* ScProcessSpawn
 * Spawns a new process with the given path
 * and the given arguments, returns UUID_INVALID on failure */
UUId_t
ScProcessSpawn(
    _In_ const char*                            Path,
    _In_ const ProcessStartupInformation_t*     StartupInformation,
    _In_ int                                    Asynchronous)
{
    // Variables
    MCorePhoenixRequest_t *Request  = NULL;
    MString_t *mPath                = NULL;
    UUId_t Result                   = UUID_INVALID;

    // Only the path cannot be null
    // Arguments are allowed to be null
    if (Path == NULL || StartupInformation == NULL) {
        return UUID_INVALID;
    }

    // Allocate resources for the spawn
    Request = (MCorePhoenixRequest_t*)kmalloc(sizeof(MCorePhoenixRequest_t));
    mPath = MStringCreate((void*)Path, StrUTF8);

    // Reset structure and set it up
    memset(Request, 0, sizeof(MCorePhoenixRequest_t));
    Request->Base.Type = AshSpawnProcess;
    Request->Path = mPath;
    Request->Base.Cleanup = Asynchronous;
    
    // Copy startup-information
    if (StartupInformation->ArgumentPointer != NULL
        && StartupInformation->ArgumentLength != 0) {
        Request->StartupInformation.ArgumentLength = 
            StartupInformation->ArgumentLength;
        Request->StartupInformation.ArgumentPointer = 
            (__CONST char*)kmalloc(StartupInformation->ArgumentLength);
        memcpy((void*)Request->StartupInformation.ArgumentPointer,
            StartupInformation->ArgumentPointer, 
            StartupInformation->ArgumentLength);
    }
    if (StartupInformation->InheritanceBlockPointer != NULL
        && StartupInformation->InheritanceBlockLength != 0) {
        Request->StartupInformation.InheritanceBlockLength = 
            StartupInformation->InheritanceBlockLength;
        Request->StartupInformation.InheritanceBlockPointer = 
            (__CONST char*)kmalloc(StartupInformation->InheritanceBlockLength);
        memcpy((void*)Request->StartupInformation.InheritanceBlockPointer,
            StartupInformation->InheritanceBlockPointer, 
            StartupInformation->InheritanceBlockLength);
    }

    // If it's an async request we return immediately
    // We return an invalid UUID as it cannot be used
    // for queries
    PhoenixCreateRequest(Request);
    if (Asynchronous != 0) {
        return UUID_INVALID;
    }

    // Otherwise wait for request to complete
    // and then cleanup and return the process id
    PhoenixWaitRequest(Request, 0);
    MStringDestroy(mPath);

    // Store result and cleanup
    Result = Request->AshId;
    kfree(Request);
    return Result;
}

/* ScProcessJoin
 * Wait for a given process to exit, optionaly specifying a timeout.
 * The exit-code for the process will be returned. */
OsStatus_t
ScProcessJoin(
    _In_  UUId_t    ProcessId,
    _In_  size_t    Timeout,
    _Out_ int*      ExitCode)
{
    // Variables
    MCoreAsh_t *Process = PhoenixGetAsh(ProcessId);
    int SleepResult     = 0;
    
    if (Process == NULL) {
        return OsError;
    }
    SleepResult = SchedulerThreadSleep((uintptr_t*)Process, Timeout);
    if (SleepResult == SCHEDULER_SLEEP_OK) {
        if (ExitCode != NULL) {
            *ExitCode = Process->Code;
        }
        return OsSuccess;
    }
    return OsError;
}

/* ScProcessKill
 * Kills the given process-id, it does not guarantee instant kill. */
OsStatus_t
ScProcessKill(
    _In_ UUId_t ProcessId)
{
    // Variables
    MCorePhoenixRequest_t Request;

    // Initialize the request
    memset(&Request, 0, sizeof(MCorePhoenixRequest_t));
    Request.Base.Type = AshKill;
    Request.AshId = ProcessId;

    // Create and wait with 1 second timeout
    PhoenixCreateRequest(&Request);
    PhoenixWaitRequest(&Request, 1000);
    if (Request.Base.State == EventOk) {
        return OsSuccess;
    }
    else {
        return OsError;
    }
}

/* ScProcessExit
 * Kills the entire process and all non-detached threads that
 * has been spawned by the process. */
OsStatus_t
ScProcessExit(
    _In_ int ExitCode)
{
    // Variables
    MCoreAsh_t *Process     = PhoenixGetCurrentAsh();
    if (Process == NULL) {
        return OsError;
    }
    
    // Debug
    WARNING("Process %s terminated with code %i", MStringRaw(Process->Name), ExitCode);

    // Terminate ash and current thread
    PhoenixTerminateAsh(Process, ExitCode, 0, 1);
    ThreadingKillThread(ThreadingGetCurrentThreadId(), ExitCode, 1);
    
    // @unreachable
    return OsSuccess;
}

/* ScProcessGetCurrentId 
 * Retrieves the current process identifier. */
OsStatus_t 
ScProcessGetCurrentId(
    UUId_t *ProcessId)
{
    MCoreAsh_t *Process = PhoenixGetCurrentAsh();
    if (ProcessId == NULL || Process == NULL) {
        return OsError;
    }
    return Process->Id;
}

/* ScProcessGetCurrentName
 * Retreieves the current process name */
OsStatus_t
ScProcessGetCurrentName(const char *Buffer, size_t MaxLength) {
    MCoreAsh_t *Process = PhoenixGetCurrentAsh();
    if (Process == NULL) {
        return OsError;
    }
    memset((void*)Buffer, 0, MaxLength);
    memcpy((void*)Buffer, MStringRaw(Process->Name), MIN(MStringSize(Process->Name), MaxLength));
    return OsSuccess;
}

/* ScProcessSignal
 * Installs a default signal handler for the given process. */
OsStatus_t
ScProcessSignal(
    _In_ uintptr_t Handler) 
{
    // Process
    MCoreAsh_t *Process = NULL;

    // Get current process
    Process = PhoenixGetCurrentAsh();
    if (Process == NULL) {
        return OsError;
    }

    Process->SignalHandler = Handler;
    return OsSuccess;
}

/* Dispatches a signal to the target process id 
 * It will get handled next time it's selected for execution 
 * so we yield instantly as well. If processid is -1, we select self */
OsStatus_t
ScProcessRaise(
    _In_ UUId_t ProcessId, 
    _In_ int    Signal)
{
    // Variables
    MCoreProcess_t *Process = NULL;

    // Lookup process
    Process = PhoenixGetProcess(ProcessId);
    if (Process == NULL) {
        return OsError;
    }

    // Create the signal
    return SignalCreate(Process->Base.MainThread, Signal);
}

/* ScProcessGetStartupInformation
 * Retrieves information passed about process startup. */
OsStatus_t
ScProcessGetStartupInformation(
    _InOut_ ProcessStartupInformation_t *StartupInformation)
{
    // Variables
    MCoreProcess_t *Process = NULL;

    // Sanitize parameters
    if (StartupInformation == NULL) {
        return OsError;
    }

    // Find process
    Process = PhoenixGetCurrentProcess();
    if (Process == NULL) {
        return OsError;
    }

    // Update outs
    if (Process->StartupInformation.ArgumentPointer != NULL) {
        if (StartupInformation->ArgumentPointer != NULL) {
            memcpy((void*)StartupInformation->ArgumentPointer, 
                Process->StartupInformation.ArgumentPointer,
                MIN(StartupInformation->ArgumentLength, 
                    Process->StartupInformation.ArgumentLength));
        }
        else {
            StartupInformation->ArgumentLength = 
                Process->StartupInformation.ArgumentLength;
        }
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
            StartupInformation->InheritanceBlockLength = 0;
        }
    }

    // Done
    return OsSuccess;
}

/* ScProcessGetModuleHandles
 * Retrieves a list of loaded module handles. Handles can be queried
 * for various application-image data. */
OsStatus_t
ScProcessGetModuleHandles(
    _In_ Handle_t ModuleList[PROCESS_MAXMODULES])
{
    // Variables
    MCoreAsh_t *Process = NULL;

    // Get current process
    Process = PhoenixGetCurrentAsh();
    if (Process == NULL) {
        return OsError;
    }

    // Redirect call to executable interface
    return PeGetModuleHandles(Process->Executable, ModuleList);
}

/* ScProcessGetModuleEntryPoints
 * Retrieves a list of loaded module entry points. */
OsStatus_t
ScProcessGetModuleEntryPoints(
    _In_ Handle_t ModuleList[PROCESS_MAXMODULES])
{
    // Variables
    MCoreAsh_t *Process = NULL;

    // Get current process
    Process = PhoenixGetCurrentAsh();
    if (Process == NULL) {
        return OsError;
    }

    // Redirect call to executable interface
    return PeGetModuleEntryPoints(Process->Executable, ModuleList);
}
