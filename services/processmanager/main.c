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
#include <ddk/utils.h>
#include <os/ipc.h>
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
    _In_ IpcMessage_t* Message)
{
    OsStatus_t Handled = OsInvalidParameters;
    TRACE("Processmanager.OnEvent(%i)", RPC->Function);

    switch (IPC_GET_TYPED(Message, 0)) {
        case __PROCESSMANAGER_CREATE_PROCESS: {
            const char*                  Path               = IPC_GET_STRING(Message, 0);
            ProcessStartupInformation_t* StartupInformation = IPC_GET_UNTYPED(Message, 1);
            UUId_t                       Handle;
            OsStatus_t                   Result;

            Result = CreateProcess(IPC_GET_TYPED(Message, 1), Path, StartupInformation, 
                IPC_GET_STRING(Message, 3), IPC_GET_LENGTH(Message, 3), 
                IPC_GET_UNTYPED(Message, 2), IPC_GET_LENGTH(Message, 2), &Handle);
            if (Result != OsSuccess) {
                Handle = UUID_INVALID;
            }
            
            Handled = IpcReply(Message, &Handle, sizeof(UUId_t));
        } break;

        case __PROCESSMANAGER_JOIN_PROCESS: {
            JoinProcessPackage_t Package = { .Status = OsDoesNotExist };
            Process_t*           Process = AcquireProcess(IPC_GET_TYPED(Message, 2));
            size_t               Timeout = IPC_GET_TYPED(Message, 3);
            
            if (Process != NULL) {
                Package.Status = JoinProcess(Process, Message->Sender, Timeout);
                ReleaseProcess(Process);
                if (Package.Status == OsSuccess) {
                    // Delayed response/already responded
                    Handled = OsSuccess;
                    break;
                }
            }
            Handled = IpcReply(Message, &Package, sizeof(JoinProcessPackage_t));
        } break;

        case __PROCESSMANAGER_KILL_PROCESS: {
            Process_t* Process       = AcquireProcess(IPC_GET_TYPED(Message, 1));
            Process_t* TargetProcess = AcquireProcess(IPC_GET_TYPED(Message, 2));
            OsStatus_t Result        = OsInvalidPermissions;
            
            if (Process != NULL) {
                Result = OsDoesNotExist;
                if (TargetProcess != NULL) {
                    Result = KillProcess(Process, TargetProcess);
                    ReleaseProcess(TargetProcess);
                }
                ReleaseProcess(Process);
            }
            Handled = IpcReply(Message, &Result, sizeof(OsStatus_t));
        } break;

        case __PROCESSMANAGER_TERMINATE_PROCESS: {
            Process_t* Process  = AcquireProcess(IPC_GET_TYPED(Message, 1));
            int        ExitCode = (int)IPC_GET_TYPED(Message, 2);
            OsStatus_t Result   = OsDoesNotExist;
            
            if (Process != NULL) {
                Result = TerminateProcess(Process, ExitCode);
                ReleaseProcess(Process);
            }
            Handled = IpcReply(Message, &Result, sizeof(OsStatus_t));
        } break;

        case __PROCESSMANAGER_GET_PROCESS_ID: {
            Process_t* Process   = GetProcessByPrimaryThread(Message->Sender);
            UUId_t     ProcessId = UUID_INVALID;
            
            if (Process != NULL) {
                ProcessId = Process->Header.Key.Value.Id;
            }
            Handled = IpcReply(Message, &ProcessId, sizeof(UUId_t));
        } break;

        case __PROCESSMANAGER_GET_ARGUMENTS: {
            Process_t* Process = AcquireProcess(IPC_GET_TYPED(Message, 1));
            void*      NullPtr = NULL;
            
            if (Process == NULL) {
                Handled = IpcReply(Message, &NullPtr, sizeof(void*));
            }
            else {
                Handled = IpcReply(Message, (void*)Process->Arguments, Process->ArgumentsLength);
                ReleaseProcess(Process);
            }
        } break;

        case __PROCESSMANAGER_GET_INHERIT_BLOCK: {
            Process_t* Process = AcquireProcess(IPC_GET_TYPED(Message, 1));
            void*      NullPtr = NULL;
            
            if (Process == NULL || Process->InheritationBlockLength == 0) {
                Handled = IpcReply(Message, &NullPtr, sizeof(void*));
            }
            else {
                Handled = IpcReply(Message, Process->InheritationBlock, Process->InheritationBlockLength);
            }

            if (Process != NULL) {
                ReleaseProcess(Process);
            }
        } break;

        case __PROCESSMANAGER_GET_PROCESS_NAME: {
            Process_t* Process = AcquireProcess(IPC_GET_TYPED(Message, 1));
            void*      NullPtr = NULL;
            
            if (Process == NULL) {
                Handled = IpcReply(Message, &NullPtr, sizeof(void*));
            }
            else {
                Handled = IpcReply(Message, (void*)MStringRaw(Process->Name), MStringSize(Process->Name) + 1);
                ReleaseProcess(Process);
            }
        } break;

        case __PROCESSMANAGER_GET_PROCESS_TICK: {
            Process_t* Process = AcquireProcess(IPC_GET_TYPED(Message, 1));
            clock_t    Tick    = 0;
            
            if (Process != NULL) {
                Tick = clock() - Process->StartedAt;
                ReleaseProcess(Process);
            }
            Handled = IpcReply(Message, &Tick, sizeof(clock_t));
        } break;

        case __PROCESSMANAGER_GET_ASSEMBLY_DIRECTORY: {
            UUId_t     ProcessHandle = IPC_GET_TYPED(Message, 2);
            void*      NullPtr       = NULL;
            Process_t* Process       = AcquireProcess(
                (ProcessHandle == UUID_INVALID) ? IPC_GET_TYPED(Message, 1) : ProcessHandle);
            
            if (Process == NULL) {
                Handled = IpcReply(Message, &NullPtr, sizeof(void*));
            }
            else {
                Handled = IpcReply(Message, (void*)MStringRaw(Process->AssemblyDirectory), MStringSize(Process->AssemblyDirectory) + 1);
                ReleaseProcess(Process);
            }
        } break;

        case __PROCESSMANAGER_GET_WORKING_DIRECTORY: {
            UUId_t     ProcessHandle = IPC_GET_TYPED(Message, 2);
            void*      NullPtr       = NULL;
            Process_t* Process       = AcquireProcess(
                (ProcessHandle == UUID_INVALID) ? IPC_GET_TYPED(Message, 1) : ProcessHandle);

            if (Process == NULL) {
                TRACE("proc_get_cwd => invalid proc");
                Handled = IpcReply(Message, &NullPtr, sizeof(void*));
            }
            else {
                TRACE("proc_get_cwd => %s", MStringRaw(Process->WorkingDirectory));
                Handled = IpcReply(Message, (void*)MStringRaw(Process->WorkingDirectory), MStringSize(Process->WorkingDirectory) + 1);
                ReleaseProcess(Process);
            }
        } break;

        case __PROCESSMANAGER_SET_WORKING_DIRECTORY: {
            Process_t*  Process = AcquireProcess(IPC_GET_TYPED(Message, 1));
            OsStatus_t  Result  = OsDoesNotExist;
            const char* Path    = IPC_GET_STRING(Message, 0);
            
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
            Handled = IpcReply(Message, &Result, sizeof(OsStatus_t));
        } break;

        case __PROCESSMANAGER_GET_LIBRARY_HANDLES: {
            Process_t* Process = AcquireProcess(IPC_GET_TYPED(Message, 1));
            Handle_t   LibraryList[PROCESS_MAXMODULES];
            memset(&LibraryList[0], 0, sizeof(LibraryList));

            if (Process != NULL) {
                GetProcessLibraryHandles(Process, LibraryList);
                ReleaseProcess(Process);
            }
            Handled = IpcReply(Message, &LibraryList[0], sizeof(LibraryList));
        } break;

        case __PROCESSMANAGER_GET_LIBRARY_ENTRIES: {
            Process_t* Process = AcquireProcess(IPC_GET_TYPED(Message, 1));
            Handle_t   LibraryList[PROCESS_MAXMODULES];
            memset(&LibraryList[0], 0, sizeof(LibraryList));

            if (Process != NULL) {
                GetProcessLibraryEntryPoints(Process, LibraryList);
                ReleaseProcess(Process);
            }
            Handled = IpcReply(Message, &LibraryList[0], sizeof(LibraryList));
        } break;

        case __PROCESSMANAGER_LOAD_LIBRARY: {
            Process_t*  Process = AcquireProcess(IPC_GET_TYPED(Message, 1));
            const char* Path    = IPC_GET_STRING(Message, 0);
            Handle_t    Handle  = HANDLE_INVALID;
            
            if (Process != NULL) {
                LoadProcessLibrary(Process, Path, &Handle);
                ReleaseProcess(Process);
            }
            Handled = IpcReply(Message, &Handle, sizeof(Handle_t));
        } break;

        case __PROCESSMANAGER_RESOLVE_FUNCTION: {
            Process_t*  Process  = AcquireProcess(IPC_GET_TYPED(Message, 1));
            Handle_t    Handle   = (Handle_t)IPC_GET_TYPED(Message, 2);
            const char* Function = IPC_GET_STRING(Message, 0);
            uintptr_t   Address  = 0;
            
            if (Process != NULL) {
                Address = ResolveProcessLibraryFunction(Process, Handle, Function);
                ReleaseProcess(Process);
            }
            Handled = IpcReply(Message, &Address, sizeof(uintptr_t));
        } break;

        case __PROCESSMANAGER_UNLOAD_LIBRARY: {
            Process_t* Process = AcquireProcess(IPC_GET_TYPED(Message, 1));
            OsStatus_t Result  = OsDoesNotExist;
            
            if (Process != NULL) {
                Result = UnloadProcessLibrary(Process, (Handle_t)IPC_GET_TYPED(Message, 2));
                ReleaseProcess(Process);
            }
            Handled = IpcReply(Message, &Result, sizeof(OsStatus_t));
        } break;

        case __PROCESSMANAGER_CRASH_REPORT: {
            // so far what do, we need to specify a report structure
            Process_t* Process      = AcquireProcess(IPC_GET_TYPED(Message, 1));
            Context_t* CrashContext = IPC_GET_UNTYPED(Message, 0);
            int        CrashReason  = (int)IPC_GET_TYPED(Message, 2);
            OsStatus_t Result       = OsDoesNotExist;
            
            if (Process != NULL) {
                Result = HandleProcessCrashReport(Process, CrashContext, CrashReason);
            }
            Handled = IpcReply(Message, &Result, sizeof(OsStatus_t));
        }
        
        default: {
            break;
        }
    }
    return Handled;
}
