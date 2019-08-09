/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Process Manager
 * - Contains the implementation of the process-manager which keeps track
 *   of running applications.
 */
//#define __TRACE

#include <ds/mstring.h>
#include <ddk/services/process.h>
#include <ddk/service.h>
#include <ddk/utils.h>
#include <string.h>
#include <stdio.h>
#include "process.h"

OsStatus_t
OnLoad(
    _In_ char** ServicePathOut)
{
    *ServicePathOut = SERVICE_PROCESS_PATH;
    return InitializeProcessManager();
}

OsStatus_t
OnUnload(void)
{
    return OsSuccess;
}

OsStatus_t
OnEvent(
	_In_ MRemoteCall_t* RPC)
{
    OsStatus_t Handled = OsInvalidParameters;
    TRACE("Processmanager.OnEvent(%i)", RPC->Function);

    switch (RPC->Function) {
        case __PROCESSMANAGER_CREATE_PROCESS: {
            const char*                  Path               = RPCGetStringArgument(RPC, 0);
            ProcessStartupInformation_t* StartupInformation = RPCGetPointerArgument(RPC, 1);
            UUId_t                       Handle;
            OsStatus_t                   Result;

            Result = CreateProcess(RPC->From.Process, Path, StartupInformation, 
                RPCGetStringArgument(RPC, 3), RPC->Arguments[3].Length, 
                RPCGetPointerArgument(RPC, 2), RPC->Arguments[2].Length, &Handle);
            if (Result != OsSuccess) {
                Handle = UUID_INVALID;
            }
            Handled = RPCRespond(&RPC->From, (const void*)&Handle, sizeof(UUId_t));
        } break;

        case __PROCESSMANAGER_JOIN_PROCESS: {
            JoinProcessPackage_t Package = { .Status = OsDoesNotExist };
            Process_t*           Process = AcquireProcess(RPC->Arguments[0].Data.Value);
            size_t               Timeout = RPC->Arguments[1].Data.Value;
            if (Process != NULL) {
                Package.Status = JoinProcess(Process, &RPC->From, Timeout);
                ReleaseProcess(Process);
                if (Package.Status == OsSuccess) {
                    // Delayed response/already responded
                    Handled = OsSuccess;
                    break;
                }
            }
            Handled = RPCRespond(&RPC->From, (const void*)&Package, sizeof(JoinProcessPackage_t));
        } break;

        case __PROCESSMANAGER_KILL_PROCESS: {
            Process_t* Process       = AcquireProcess(RPC->From.Process);
            Process_t* TargetProcess = AcquireProcess(RPC->Arguments[0].Data.Value);
            OsStatus_t Result        = OsInvalidPermissions;
            if (Process != NULL) {
                Result = OsDoesNotExist;
                if (TargetProcess != NULL) {
                    Result = KillProcess(Process, TargetProcess);
                    ReleaseProcess(TargetProcess);
                }
                ReleaseProcess(Process);
            }
            Handled = RPCRespond(&RPC->From, (const void*)&Result, sizeof(OsStatus_t));
        } break;

        case __PROCESSMANAGER_TERMINATE_PROCESS: {
            Process_t* Process  = AcquireProcess(RPC->From.Process);
            int        ExitCode = (int)RPC->Arguments[0].Data.Value;
            OsStatus_t Result   = OsDoesNotExist;
            if (Process != NULL) {
                Result = TerminateProcess(Process, ExitCode);
                ReleaseProcess(Process);
            }
            Handled = RPCRespond(&RPC->From, (const void*)&Result, sizeof(OsStatus_t));
        } break;

        case __PROCESSMANAGER_GET_PROCESS_ID: {
            Process_t* Process   = GetProcessByPrimaryThread(RPC->From.Thread);
            UUId_t     ProcessId = UUID_INVALID;
            if (Process != NULL) {
                ProcessId = Process->Header.Key.Value.Id;
            }
            Handled = RPCRespond(&RPC->From, (const void*)&ProcessId, sizeof(UUId_t));
        } break;

        case __PROCESSMANAGER_GET_ARGUMENTS: {
            Process_t* Process = AcquireProcess(RPC->From.Process);
            void*      NullPtr = NULL;
            if (Process == NULL) {
                Handled = RPCRespond(&RPC->From, (const void*)&NullPtr, sizeof(void*));
            }
            else {
                Handled = RPCRespond(&RPC->From, (const void*)Process->Arguments, Process->ArgumentsLength);
                ReleaseProcess(Process);
            }
        } break;

        case __PROCESSMANAGER_GET_INHERIT_BLOCK: {
            Process_t* Process = AcquireProcess(RPC->From.Process);
            void*      NullPtr = NULL;
            if (Process == NULL || Process->InheritationBlockLength == 0) {
                Handled = RPCRespond(&RPC->From, (const void*)&NullPtr, sizeof(void*));
            }
            else {
                Handled = RPCRespond(&RPC->From, (const void*)Process->InheritationBlock, Process->InheritationBlockLength);
            }

            if (Process != NULL) {
                ReleaseProcess(Process);
            }
        } break;

        case __PROCESSMANAGER_GET_PROCESS_NAME: {
            Process_t* Process = AcquireProcess(RPC->From.Process);
            void*      NullPtr = NULL;
            if (Process == NULL) {
                Handled = RPCRespond(&RPC->From, (const void*)&NullPtr, sizeof(void*));
            }
            else {
                Handled = RPCRespond(&RPC->From, (const void*)MStringRaw(Process->Name), MStringSize(Process->Name) + 1);
                ReleaseProcess(Process);
            }
        } break;

        case __PROCESSMANAGER_GET_PROCESS_TICK: {
            Process_t* Process = AcquireProcess(RPC->From.Process);
            clock_t    Tick    = 0;
            if (Process != NULL) {
                Tick = clock() - Process->StartedAt;
                ReleaseProcess(Process);
            }
            Handled = RPCRespond(&RPC->From, (const void*)&Tick, sizeof(clock_t));
        } break;

        case __PROCESSMANAGER_GET_ASSEMBLY_DIRECTORY: {
            UUId_t     ProcessHandle = RPC->Arguments[0].Data.Value;
            void*      NullPtr       = NULL;
            Process_t* Process       = AcquireProcess((ProcessHandle == UUID_INVALID) ? RPC->From.Process : ProcessHandle);
            
            if (Process == NULL) {
                Handled = RPCRespond(&RPC->From, (const void*)&NullPtr, sizeof(void*));
            }
            else {
                Handled = RPCRespond(&RPC->From, (const void*)MStringRaw(Process->AssemblyDirectory), MStringSize(Process->AssemblyDirectory) + 1);
                ReleaseProcess(Process);
            }
        } break;

        case __PROCESSMANAGER_GET_WORKING_DIRECTORY: {
            UUId_t     ProcessHandle = RPC->Arguments[0].Data.Value;
            void*      NullPtr       = NULL;
            Process_t* Process       = AcquireProcess((ProcessHandle == UUID_INVALID) ? RPC->From.Process : ProcessHandle);

            if (Process == NULL) {
                TRACE("proc_get_cwd => invalid proc");
                Handled = RPCRespond(&RPC->From, (const void*)&NullPtr, sizeof(void*));
            }
            else {
                TRACE("proc_get_cwd => %s", MStringRaw(Process->WorkingDirectory));
                Handled = RPCRespond(&RPC->From, (const void*)MStringRaw(Process->WorkingDirectory), MStringSize(Process->WorkingDirectory) + 1);
                ReleaseProcess(Process);
            }
        } break;

        case __PROCESSMANAGER_SET_WORKING_DIRECTORY: {
            Process_t*  Process = AcquireProcess(RPC->From.Process);
            OsStatus_t  Result  = OsDoesNotExist;
            const char* Path    = RPCGetStringArgument(RPC, 0);
            if (Process != NULL) {
                Result = OsInvalidParameters;
                if (Path != NULL) {
                    TRACE("proc_set_cwd(%s)", Path);
                    MStringDestroy(Process->WorkingDirectory);
                    Process->WorkingDirectory = MStringCreate((void*)Path, StrUTF8);
                    Result = OsSuccess;
                }
                ReleaseProcess(Process);
            }
            Handled = RPCRespond(&RPC->From, (const void*)&Result, sizeof(OsStatus_t));
        } break;

        case __PROCESSMANAGER_GET_LIBRARY_HANDLES: {
            Process_t* Process = AcquireProcess(RPC->From.Process);
            Handle_t   LibraryList[PROCESS_MAXMODULES];
            memset(&LibraryList[0], 0, sizeof(LibraryList));

            if (Process != NULL) {
                GetProcessLibraryHandles(Process, LibraryList);
                ReleaseProcess(Process);
            }
            Handled = RPCRespond(&RPC->From, (const void*)&LibraryList[0], sizeof(LibraryList));
        } break;

        case __PROCESSMANAGER_GET_LIBRARY_ENTRIES: {
            Process_t* Process = AcquireProcess(RPC->From.Process);
            Handle_t   LibraryList[PROCESS_MAXMODULES];
            memset(&LibraryList[0], 0, sizeof(LibraryList));

            if (Process != NULL) {
                GetProcessLibraryEntryPoints(Process, LibraryList);
                ReleaseProcess(Process);
            }
            Handled = RPCRespond(&RPC->From, (const void*)&LibraryList[0], sizeof(LibraryList));
        } break;

        case __PROCESSMANAGER_LOAD_LIBRARY: {
            Process_t*  Process = AcquireProcess(RPC->From.Process);
            const char* Path    = RPCGetStringArgument(RPC, 0);
            Handle_t    Handle  = HANDLE_INVALID;
            if (Process != NULL) {
                LoadProcessLibrary(Process, Path, &Handle);
                ReleaseProcess(Process);
            }
            Handled = RPCRespond(&RPC->From, (const void*)&Handle, sizeof(Handle_t));
        } break;

        case __PROCESSMANAGER_RESOLVE_FUNCTION: {
            Process_t*  Process  = AcquireProcess(RPC->From.Process);
            Handle_t    Handle   = (Handle_t)RPC->Arguments[0].Data.Value;
            const char* Function = RPCGetStringArgument(RPC, 1);
            uintptr_t   Address  = 0;
            if (Process != NULL) {
                Address = ResolveProcessLibraryFunction(Process, Handle, Function);
                ReleaseProcess(Process);
            }
            Handled = RPCRespond(&RPC->From, (const void*)&Address, sizeof(uintptr_t));
        } break;

        case __PROCESSMANAGER_UNLOAD_LIBRARY: {
            Process_t* Process = AcquireProcess(RPC->From.Process);
            OsStatus_t Result  = OsDoesNotExist;
            if (Process != NULL) {
                Result = UnloadProcessLibrary(Process, (Handle_t)RPC->Arguments[0].Data.Value);
                ReleaseProcess(Process);
            }
            Handled = RPCRespond(&RPC->From, (const void*)&Result, sizeof(OsStatus_t));
        } break;

        case __PROCESSMANAGER_CRASH_REPORT: {
            // so far what do, we need to specify a report structure
            Process_t* Process      = AcquireProcess(RPC->From.Process);
            Context_t* CrashContext = RPCGetPointerArgument(RPC, 0);
            int        CrashReason  = (int)RPC->Arguments[1].Data.Value;
            OsStatus_t Result       = OsDoesNotExist;
            if (Process != NULL) {
                Result = HandleProcessCrashReport(Process, CrashContext, CrashReason);
            }
            Handled = RPCRespond(&RPC->From, (const void*)&Result, sizeof(OsStatus_t));
        }
        
        default: {
            break;
        }
    }
    return Handled;
}
