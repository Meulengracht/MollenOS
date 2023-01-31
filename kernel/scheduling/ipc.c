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
#include <ioset.h>
#include <ipc_context.h>
#include <shm.h>

static oserr_t
__AllocateMessage(
        _In_  IPCMessage_t*              message,
        _In_  streambuffer_rw_options_t* options,
        _In_  streambuffer_packet_ctx_t* packetCtx,
        _Out_ uuid_t*                    streamIDOut)
{
    streambuffer_t* stream;
    size_t          bytesAvailable;
    size_t          bytesToAllocate = sizeof(uuid_t) + message->Length;
    uuid_t          streamID;
    oserr_t         oserr;
    TRACE("__AllocateMessage(target=%u, len=%" PRIuIN ")", message->Address->Data.Handle, bytesToAllocate);
    
    if (message->Address->Type == IPC_ADDRESS_HANDLE) {
        streamID = message->Address->Data.Handle;
    } else {
        oserr = LookupHandleByPath(message->Address->Data.Path, &streamID);
        if (oserr != OS_EOK) {
            ERROR("__AllocateMessage could not find target path %s", message->Address->Data.Path);
            return oserr;
        }
    }

    oserr = SHMKernelMapping(streamID, (void**)&stream);
    if (oserr != OS_EOK) {
        ERROR("__AllocateMessage could not find target handle %u", streamID);
        return oserr;
    }

    bytesAvailable = streambuffer_write_packet_start(
            stream,
            bytesToAllocate,
            options,
            packetCtx
    );
    if (!bytesAvailable) {
        ERROR("__AllocateMessage timeout allocating space for message");
        return OS_ENOENT;
    }
    
    *streamIDOut = streamID;
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
        _In_ uuid_t                     streamID,
        _In_ streambuffer_packet_ctx_t* packetCtx)
{
    TRACE("SendMessage()");
    streambuffer_write_packet_end(packetCtx);
    MarkHandle(streamID, IOSETIN);
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
    TRACE("IpcContextSendMultiple(count=%i)", messageCount);
    
    if (!messages || !messageCount) {
        return OS_EINVALPARAMS;
    }
    
    for (int i = 0; i < messageCount; i++) {
        uuid_t  streamID;
        oserr_t status = __AllocateMessage(
                messages[i],
                &options,
                &packetCtx,
                &streamID
        );
        if (status != OS_EOK) {
            // todo store status in context and return incomplete
            return OS_EINCOMPLETE;
        }
        __WriteMessage(messages[i], &packetCtx);
        SendMessage(streamID, &packetCtx);
    }
    return OS_EOK;
}
