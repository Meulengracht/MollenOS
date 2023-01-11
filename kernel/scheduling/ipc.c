/**
 * Copyright 2023, Philip Meulengracht
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
#include <shm.h>
#include <threading.h>

static oserr_t
__AllocateMessage(
        _In_  IPCMessage_t*              message,
        _In_  streambuffer_rw_options_t* options,
        _In_  streambuffer_packet_ctx_t* packetCtx,
        _Out_ IPCContext_t**             targetContext)
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
            bytesToAllocate,
            options,
            packetCtx
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
        _In_ IPCMessage_t*              message,
        _In_ streambuffer_packet_ctx_t* packetCtx)
{
    TRACE("__WriteMessage()");
    
    // write the header (senders handle)
    streambuffer_write_packet_data(
            &message->SenderHandle,
            sizeof(uuid_t),
            packetCtx
    );

    // write the actual payload
    streambuffer_write_packet_data(
            (void*)message->Payload,
            message->Length,
            packetCtx
    );
}

static inline void
SendMessage(
        _In_ IPCContext_t*              context,
        _In_ streambuffer_packet_ctx_t* packetCtx)
{
    TRACE("SendMessage()");
    streambuffer_write_packet_end(packetCtx);
    MarkHandle(context->Handle, IOSETIN);
}

oserr_t
IpcContextSendMultiple(
        _In_ IPCMessage_t**    messages,
        _In_ int               messageCount,
        _In_ OSTimestamp_t*    deadline,
        _In_ OSAsyncContext_t* asyncContext)
{
    streambuffer_packet_ctx_t packetCtx;
    streambuffer_rw_options_t options = {
            .flags = 0,
            .async_context = asyncContext,
            .deadline = deadline,
    };
    TRACE("[ipc] [send] count %i, timeout %u", messageCount, LODWORD(timeout));
    
    if (!messages || !messageCount) {
        return OS_EINVALPARAMS;
    }
    
    for (int i = 0; i < messageCount; i++) {
        IPCContext_t* targetContext;
        oserr_t       status = __AllocateMessage(
                messages[i],
                &options,
                &packetCtx,
                &targetContext
        );
        if (status != OS_EOK) {
            // todo store status in context and return incomplete
            return OS_EINCOMPLETE;
        }
        __WriteMessage(messages[i], &packetCtx);
        SendMessage(targetContext, &packetCtx);
    }
    return OS_EOK;
}
