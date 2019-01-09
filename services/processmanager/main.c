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
#define __TRACE

#include <os/service.h>
#include <os/process.h>
#include <os/utils.h>
#include <string.h>
#include <stdio.h>
#include "process.h"

OsStatus_t
OnLoad(void)
{
    // Register us with server manager
	return RegisterService(__PROCESSMANAGER_TARGET);
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
    TRACE("Processmanager.OnEvent(%i)", Message->Function);

    switch (Message->Function) {
        case __PROCESSMANAGER_CREATE_PROCESS: {
            const char*                  Path               = RPCGetStringArgument(RPC, 0);
            ProcessStartupInformation_t* StartupInformation = RPCGetPointerArgument(RPC, 1);
            UUId_t                       Handle;
            OsStatus_t                   Result;

            Result = CreateProcess(Path, StartupInformation, 
                RPCGetPointerArgument(RPC, 2), RPC->Arguments[2].Length, 
                RPCGetStringArgument(RPC, 3), RPC->Arguments[3].Length, &Handle);
            if (Result != OsSuccess) {
                Handle = UUID_INVALID;
            }
            Handled = RPCRespond(&RPC->From, (const void*)&Handle, sizeof(UUId_t));
        } break;

        case __PROCESSMANAGER_JOIN_PROCESS: {
            JoinProcessPackage_t Package;
            UUId_t               Handle  = RPC->Arguments[0].Data.Value;
            size_t               Timeout = RPC->Arguments[1].Data.Value;
            
            Handled = RPCRespond(&RPC->From, (const void*)&Package, sizeof(JoinProcessPackage_t));
        } break;

        case __PROCESSMANAGER_KILL_PROCESS: {
            UUId_t     Handle = RPC->Arguments[0].Data.Value;
            OsStatus_t Result;


            Handled = RPCRespond(&RPC->From, (const void*)&Result, sizeof(OsStatus_t));
        } break;

        case __PROCESSMANAGER_TERMINATE_PROCESS: {
            UUId_t     Handle   = RPC->From.Process;
            int        ExitCode = (int)RPC->Arguments[0].Data.Value;
            OsStatus_t Result;

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
            Process_t* Process = GetProcess(RPC->From.Process);
            void*      NullPtr = NULL;
            if (Process == NULL) {
                Handled = RPCRespond(&RPC->From, (const void*)&NullPtr, sizeof(void*));
            }
            Handled = RPCRespond(&RPC->From, (const void*)Process->Arguments, Process->ArgumentsLength);
        } break;

        case __PROCESSMANAGER_GET_INHERIT_BLOCK: {
            Process_t* Process = GetProcess(RPC->From.Process);
            void*      NullPtr = NULL;
            if (Process == NULL || Process->InheritationBlockLength == 0) {
                Handled = RPCRespond(&RPC->From, (const void*)&NullPtr, sizeof(void*));
            }
            Handled = RPCRespond(&RPC->From, (const void*)Process->InheritationBlock, Process->InheritationBlockLength);
        } break;

        case __PROCESSMANAGER_GET_PROCESS_NAME: {
            Process_t* Process = GetProcess(RPC->From.Process);
            void*      NullPtr = NULL;
            if (Process == NULL) {
                Handled = RPCRespond(&RPC->From, (const void*)&NullPtr, sizeof(void*));
            }
            Handled = RPCRespond(&RPC->From, (const void*)MStringRaw(Process->Name), MStringSize(Process->Name) + 1);
        } break;

        case __PROCESSMANAGER_GET_PROCESS_TICK: {
            Process_t* Process = GetProcess(RPC->From.Process);
            clock_t    Tick    = 0;
            if (Process != NULL) {
                Tick = Process->StartedAt;
            }
            Handled = RPCRespond(&RPC->From, (const void*)&Tick, sizeof(clock_t));
        } break;

        case __PROCESSMANAGER_GET_ASSEMBLY_DIRECTORY: {
            Process_t* Process = GetProcess(RPC->From.Process);
            void*      NullPtr = NULL;
            if (Process == NULL) {
                Handled = RPCRespond(&RPC->From, (const void*)&NullPtr, sizeof(void*));
            }
            Handled = RPCRespond(&RPC->From, (const void*)MStringRaw(Process->AssemblyDirectory), MStringSize(Process->AssemblyDirectory) + 1);
        } break;

        case __PROCESSMANAGER_GET_WORKING_DIRECTORY: {
            Process_t* Process = GetProcess(RPC->From.Process);
            void*      NullPtr = NULL;
            if (Process == NULL) {
                Handled = RPCRespond(&RPC->From, (const void*)&NullPtr, sizeof(void*));
            }
            Handled = RPCRespond(&RPC->From, (const void*)MStringRaw(Process->WorkingDirectory), MStringSize(Process->WorkingDirectory) + 1);
        } break;

        case __PROCESSMANAGER_SET_WORKING_DIRECTORY: {
            UUId_t     Handle = RPC->From.Process;
            OsStatus_t Result;

            Handled = RPCRespond(&RPC->From, (const void*)&Result, sizeof(OsStatus_t));
        } break;

        case __PROCESSMANAGER_GET_LIBRARY_HANDLES: {
            UUId_t Handle = RPC->From.Process;

        } break;

        case __PROCESSMANAGER_GET_LIBRARY_ENTRIES: {
            UUId_t Handle = RPC->From.Process;

        } break;

        case __PROCESSMANAGER_LOAD_LIBRARY: {
            UUId_t Handle = RPC->From.Process;

        } break;

        case __PROCESSMANAGER_RESOLVE_FUNCTION: {
            UUId_t Handle = RPC->From.Process;

        } break;

        case __PROCESSMANAGER_UNLOAD_LIBRARY: {
            UUId_t     Handle = RPC->From.Process;
            OsStatus_t Result;

            Handled = RPCRespond(&RPC->From, (const void*)&Result, sizeof(OsStatus_t));
        } break;
        
        default: {
            break;
        }
    }
    return Handled;
}
