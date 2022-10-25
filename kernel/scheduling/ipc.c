/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
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
#include <memory_region.h>
#include <threading.h>

typedef struct IPCContext {
    uuid_t          Handle;
    uuid_t          CreatorThreadHandle;
    uuid_t          MemoryRegionHandle;
    streambuffer_t* KernelStream;
} IPCContext_t;

static void
IpcContextDestroy(
    _In_ void* resource)
{
    IPCContext_t* context = resource;
    DestroyHandle(context->MemoryRegionHandle);
    kfree(context);
}

oserr_t
IpcContextCreate(
        _In_  size_t  Size,
        _Out_ uuid_t* HandleOut,
        _Out_ void**  UserContextOut)
{
    IPCContext_t* ipcContext;
    oserr_t       oserr;
    void*         kernelMapping;
    TRACE("IpcContextCreate(%u)", Size);
    
    if (!HandleOut || !UserContextOut) {
        return OS_EINVALPARAMS;
    }

    ipcContext = kmalloc(sizeof(IPCContext_t));
    if (!ipcContext) {
        return OS_EOOM;
    }

    ipcContext->CreatorThreadHandle = ThreadCurrentHandle();

    oserr = MemoryRegionCreate(
            Size,
            Size,
            0,
            0,
            &kernelMapping,
            UserContextOut,
            &ipcContext->MemoryRegionHandle
    );
    if (oserr != OS_EOK) {
        kfree(ipcContext);
        return oserr;
    }

    ipcContext->Handle       = CreateHandle(HandleTypeIpcContext, IpcContextDestroy, ipcContext);
    ipcContext->KernelStream = (streambuffer_t*)kernelMapping;
    streambuffer_construct(ipcContext->KernelStream, Size - sizeof(streambuffer_t),
        STREAMBUFFER_GLOBAL | STREAMBUFFER_MULTIPLE_WRITERS);
    
    *HandleOut = ipcContext->Handle;
    return oserr;
}

struct __MessageState {
    unsigned int base;
    unsigned int state;
};

static oserr_t
__AllocateMessage(
        _In_  IPCMessage_t*          message,
        _In_  size_t                 timeout,
        _In_  struct __MessageState* state,
        _Out_ IPCContext_t**         targetContext)
{
    IPCContext_t* ipcContext;
    size_t        bytesAvailable;
    size_t        bytesToAllocate = sizeof(uuid_t) + message->Length;
    TRACE("__AllocateMessage(target=%u, len=%" PRIuIN ")", message->Address->Data.Handle, bytesToAllocate);
    
    if (message->Address->Type == IPC_ADDRESS_HANDLE) {
        ipcContext = LookupHandleOfType(message->Address->Data.Handle, HandleTypeIpcContext);
    } else {
        uuid_t  handle;
        oserr_t oserr = LookupHandleByPath(message->Address->Data.Path, &handle);
        if (oserr != OS_EOK) {
            ERROR("__AllocateMessage could not find target path %s", message->Address->Data.Path);
            return oserr;
        }

        ipcContext = LookupHandleOfType(handle, HandleTypeIpcContext);
    }
    
    if (!ipcContext) {
        ERROR("__AllocateMessage could not find target handle %u", message->Address->Data.Handle);
        return OS_ENOENT;
    }

    bytesAvailable = streambuffer_write_packet_start(
            ipcContext->KernelStream,
            bytesToAllocate, 0,
            &state->base, &state->state
    );
    if (!bytesAvailable) {
        ERROR("__AllocateMessage timeout allocating space for message");
        return OS_ETIMEOUT;
    }
    
    *targetContext = ipcContext;
    return OS_EOK;
}

static void
__WriteMessage(
        _In_ IPCContext_t*          context,
        _In_ IPCMessage_t*          message,
        _In_ struct __MessageState* state)
{
    TRACE("__WriteMessage()");
    
    // write the header (senders handle)
    streambuffer_write_packet_data(
            context->KernelStream,
            &message->SenderHandle,
            sizeof(uuid_t),
            &state->state
    );

    // write the actual payload
    streambuffer_write_packet_data(
            context->KernelStream,
            (void*)message->Payload,
            message->Length,
            &state->state
    );
}

static inline void
SendMessage(
        _In_ IPCContext_t*          context,
        _In_ IPCMessage_t*          message,
        _In_ struct __MessageState* state)
{
    size_t bytesToCommit;
    TRACE("SendMessage()");

    bytesToCommit = sizeof(uuid_t) + message->Length;
    streambuffer_write_packet_end(context->KernelStream, state->base, bytesToCommit);
    MarkHandle(context->Handle, IOSETIN);
}

oserr_t
IpcContextSendMultiple(
    _In_ IPCMessage_t** messages,
    _In_ int            messageCount,
    _In_ size_t         timeout)
{
    struct __MessageState state;
    TRACE("[ipc] [send] count %i, timeout %u", messageCount, LODWORD(timeout));
    
    if (!messages || !messageCount) {
        return OS_EINVALPARAMS;
    }
    
    for (int i = 0; i < messageCount; i++) {
        IPCContext_t* targetContext;
        oserr_t       status = __AllocateMessage(
                messages[i],
                timeout,
                &state,
                &targetContext
        );
        if (status != OS_EOK) {
            // todo store status in context and return incomplete
            return OS_EINCOMPLETE;
        }
        __WriteMessage(targetContext, messages[i], &state);
        SendMessage(targetContext, messages[i], &state);
    }
    return OS_EOK;
}
