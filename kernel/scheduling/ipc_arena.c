/**
 * MollenOS
 *
 * Copyright 2020, Philip Meulengracht
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
 * IP-Communication
 * - Implementation of inter-thread communication. 
 */

#include <ddk/barrier.h>
#include <debug.h>
#include <handle.h>
#include <heap.h>
#include <ipc_arena.h>
#include <memoryspace.h>
#include <semaphore.h>
#include <string.h>
#include <threading.h>

typedef struct IpcArena {
    UUId_t        CreatorThreadHandle;
    Semaphore_t   WriterSyncObject;
    Semaphore_t   ReaderSyncObject;
    size_t        ArenaSize;
    size_t        MessageMaxSize;
    void*         KernelArena;
    void*         UserArena;
    IpcMessage_t* Messages;
} IpcArena_t;

static inline MCoreThread_t*
GetArenaThread(
    _In_ IpcArena_t* Arena)
{
    return LookupHandleOfType(Arena->CreatorThreadHandle, HandleTypeThread);
}

static void
IpcArenaDestroy(
    _In_ void* Resource)
{
    IpcArena_t*          Arena   = Resource;
    SystemMemorySpace_t* Current = GetCurrentMemorySpace();
    
    SemaphoreDestruct(&Arena->WriterSyncObject);
    SemaphoreDestruct(&Arena->ReaderSyncObject);
    MemorySpaceUnmap(Current, (uintptr_t)Arena->UserArena, Arena->ArenaSize);
    MemorySpaceUnmap(Current, (uintptr_t)Arena->KernelArena, Arena->ArenaSize);
    kfree(Arena);
}

OsStatus_t
IpcArenaCreate(
    _In_  int     MessageCount,
    _In_  size_t  MessageSize,
    _Out_ UUId_t* HandleOut,
    _Out_ void**  UserArenaOut)
{
    SystemMemorySpace_t* Current;
    IpcArena_t*          Arena;
    OsStatus_t           Status;
    uintptr_t            PhysicalAddress;
    size_t               ArenaSize = MessageCount * MessageSize;
    
    if (!HandleOut || !UserArenaOut) {
        return OsInvalidParameters;
    }
    
    Arena = kmalloc(sizeof(IpcArena_t));
    if (!Arena) {
        return OsOutOfMemory;
    }
    
    // The writer semaphore is the number of empty spots, while the reader semaphore
    // is the number of messages in in the arena
    SemaphoreConstruct(&Arena->WriterSyncObject, MessageCount, MessageCount);
    SemaphoreConstruct(&Arena->ReaderSyncObject, 0, MessageCount);
    Arena->ArenaSize      = ArenaSize;
    Arena->MessageMaxSize = MessageSize;
    
    Current = GetCurrentMemorySpace();
    // Create a kernel mapping that can be accessed by everyone in these
    // system calls, and we create a user mapping in the caller's space.
    // We mark the user-mapping as persistant so when unfreeing the kernel space
    // the physical memory is freed only.
    Status = MemorySpaceMap(Current, (uintptr_t*)&Arena->KernelArena, &PhysicalAddress,
        ArenaSize, MAPPING_USERSPACE | MAPPING_COMMIT, MAPPING_VIRTUAL_GLOBAL);
    if (Status != OsSuccess) {
        kfree(Arena);
        return Status;
    }
    
    Status = MemorySpaceMap(Current, (uintptr_t*)&Arena->UserArena, &PhysicalAddress,
        ArenaSize, MAPPING_USERSPACE | MAPPING_COMMIT | MAPPING_PERSISTENT,
        MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_PROCESS);
    if (Status != OsSuccess) {
        MemorySpaceUnmap(Current, (VirtualAddress_t)Arena->KernelArena, ArenaSize);
        kfree(Arena);
        return Status;
    }
    
    *HandleOut    = CreateHandle(HandleTypeArena, IpcArenaDestroy, Arena);
    *UserArenaOut = Arena->UserArena;
    return Status;
}

static OsStatus_t
AllocateMessage(
    _In_  IpcMessage_t*  SourceMessage,
    _In_  size_t         Timeout,
    _Out_ IpcArena_t**   TargetArena,
    _Out_ IpcMessage_t** TargetMessage)
{
    IpcArena_t* Arena = LookupHandleOfType(SourceMessage->Arena, HandleTypeArena);
    OsStatus_t  Status;
    if (!Arena) {
        return OsDoesNotExist;
    }
    
    Status = SemaphoreWait(&Arena->WriterSyncObject, Timeout);
    if (Status != OsSuccess) {
        return Status;
    }
    
    // get first free index
    
    *TargetArena   = Arena;
    *TargetMessage = &Arena->Messages[0];
    return OsSuccess;
}

