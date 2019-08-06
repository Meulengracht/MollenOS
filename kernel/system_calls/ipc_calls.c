/**
 *  MollenOS
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
 * System call interface - IPC implementation
 *
 */

#define __MODULE "SCIF"
#define __TRACE

#include <arch/utils.h>
#include <ddk/ipc/ipc.h>
#include <debug.h>
#include <futex.h>
#include <handle.h>
#include <os/input.h>
#include <machine.h>
#include <modules/manager.h>
#include <modules/module.h>
#include <threading.h>

static inline void
CleanupMessage(
    _In_  MCoreThread_t* Target,
    _In_  IpcMessage_t*  Message)
{
    int i;

    // Flush all the mappings granted in the argument phase
    for (i = 0; i < IPC_MAX_ARGUMENTS; i++) {
        if (Message->UntypedArguments[i].Length > IPC_UNTYPED_THRESHOLD) {
            OsStatus_t Status = RemoveMemorySpaceMapping(Target->MemorySpace,
                Message->UntypedArguments[i].Buffer,
                Message->UntypedArguments[i].Length);
            if (Status != OsSuccess) {
                // TODO: Ehm what
                ERROR("Failed to clone ipc argument that was longer than 512 bytes");
            }
        }
    }
}

OsStatus_t
ScIpcReply(
    _In_  void*  Buffer,
    _In_  size_t Length)
{
    MCoreThread_t* Current         = GetCurrentThreadForCore(ArchGetProcessorCoreId());
    IpcArena_t*    CurrentIpcArena = Current->IpcArena;
    MCoreThread_t* Target          = LookupHandle(CurrentIpcArena->SenderHandle);
    IpcArena_t*    TargetIpcArena  = Target->IpcArena;

    memcpy(&TargetIpcArena->Buffer[IPC_RESPONSE_LOCATION], 
        Buffer, MIN(IPC_RESPONSE_MAX_SIZE, Length));

    atomic_store(&TargetIpcArena->ResponseSyncObject, 1);
    (void)FutexWake(&TargetIpcArena->ResponseSyncObject, 1, 0);
    
    CleanupMessage(Current, &CurrentIpcArena->Message);
    return OsSuccess;
}

OsStatus_t
ScIpcListen(
    _In_  size_t         Timeout,
    _Out_ IpcMessage_t** MessageOut)
{
    MCoreThread_t* Current  = GetCurrentThreadForCore(ArchGetProcessorCoreId());
    IpcArena_t*    IpcArena = Current->IpcArena;
    int            SyncValue;

    // Clear the WriteSyncObject
    atomic_store(&IpcArena->WriteSyncObject, 0);
    (void)FutexWake(&IpcArena->WriteSyncObject, 1, 0);

    // Wait for response by 'polling' the value
    SyncValue = atomic_exchange(&IpcArena->ReadSyncObject, 0);
    while (!SyncValue) {
        if (FutexWait(&IpcArena->ReadSyncObject, SyncValue, 0, Timeout) == OsTimeout) {
            return OsTimeout;
        }
        SyncValue = atomic_exchange(&IpcArena->ReadSyncObject, 0);
    }

    *MessageOut = &IpcArena->Message;
    return OsSuccess;
}

OsStatus_t
ScIpcReplyAndListen(
    _In_  void*          Buffer,
    _In_  size_t         Length,
    _In_  size_t         Timeout,
    _Out_ IpcMessage_t** MessageOut)
{
    OsStatus_t Status = ScIpcReply(Buffer, Length);
    if (Status != OsSuccess) {
        return Status;
    }
    return ScIpcListen(Timeout, MessageOut);
}

