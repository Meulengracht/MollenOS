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
#define __TRACE


#include <ddk/barrier.h>
#include <ddk/handle.h>
#include <ds/streambuffer.h>
#include <debug.h>
#include <futex.h>
#include <handle.h>
#include <handle_set.h>
#include <heap.h>
#include <ipc_context.h>
#include <memoryspace.h>
#include <memory_region.h>
#include <string.h>
#include <threading.h>

typedef struct IpcContext {
    UUId_t          Handle;
    UUId_t          CreatorThreadHandle;
    UUId_t          MemoryRegionHandle;
    streambuffer_t* KernelStream;
} IpcContext_t;

static inline MCoreThread_t*
GetContextThread(
    _In_ IpcContext_t* Context)
{
    return LookupHandleOfType(Context->CreatorThreadHandle, HandleTypeThread);
}

static void
IpcContextDestroy(
    _In_ void* Resource)
{
    IpcContext_t* Context = Resource;
    DestroyHandle(Context->MemoryRegionHandle);
    kfree(Context);
}

OsStatus_t
IpcContextCreate(
    _In_  size_t  Size,
    _Out_ UUId_t* HandleOut,
    _Out_ void**  UserContextOut)
{
    IpcContext_t* Context;
    OsStatus_t    Status;
    void*         KernelMapping;
    
    if (!HandleOut || !UserContextOut) {
        return OsInvalidParameters;
    }
    
    Context = kmalloc(sizeof(IpcContext_t));
    if (!Context) {
        return OsOutOfMemory;
    }
    
    Context->CreatorThreadHandle = GetCurrentThreadId();
    
    Status = MemoryRegionCreate(Size, Size, 0, &KernelMapping, UserContextOut,
        &Context->MemoryRegionHandle);
    if (Status != OsSuccess) {
        kfree(Context);
        return Status;
    }
    
    Context->Handle       = CreateHandle(HandleTypeIpcContext, IpcContextDestroy, Context);
    Context->KernelStream = (streambuffer_t*)KernelMapping;
    streambuffer_construct(Context->KernelStream, Size, 
        STREAMBUFFER_GLOBAL | STREAMBUFFER_MULTIPLE_WRITERS);
    
    *HandleOut = Context->Handle;
    return Status;
}

struct message_state {
    unsigned int base;
    unsigned int state;
};

static OsStatus_t
AllocateMessage(
    _In_  struct ipmsg_desc*    SourceMessage,
    _In_  size_t                Timeout,
    _In_  struct message_state* State,
    _Out_ IpcContext_t**        TargetContext)
{
    IpcContext_t* Context;
    size_t        BytesAvailable;
    size_t        BytesToAllocate = sizeof(struct ipmsg_resp) + SourceMessage->base->length;
    TRACE("[ipc] [allocate] %u/%u", SourceMessage->base->protocol, SourceMessage->base->action);
    
    if (SourceMessage->address->type == IPMSG_ADDRESS_HANDLE) {
        Context = LookupHandleOfType(SourceMessage->address->data.handle, HandleTypeIpcContext);
    }
    else {
        UUId_t     Handle;
        OsStatus_t Status = LookupHandleByPath(SourceMessage->address->data.path, &Handle);
        if (Status != OsSuccess) {
            ERROR("[ipc] [allocate] could not find target path %s", 
                SourceMessage->address->data.path);
            return Status;
        }
        
        Context = LookupHandleOfType(Handle, HandleTypeIpcContext);
    }
    
    if (!Context) {
        ERROR("[ipc] [allocate] could not find target handle %u", 
            SourceMessage->address->data.handle);
        return OsDoesNotExist;
    }
    
    BytesAvailable = streambuffer_write_packet_start(Context->KernelStream,
        BytesToAllocate, 0, &State->base, &State->state);
    if (!BytesAvailable) {
        ERROR("[ipc] [allocate] timeout allocating space for message");
        return OsTimeout;
    }
    
    *TargetContext = Context;
    return OsSuccess;
}