static OsStatus_t
MapUntypedParameter(
    _In_ IpcUntypedArgument_t* Parameter,
    _In_ SystemMemorySpace_t*  TargetMemorySpace)
{
    VirtualAddress_t CopyAddress;
    size_t     OffsetInPage = ((uintptr_t)Parameter->Buffer % GetMemorySpacePageSize());
    size_t     Length       = Parameter->Length & IPC_ARGUMENT_LENGTH_MASK;
    OsStatus_t Status       = CloneMemorySpaceMapping(
        GetCurrentMemorySpace(), TargetMemorySpace,
        (VirtualAddress_t)Parameter->Buffer, &CopyAddress, Length + OffsetInPage,
        MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_READONLY | MAPPING_PERSISTENT,
        MAPPING_VIRTUAL_PROCESS);
    if (Status != OsSuccess) {
        ERROR("[ipc] [map_untyped] Failed to clone ipc mapping");
        return Status;
    }
    
    // Update buffer pointer in untyped argument
    Parameter->Buffer = (void*)(CopyAddress + OffsetInPage);
    smp_wmb();
    
    return OsSuccess;
}

static OsStatus_t
InitializeMessage(
    _In_ IpcArena_t*   TargetArena,
    _In_ IpcMessage_t* SourceMessage,
    _In_ IpcMessage_t* TargetMessage)
{
    uint8_t* ParameterSpace = ((uint8_t*)TargetMessage + sizeof(IpcMessage_t));
    size_t   ParameterIndex = 0;
    int      i;
    
    memcpy(TargetMessage, SourceMessage, sizeof(IpcMessage_t));
    TargetMessage->Length = sizeof(IpcMessage_t);
    smp_wmb();
    
    for (i = 0; i < IPC_MAX_ARGUMENTS; i++) {
        // Handle the untyped, a bit more tricky. If the argument is larger than 512
        // bytes, we will do a mapping clone instead of just copying data into sender.
        if (TargetMessage->UntypedArguments[i].Length) {
            // Events that don't have a response do not support longer arguments than 512 bytes.
            if (TargetMessage->UntypedArguments[i].Length & IPC_ARGUMENT_MAPPED) {
                MCoreThread_t* Thread = GetArenaThread(TargetArena);
                OsStatus_t     Status = MapUntypedParameter(&TargetMessage->UntypedArguments[i],
                    Thread->MemorySpace);
                if (Status != OsSuccess) {
                    // WHAT DO
                    //CleanupMessage(Target, &IpcArena->Message);
                    //atomic_store(&IpcArena->WriteSyncObject, 0);
                    //(void)FutexWake(&IpcArena->WriteSyncObject, 1, 0);
                    ERROR("[ipc] [initialize_message] failed to map parameter");
                    return Status;
                }
            }
            else {
                size_t BytesAvailable = (TargetArena->MessageMaxSize 
                    + sizeof(IpcMessage_t)) - TargetMessage->Length;
                size_t ClampedLength  = MIN(BytesAvailable, IPC_GET_LENGTH(TargetMessage, i));
                if (!ClampedLength) {
                    return OsIncomplete;
                }
                
                if (TargetMessage->UntypedArguments[i].Length > ClampedLength) {
                    WARNING("[ipc] [initialize_message] Event with more than %" PRIuIN " bytes of data for an argument",
                        ClampedLength);
                }
                
                memcpy(&ParameterSpace[ParameterIndex], TargetMessage->UntypedArguments[i].Buffer, ClampedLength);
                TargetMessage->UntypedArguments[i].Buffer = (void*)ParameterIndex;
                TargetMessage->UntypedArguments[i].Length = ClampedLength;
                smp_wmb();
                
                ParameterIndex        += ClampedLength;
                TargetMessage->Length += ClampedLength;
            }
        }
    }
    return OsSuccess;
}

static inline void
CleanupMessage(
    _In_ IpcArena_t*   Arena,
    _In_ IpcMessage_t* Message)
{
    int i;
    TRACE("CleanupMessage(%u, 0x%llx)", Arena->CreatorThreadHandle, Message);

    // Flush all the mappings granted in the argument phase
    for (i = 0; i < IPC_MAX_ARGUMENTS; i++) {
        if (Message->UntypedArguments[i].Length & IPC_ARGUMENT_MAPPED) {
            OsStatus_t Status = MemorySpaceUnmap(GetCurrentMemorySpace(),
                (VirtualAddress_t)Message->UntypedArguments[i].Buffer,
                IPC_GET_LENGTH(Message, i));
            if (Status != OsSuccess) {
                // LOG
            }
        }
    }
    
    // Free the message in the bitmap
}

static void
WaitForMessageNotification(
    _In_ IpcResponse_t* Response)
{
    if (Response->NotifyMethod == IPC_NOTIFY_METHOD_HANDLE) {
        WaitForHandleSet(Response->NotifyData.Notify.Handle,
            NULL, 1, 0 /* Timeout */, NULL);
    }
}

static void
SendNotification(
    _In_ IpcArena_t*    Arena,
    _In_ IpcResponse_t* Response)
{
    if (Response->NotifyMethod == IPC_NOTIFY_METHOD_HANDLE) {
        // MarkHandle(Response->NotifyData.Notify.Handle, Response->NotifyData.Context);
    }
    else if (Response->NotifyMethod == IPC_NOTIFY_METHOD_SIGNAL) {
        SignalSend(Arena->CreatorThreadHandle, 0/* SIGIPC */, Response->NotifyData.Context);
    }
    else if (Response->NotifyMethod == IPC_NOTIFY_METHOD_THREAD) {
        // TODO
    }
}

