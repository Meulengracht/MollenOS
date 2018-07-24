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
#include <system/utils.h>
#include <threading.h>
#include <debug.h>
#include <pipe.h>

/* ScPipeOpen
 * Opens a new pipe for the calling Ash process and allows 
 * communication to this port from other processes */
OsStatus_t
ScPipeOpen(
    _In_ int            Port, 
    _In_ int            Type) {
    return PhoenixOpenAshPipe(PhoenixGetCurrentAsh(), Port, Type);
}

/* ScPipeClose
 * Closes an existing pipe on a given port and
 * shutdowns any communication on that port */
OsStatus_t
ScPipeClose(
    _In_ int            Port) {
    return PhoenixCloseAshPipe(PhoenixGetCurrentAsh(), Port);
}

/* ScPipeRead
 * Reads the requested number of bytes from the system-pipe. */
OsStatus_t
ScPipeRead(
    _In_ int            Port,
    _In_ uint8_t*       Container,
    _In_ size_t         Length)
{
    // Variables
    SystemPipe_t *Pipe  = NULL;

    // Sanitize parameters
    if (Length == 0) {
        return OsSuccess;
    }

    Pipe = PhoenixGetAshPipe(PhoenixGetCurrentAsh(), Port);
    if (Pipe == NULL) {
        ERROR("Trying to read from non-existing pipe %i", Port);
        return OsError;
    }

    ReadSystemPipe(Pipe, Container, Length);
    return OsSuccess;
}

/* ScPipeWrite
 * Writes the requested number of bytes to the system-pipe. */
OsStatus_t
ScPipeWrite(
    _In_ UUId_t         ProcessId,
    _In_ int            Port,
    _In_ uint8_t*       Message,
    _In_ size_t         Length)
{
    // Variables
    SystemPipe_t *Pipe      = NULL;

    // Sanitize parameters
    if (Message == NULL || Length == 0) {
        return OsError;
    }

    // Are we looking for a system out pipe? (std) then the
    // process id (target) will be set as invalid
    if (ProcessId == UUID_INVALID) {
        if (Port == PIPE_STDOUT || Port == PIPE_STDERR) {
            if (Port == PIPE_STDOUT) {
                WARNING(" > stdout");
                Pipe = LogPipeStdout();
            }
            else if (Port == PIPE_STDERR) {
                WARNING(" > stderr");
                Pipe = LogPipeStderr();
            }
        }
        else {
            ERROR("Invalid system pipe %i", Port);
            return OsError;
        }
    }
    else { Pipe = PhoenixGetAshPipe(PhoenixGetAsh(ProcessId), Port); }
    if (Pipe == NULL) {
        ERROR("Invalid pipe %i", Port);
        return OsError;
    }

    WriteSystemPipe(Pipe, Message, Length);
    return OsSuccess;
}

/* ScPipeReceive
 * Receives the requested number of bytes from the system-pipe. */
OsStatus_t
ScPipeReceive(
    _In_ UUId_t         ProcessId,
    _In_ int            Port,
    _In_ uint8_t*       Message,
    _In_ size_t         Length)
{
    // Variables
    SystemPipe_t *Pipe      = NULL;

    // Sanitize parameters
    if (Message == NULL || Length == 0 || ProcessId == UUID_INVALID) {
        ERROR("Invalid paramters for pipe-receive");
        return OsError;
    }

    Pipe = PhoenixGetAshPipe(PhoenixGetAsh(ProcessId), Port);
    if (Pipe == NULL) {
        ERROR("Invalid pipe %i", Port);
        return OsError;
    }

    ReadSystemPipe(Pipe, Message, Length);
    return OsSuccess;
}

/* ScRpcResponse
 * Waits for IPC RPC request to finish 
 * by polling the default pipe for a rpc-response */
OsStatus_t
ScRpcResponse(
    _In_ MRemoteCall_t* RemoteCall)
{
    // Variables
    SystemPipe_t *Pipe  = ThreadingGetCurrentThread(CpuGetCurrentId())->Pipe;
    assert(Pipe != NULL);
    assert(RemoteCall != NULL);
    assert(RemoteCall->Result.Length > 0);

    // Read up to <Length> bytes, this results in the next 1 .. Length
    // being read from the raw-pipe.
    ReadSystemPipe(Pipe, (uint8_t*)RemoteCall->Result.Data.Buffer, 
        RemoteCall->Result.Length);
    return OsSuccess;
}

/* ScRpcExecute
 * Executes an IPC RPC request to the given process and optionally waits for
 * a reply/response */