static OsStatus_t
MapUntypedParameter(
    _In_ struct ipmsg_param*  Parameter,
    _In_ SystemMemorySpace_t* TargetMemorySpace)
{
    VirtualAddress_t CopyAddress;
    size_t     OffsetInPage = Parameter->data.value % GetMemorySpacePageSize();
    OsStatus_t Status       = CloneMemorySpaceMapping(
        GetCurrentMemorySpace(), TargetMemorySpace,
        (VirtualAddress_t)Parameter->data.buffer, &CopyAddress, Parameter->length + OffsetInPage,
        MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_READONLY | MAPPING_PERSISTENT,
        MAPPING_VIRTUAL_PROCESS);
    if (Status != OsSuccess) {
        ERROR("[ipc] [map_untyped] Failed to clone ipc mapping");
        return Status;
    }
    
    // Update buffer pointer in untyped argument
    Parameter->data.buffer = (void*)(CopyAddress + OffsetInPage);
    smp_wmb();
    
    return OsSuccess;
}

static OsStatus_t
WriteMessage(
    _In_ IpcContext_t*         Context,
    _In_ struct ipmsg_desc*    Message,
    _In_ struct message_state* State)
{
    int i;
    
    TRACE("[ipc] [write] %u/%u", Message->base->protocol, Message->base->action);
    
    // Write all members in the order of ipmsg
    streambuffer_write_packet_data(Context->KernelStream, 
        Message->response, sizeof(struct ipmsg_resp), &State->state);
    
    // Fixup all SHM buffer values before writing the base message
    for (i = 0; i < Message->base->param_in; i++) {
        if (Message->base->params[i].type == IPMSG_PARAM_SHM) {
            MCoreThread_t* Thread = GetContextThread(Context);
            OsStatus_t     Status = MapUntypedParameter(&Message->base->params[i],
                Thread->MemorySpace);
            if (Status != OsSuccess) {
                // WHAT DO
                //CleanupMessage(Target, &IpcContext->Message);
                //atomic_store(&IpcContext->WriteSyncObject, 0);
                //(void)FutexWake(&IpcContext->WriteSyncObject, 1, 0);
                ERROR("[ipc] [initialize_message] failed to map parameter");
                return Status;
            }
        }
    }
    
    streambuffer_write_packet_data(Context->KernelStream, 
        Message->base, sizeof(struct ipmsg_base) + 
            (Message->base->param_in * sizeof(struct ipmsg_param)),
        &State->state);
    
    // Handle all the buffer/shm parameters
    for (i = 0; i < Message->base->param_in; i++) {
        if (Message->base->params[i].type == IPMSG_PARAM_BUFFER) {
            streambuffer_write_packet_data(Context->KernelStream, 
                Message->base->params[i].data.buffer,
                Message->base->params[i].length,
                &State->state);
        }
    }
    return OsSuccess;
}

static inline void
SendMessage(
    _In_ IpcContext_t*         Context,
    _In_ struct ipmsg_desc*    Message,
    _In_ struct message_state* State)
{
    TRACE("[ipc] [send] %u/%u", Message->base->protocol, Message->base->action);
    
    streambuffer_write_packet_end(Context->KernelStream, State->base,
        sizeof(struct ipmsg_resp) + Message->base->length);
    MarkHandle(Context->Handle, 0);
}

static inline void
CleanupMessage(
    _In_ struct ipmsg* Message)
{
    int i;
    TRACE("CleanupMessage(0x%llx)", Message);

    // Flush all the mappings granted in the argument phase
    for (i = 0; i < Message->base.param_in; i++) {
        if (Message->base.params[i].type == IPMSG_PARAM_SHM) {
            OsStatus_t Status = MemorySpaceUnmap(GetCurrentMemorySpace(),
                (VirtualAddress_t)Message->base.params[i].data.buffer,
                Message->base.params[i].length);
            if (Status != OsSuccess) {
                // LOG
            }
        }
    }
}

static void
WaitForMessageNotification(
    _In_ struct ipmsg_resp* Response,
    _In_ size_t             Timeout)
{
    if (Response->notify_method == IPMSG_NOTIFY_HANDLE_SET) {
        handle_event_t event;
        int            numberOfEvents;
        WaitForHandleSet(Response->notify_data.handle,
            &event, 1, Timeout, &numberOfEvents);
    }
}