static OsStatus_t
WriteShortResponse(
    _In_ IpcResponse_t* Reply,
    _In_ OsStatus_t     Status)
{
    IpcArena_t*                 Arena = LookupHandleOfType(Reply->Arena, HandleTypeArena);
    IpcResponsePayloadHeader_t* Header;
    if (!Arena) {
        return OsDoesNotExist;
    }
    
    Header = (IpcResponsePayloadHeader_t*)((uint8_t*)Arena->KernelArena + Reply->ArenaOffset);
    Header->Status = Status;
    SendNotification(Arena, Reply);
    return OsSuccess;
}


static OsStatus_t
WriteFullResponse(
    _In_ IpcResponse_t* Reply,
    _In_ const void*    Payload,
    _In_ size_t         Length)
{
    IpcArena_t* Arena = LookupHandleOfType(Reply->Arena, HandleTypeArena);
    if (!Arena) {
        return OsDoesNotExist;
    }

    if (Payload && Length) {
        memcpy(((uint8_t*)Arena->KernelArena + Reply->ArenaOffset),
            Payload, Length);
    }
    SendNotification(Arena, Reply);
    return OsSuccess;
}

OsStatus_t
IpcArenaSendMultiple(
    _In_ IpcMessage_t** Messages,
    _In_ int            MessageCount,
    _In_ size_t         Timeout)
{
    OsStatus_t Status;
    int        i;
    
    // Prepare all messages and send them first
    Status = IpcArenaSendMultipleAsync(Messages, MessageCount, Timeout);
    if (Status != OsSuccess) {
        // LOG
    }
    
    // Iterate all messages again and wait for response
    for (i = 0; i < MessageCount; i++) {
        WaitForMessageNotification(&Messages[i]->Response);
    }
    return OsSuccess;
}

OsStatus_t
IpcArenaSendMultipleAsync(
    _In_ IpcMessage_t** Messages,
    _In_ int            MessageCount,
    _In_ size_t         Timeout)
{
    for (int i = 0; i < MessageCount; i++) {
        IpcArena_t*   TargetArena;
        IpcMessage_t* TargetMessage;
        OsStatus_t    Status = AllocateMessage(Messages[i], Timeout, 
            &TargetArena, &TargetMessage);
        if (Status != OsSuccess) {
            if (WriteShortResponse(&Messages[i]->Response, Status) != OsSuccess) {
                // LOG THIS
                WARNING("[ipc] [send_multiple] failed to write response");
            }
        }
        InitializeMessage(TargetArena, Messages[i], TargetMessage);
        SemaphoreSignal(&TargetArena->ReaderSyncObject, 1);
    }
    return OsSuccess;
}

OsStatus_t
IpcArenaRespondMultiple(
    _In_ IpcMessage_t** Replies,
    _In_ void**         ReplyBuffers,
    _In_ size_t*        ReplyLengths,
    _In_ int            ReplyCount)
{
    int i;
    
    if (!Replies || !ReplyBuffers || !ReplyLengths || !ReplyCount) {
        return OsInvalidParameters;
    }
    
    for (i = 0; i < ReplyCount; i++) {
        IpcArena_t* Arena = LookupHandleOfType(Replies[i]->Arena, HandleTypeArena);
        if (WriteFullResponse(&Replies[i]->Response, ReplyBuffers[i], ReplyLengths[i]) != OsSuccess) {
            WARNING("[ipc] [respond] failed to write response");
        }
        CleanupMessage(Arena, Replies[i]);
    }
    return OsSuccess;
}

OsStatus_t
IpcArenaResponseMultipleAndWait(
    _In_ IpcMessage_t** Replies,
    _In_ void**         ReplyBuffers,
    _In_ size_t*        ReplyLengths,
    _In_ int            ReplyCount,
    _In_ UUId_t         ArenaHandle,
    _In_ IpcMessage_t** Messages,
    _In_ int*           MessageCount,
    _In_ size_t         Timeout)
{
    IpcArena_t* Arena;
    OsStatus_t  Status;
    int         MaxMessageCount = *MessageCount;
    int         i;
    
    if (!Messages || !MessageCount) {
        return OsInvalidParameters;
    }
    
    if (ReplyCount > 0) {
        Status = IpcArenaRespondMultiple(Replies, ReplyBuffers,
            ReplyLengths, ReplyCount);
        if (Status != OsSuccess) {
            *MessageCount = 0;
            return Status;
        }
    }
    
    Arena = LookupHandleOfType(ArenaHandle, HandleTypeArena);
    if (!Arena) {
        *MessageCount = 0;
        return OsDoesNotExist;
    }
    
    // Wait for messages
    for (i = 0; i < MaxMessageCount; i++) {
        Status = SemaphoreWait(&Arena->ReaderSyncObject, Timeout);
        if (Status != OsSuccess) {
            break;
        }
        Messages[i] = NULL; // GetAllocatedIndex;
    }
    *MessageCount = i;
    return OsSuccess;
}