OsStatus_t
ScRpcExecute(
    _In_ MRemoteCall_t* RemoteCall,
    _In_ int            Async)
{
    // Variables
    SystemPipeUserState_t State;
    MCoreThread_t *Thread;
    SystemPipe_t *Pipe;
    MCoreAsh_t *Ash;
    size_t TotalLength  = sizeof(MRemoteCall_t);
    int i               = 0;

    // Start out by resolving both the process and pipe
    Ash     = PhoenixGetAsh(RemoteCall->To.Process);
    Pipe    = PhoenixGetAshPipe(Ash, RemoteCall->To.Port);

    // Sanitize the lookups
    if (Ash == NULL || Pipe == NULL) {
        if (Ash == NULL) {
            ERROR("Target 0x%x did not exist", RemoteCall->To.Process);
        }
        else {
            ERROR("Port %u did not exist in target 0x%x",
                RemoteCall->To.Port, RemoteCall->To.Process);
        }
        return OsError;
    }

    // Trace
    TRACE("ScRpcExecute(Target %s, Message %i, Async %i)", 
        MStringRaw(Ash->Name), RemoteCall->Function, Async);
    
    // Install Sender
    Thread = ThreadingGetCurrentThread(CpuGetCurrentId());
    RemoteCall->From.Process    = Thread->AshId;
    RemoteCall->From.Thread     = Thread->Id;
    RemoteCall->From.Port       = -1;

    // Calculate how much data to be comitted
    for (i = 0; i < IPC_MAX_ARGUMENTS; i++) {
        if (RemoteCall->Arguments[i].Type == ARGUMENT_BUFFER) {
            TotalLength += RemoteCall->Arguments[i].Length;
        }
    }

    // Setup producer access
    AcquireSystemPipeProduction(Pipe, TotalLength, &State);
    WriteSystemPipeProduction(&State, (const uint8_t*)RemoteCall, sizeof(MRemoteCall_t));
    for (i = 0; i < IPC_MAX_ARGUMENTS; i++) {
        if (RemoteCall->Arguments[i].Type == ARGUMENT_BUFFER) {
            WriteSystemPipeProduction(&State, 
                (const uint8_t*)RemoteCall->Arguments[i].Data.Buffer,
                RemoteCall->Arguments[i].Length);
        }
    }

    // Async request? Because if yes, don't
    // wait for response
    if (Async) {
        return OsSuccess;
    }
    return ScRpcResponse(RemoteCall);
}

/* ScRpcListen
 * Listens for a new rpc-message on the default rpc-pipe. */
OsStatus_t
ScRpcListen(
    _In_ int            Port,
    _In_ MRemoteCall_t* RemoteCall,
    _In_ uint8_t*       ArgumentBuffer)
{
    // Variables
    SystemPipeUserState_t State;
    uint8_t *BufferPointer = ArgumentBuffer;
    SystemPipe_t *Pipe;
    MCoreAsh_t *Ash;
    size_t Length;
    int i;

    // Trace
    TRACE("%s: ScRpcListen(Port %i)", MStringRaw(PhoenixGetCurrentAsh()->Name), Port);
    
    // Start out by resolving both the
    // process and pipe
    Ash     = PhoenixGetCurrentAsh();
    Pipe    = PhoenixGetAshPipe(Ash, Port);

    // Start consuming
    AcquireSystemPipeConsumption(Pipe, &Length, &State);
    ReadSystemPipeConsumption(&State, (uint8_t*)RemoteCall, sizeof(MRemoteCall_t));
    for (i = 0; i < IPC_MAX_ARGUMENTS; i++) {
        if (RemoteCall->Arguments[i].Type == ARGUMENT_BUFFER) {
            RemoteCall->Arguments[i].Data.Buffer = (const void*)BufferPointer;
            ReadSystemPipeConsumption(&State, BufferPointer, RemoteCall->Arguments[i].Length);
            BufferPointer += RemoteCall->Arguments[i].Length;
        }
    }
    FinalizeSystemPipeConsumption(Pipe, &State);
    return OsSuccess;
}

/* ScRpcRespond
 * A wrapper for sending an RPC response to the calling thread. Each thread has each it's response
 * channel to avoid any concurrency issues. */
OsStatus_t
ScRpcRespond(
    _In_ MRemoteCallAddress_t*  RemoteAddress,
    _In_ const uint8_t*         Buffer, 
    _In_ size_t                 Length)
{
    // Variables
    MCoreThread_t *Thread   = ThreadingGetThread(RemoteAddress->Thread);
    SystemPipe_t *Pipe      = NULL;

    // Sanitize thread still exists
    if (Thread != NULL) {
        Pipe = Thread->Pipe;
    }
    if (Pipe == NULL) {
        ERROR("Thread %u did not exist", RemoteAddress->Thread);
        return OsError;
    }

    WriteSystemPipe(Pipe, Buffer, Length);
    return OsSuccess;
}