static void
SendNotification(
    _In_ struct ipmsg_resp* Response)
{
    if (Response->notify_method == IPMSG_NOTIFY_HANDLE_SET) {
        MarkHandle(Response->notify_data.handle, 0);
    }
    else if (Response->notify_method == IPMSG_NOTIFY_SIGNAL) {
        SignalSend(Response->notify_data.handle, SIGIPC, Response->notify_context);
    }
    else if (Response->notify_method == IPMSG_NOTIFY_THREAD) {
        NOTIMPLEMENTED("[ipc] [send_notification] IPC_NOTIFY_METHOD_THREAD missing implementation");
    }
}

static OsStatus_t
WriteShortResponse(
    _In_ struct ipmsg_resp* Reply,
    _In_ OsStatus_t         Status)
{
    IpcResponsePayloadHeader_t Header = { Status };
    size_t                     BytesWritten;
    
    MemoryRegionWrite(Reply->dma_handle, Reply->dma_offset, &Header,
        sizeof(IpcResponsePayloadHeader_t), &BytesWritten);
    SendNotification(Reply);
    return OsSuccess;
}

static OsStatus_t
WriteFullResponse(
    _In_ struct ipmsg*      message,
    _In_ struct ipmsg_base* messageDescriptor)
{
    uint16_t offset = message->response.dma_offset;
    int      i;
    
    for (i = 0; i < message->base.param_out; i++) {
        struct ipmsg_param* param        = &messageDescriptor->params[i];
        size_t              bytesWritten = 0;
        
        if (param->type == IPMSG_PARAM_VALUE) {
            MemoryRegionWrite(message->response.dma_handle, offset,
                &param->data.value, param->length, &bytesWritten);
        }
        else if (param->type == IPMSG_PARAM_BUFFER) {
            MemoryRegionWrite(message->response.dma_handle, offset,
                param->data.buffer, param->length, &bytesWritten);
        }
        offset += message->base.params[message->base.param_in + i].length;
    }
    
    SendNotification(&message->response);
    return OsSuccess;
}

OsStatus_t
IpcContextSendMultiple(
    _In_ struct ipmsg_desc** Messages,
    _In_ int                 MessageCount,
    _In_ size_t              Timeout)
{
    struct message_state State;
    int                  i;
    TRACE("[ipc] [send] count %i, timeout %u", MessageCount, LODWORD(Timeout));
    
    if (!Messages || !MessageCount) {
        return OsInvalidParameters;
    }
    
    for (int i = 0; i < MessageCount; i++) {
        IpcContext_t* TargetContext;
        OsStatus_t    Status = AllocateMessage(Messages[i], Timeout, 
             &State, &TargetContext);
        if (Status != OsSuccess) {
            if (WriteShortResponse(Messages[i]->response, Status) != OsSuccess) {
                WARNING("[ipc] [send_multiple] failed to write response");
            }
        }
        WriteMessage(TargetContext, Messages[i], &State);
        SendMessage(TargetContext, Messages[i], &State);
    }
    
    // Iterate all messages again and wait for response
    for (i = 0; i < MessageCount; i++) {
        if (!(Messages[i]->base->flags & IPMSG_ASYNC)) {
            WaitForMessageNotification(Messages[i]->response, Timeout);
        }
    }
    return OsSuccess;
}

OsStatus_t
IpcContextRespondMultiple(
    _In_ struct ipmsg**      messages,
    _In_ struct ipmsg_base** messageDescriptors,
    _In_ int                 messageCount)
{
    int i;
    
    if (!messages || !messageDescriptors || !messageCount) {
        return OsInvalidParameters;
    }
    
    for (i = 0; i < messageCount; i++) {
        if (WriteFullResponse(messages[i], messageDescriptors[i]) != OsSuccess) {
            WARNING("[ipc] [respond] failed to write response");
        }
        CleanupMessage(messages[i]);
    }
    return OsSuccess;
}
