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

//#define __TRACE

#include <ddk/barrier.h>
#include <ds/streambuffer.h>
#include <debug.h>
#include <handle.h>
#include <handle_set.h>
#include <heap.h>
#include <ioset.h>
#include <ipc_context.h>
#include <memoryspace.h>
#include <memory_region.h>
#include <threading.h>

typedef struct IpcContext {
    UUId_t          Handle;
    UUId_t          CreatorThreadHandle;
    UUId_t          MemoryRegionHandle;
    streambuffer_t* KernelStream;
} IpcContext_t;

static inline MCoreThread_t*
GetContextThread(
    _In_ IpcContext_t* context)
{
    return LookupHandleOfType(context->CreatorThreadHandle, HandleTypeThread);
}

static void
IpcContextDestroy(
    _In_ void* resource)
{
    IpcContext_t* context = resource;
    DestroyHandle(context->MemoryRegionHandle);
    kfree(context);
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
    streambuffer_construct(Context->KernelStream, Size - sizeof(streambuffer_t), 
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
    _In_  struct ipmsg_header*   message,
    _In_  size_t                 timeout,
    _In_  struct message_state*  state,
    _Out_ IpcContext_t**         targetContext)
{
    IpcContext_t* Context;
    size_t        BytesAvailable;
    size_t        BytesToAllocate = sizeof(UUId_t) + message->base->header.length;
    TRACE("[ipc] [allocate] %u/%u", SourceMessage->base->header.protocol, SourceMessage->base->header.action);
    
    if (message->address->type == IPMSG_ADDRESS_HANDLE) {
        Context = LookupHandleOfType(message->address->data.handle, HandleTypeIpcContext);
    }
    else {
        UUId_t     Handle;
        OsStatus_t Status = LookupHandleByPath(message->address->data.path, &Handle);
        if (Status != OsSuccess) {
            ERROR("[ipc] [allocate] could not find target path %s", message->address->data.path);
            return Status;
        }
        
        Context = LookupHandleOfType(Handle, HandleTypeIpcContext);
    }
    
    if (!Context) {
        ERROR("[ipc] [allocate] could not find target handle %u", message->address->data.handle);
        return OsDoesNotExist;
    }
    
    BytesAvailable = streambuffer_write_packet_start(Context->KernelStream,
        BytesToAllocate, 0, &state->base, &state->state);
    if (!BytesAvailable) {
        ERROR("[ipc] [allocate] timeout allocating space for message");
        return OsTimeout;
    }
    
    *targetContext = Context;
    return OsSuccess;
}

static OsStatus_t
MapUntypedParameter(
    _In_ struct gracht_param* parameter,
    _In_ SystemMemorySpace_t* TargetMemorySpace)
{
    VirtualAddress_t CopyAddress;
    size_t     OffsetInPage = parameter->data.value % GetMemorySpacePageSize();
    OsStatus_t Status       = CloneMemorySpaceMapping(
        GetCurrentMemorySpace(), TargetMemorySpace,
        (VirtualAddress_t)parameter->data.buffer, &CopyAddress, parameter->length + OffsetInPage,
        MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_READONLY | MAPPING_PERSISTENT,
        MAPPING_VIRTUAL_PROCESS);
    if (Status != OsSuccess) {
        ERROR("[ipc] [map_untyped] Failed to clone ipc mapping");
        return Status;
    }
    
    // Update buffer pointer in untyped argument
    parameter->data.buffer = (void*)(CopyAddress + OffsetInPage);
    smp_wmb();
    
    return OsSuccess;
}

static OsStatus_t
WriteMessage(
    _In_ IpcContext_t*         context,
    _In_ struct ipmsg_header*  message,
    _In_ struct message_state* state)
{
    int i;
    
    TRACE("[ipc] [write] %u/%u [%u, %u]", Message->base->header.protocol,
        Message->base->header.action, sizeof(struct ipmsg_resp),
        sizeof(struct gracht_message) + (
            (Message->base->header.param_in + Message->base->header.param_out)
                * sizeof(struct gracht_param)));
    
    // Write all members in the order of ipmsg
    streambuffer_write_packet_data(context->KernelStream,
        &message->sender, sizeof(UUId_t), &state->state);
    
    // Fixup all SHM buffer values before writing the base message
    for (i = 0; i < message->base->header.param_in; i++) {
        if (message->base->params[i].type == GRACHT_PARAM_SHM &&
            message->base->params[i].length > 0) {
            MCoreThread_t* Thread = GetContextThread(context);
            OsStatus_t     Status = MapUntypedParameter(&message->base->params[i],
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
    
    streambuffer_write_packet_data(context->KernelStream,
        message->base, sizeof(struct gracht_message) +
            ((message->base->header.param_in + message->base->header.param_out)
                * sizeof(struct gracht_param)),
        &state->state);
    
    // Handle all the buffer/shm parameters
    for (i = 0; i < message->base->header.param_in; i++) {
        if (message->base->params[i].type == GRACHT_PARAM_BUFFER &&
            message->base->params[i].length > 0) {
            streambuffer_write_packet_data(context->KernelStream,
                message->base->params[i].data.buffer,
                message->base->params[i].length,
                &state->state);
        }
    }
    return OsSuccess;
}

static inline void
SendMessage(
    _In_ IpcContext_t*         context,
    _In_ struct ipmsg_header*  message,
    _In_ struct message_state* state)
{
    TRACE("[ipc] [send] %u/%u => %u",
        message->base->header.protocol, message->base->header.action,
        sizeof(struct ipmsg_resp) + message->base->header.length);
    
    streambuffer_write_packet_end(context->KernelStream, state->base,
        sizeof(UUId_t) + message->base->header.length);
    MarkHandle(context->Handle, IOSETIN);
}

static inline void
CleanupMessage(
    _In_ struct ipmsg* message)
{
    int i;
    TRACE("[ipc] [cleanup]");

    // Flush all the mappings granted in the argument phase
    for (i = 0; i < message->base.header.param_in; i++) {
        if (message->base.params[i].type == GRACHT_PARAM_SHM && 
            message->base.params[i].length > 0) {
            OsStatus_t status = MemorySpaceUnmap(GetCurrentMemorySpace(),
                (VirtualAddress_t)message->base.params[i].data.buffer,
                message->base.params[i].length);
            if (status != OsSuccess) {
                // LOG
            }
        }
    }
}

// THIS STRUCTURE MUST MATCH THE STRUCTURE IN libgracht/client.c
struct gracht_message_descriptor {
    gracht_object_header_t header;
    int                    status;
    struct gracht_message  message;
};

OsStatus_t
IpcContextSendMultiple(
    _In_ struct ipmsg_header** messages,
    _In_ int                   messageCount,
    _In_ size_t                timeout)
{
    struct message_state state;
    TRACE("[ipc] [send] count %i, timeout %u", MessageCount, LODWORD(Timeout));
    
    if (!messages || !messageCount) {
        return OsInvalidParameters;
    }
    
    for (int i = 0; i < messageCount; i++) {
        IpcContext_t* targetContext;
        OsStatus_t    status = AllocateMessage(messages[i], timeout, &state, &targetContext);
        if (status != OsSuccess) {
            // todo store status in context and return incomplete
            return OsIncomplete;
        }
        WriteMessage(targetContext, messages[i], &state);
        SendMessage(targetContext, messages[i], &state);
    }
    return OsSuccess;
}

OsStatus_t
IpcContextRespondMultiple(
    _In_ struct ipmsg**        messages,
    _In_ struct ipmsg_header** responses,
    _In_ int                   count)
{
    struct message_state state;
    int                  i;

    TRACE("[ipc] [respond]");
    if (!messages || !responses || !count) {
        ERROR("[ipc] [respond] input was null");
        return OsInvalidParameters;
    }
    
    for (i = 0; i < count; i++) {
        IpcContext_t* targetContext;
        OsStatus_t    status = AllocateMessage(responses[i], 0, &state, &targetContext);
        if (status != OsSuccess) {
            // todo store status in context and return incomplete
            return OsIncomplete;
        }
        WriteMessage(targetContext, responses[i], &state);
        SendMessage(targetContext, responses[i], &state);
        CleanupMessage(messages[i]);
    }
    return OsSuccess;
}