OsStatus_t
ScIpcGetResponse(
    _In_ size_t Timeout,
    _In_ void** BufferOut)
{
    MCoreThread_t* Current  = GetCurrentThreadForCore(ArchGetProcessorCoreId());
    IpcArena_t*    IpcArena = Current->IpcArena;
    int            SyncValue;
    
    // Wait for response by 'polling' the value
    SyncValue = atomic_exchange(&IpcArena->ResponseSyncObject, 0);
    while (!SyncValue) {
        if (FutexWait(&IpcArena->ResponseSyncObject, SyncValue, 0, Timeout) == OsTimeout) {
            return OsTimeout;
        }
        SyncValue = atomic_exchange(&IpcArena->ResponseSyncObject, 0);
    }
    
    *BufferOut = &IpcArena->Buffer[IPC_RESPONSE_LOCATION];
    return OsSuccess;
}

OsStatus_t
ScIpcInvoke(
    _In_  UUId_t        TargetHandle,
    _In_  IpcMessage_t* Message,
    _In_  unsigned int  Flags,
    _In_  size_t        Timeout)
{
    MCoreThread_t* Target      = LookupHandle(TargetHandle);
    size_t         BufferIndex = 0;
    IpcArena_t*    ResponseIpcArena;
    IpcArena_t*    IpcArena;
    int            SyncValue;
    int            i;
    if (!Target) {
        return OsDoesNotExist;
    }
    
    IpcArena  = Target->IpcArena
    SyncValue = atomic_exchange(&IpcArena->WriteSyncObject, 1);
    while (SyncValue) {
        if (FutexWait(&IpcArena->WriteSyncObject, SyncValue, 0, Timeout) == OsTimeout) {
            return OsTimeout;
        }
        SyncValue = atomic_exchange(&IpcArena->WriteSyncObject, 1);
    }
    
    IpcArena->SenderHandle = GetCurrentThreadId();
    for (i = 0; i < IPC_MAX_ARGUMENTS; i++) {
        // Handle typed argument
        IpcArena->Message.TypedArguments[i]          = Message->TypedArguments[i];
        IpcArena->Message.UntypedArguments[i].Length = Message->UntypedArguments[i].Length;
        
        // Handle the untyped, a bit more tricky. If the argument is larger than 512
        // bytes, we will do a mapping clone instead of just copying data into sender.
        if (Message->UntypedArguments[i].Length) {
            // Events that don't have a response do not support longer arguments than 512 bytes.
            if (Message->UntypedArguments[i].Length > IPC_UNTYPED_THRESHOLD && 
                !(Flags & IPC_NO_RESPONSE)) {
                OsStatus_t Status = CloneMemorySpaceMapping(
                    GetCurrentMemorySpace(), Target->MemorySpace,
                    Message->UntypedArguments[i].Buffer, 
                    (VirtualAddress_t*)&IpcArena->Message.UntypedArguments[i].Buffer,
                    Message->UntypedArguments[i].Length,
                    MAPPING_USERSPACE | MAPPING_READONLY,
                    MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_PROCESS);
                if (Status != OsSuccess) {
                    // TODO: Ehm what
                    ERROR("Failed to clone ipc argument that was longer than 512 bytes");
                }
            }
            else {
                // TODO: support longer but untill we run out of space?
                if (Message->UntypedArguments[i].Length > IPC_UNTYPED_THRESHOLD) {
                    WARNING("Event with more than IPC_UNTYPED_THRESHOLD bytes of data for an argument, argument was truncated");
                }
                memcpy(&IpcArena->Buffer[BufferIndex], 
                    Message->UntypedArguments[i].Buffer,
                    MIN(IPC_UNTYPED_THRESHOLD, Message->UntypedArguments[i].Length));
                IpcArena->Message.UntypedArguments[i].Buffer = (void*)BufferIndex;
                BufferIndex += MIN(IPC_UNTYPED_THRESHOLD, Message->UntypedArguments[i].Length);
            }
        }
    }
    
    atomic_store(&IpcArena->ResponseSyncObject, 0);
    atomic_store(&IpcArena->ReadSyncObject, 1);
    (void)FutexWake(&IpcArena->ReadSyncObject, 1, 0);
    if (Flags & (IPC_ASYNCHRONOUS | IPC_NO_RESPONSE)) {
        return OsSuccess;
    }
    return ScIpcGetResponse(Timeout, BufferOut);
}
