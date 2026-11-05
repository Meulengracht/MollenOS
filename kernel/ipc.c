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
#include <handles/global.h>
#include <handles/paths.h>
#include <handles/set.h>
#include <memoryspace.h>
#include <ioset.h>
#include <ipc_context.h>
#include <shm/shm.h>

static oserr_t
__AllocateMessage(
    _In_  IPCMessage_t*              message,
    _In_  streambuffer_rw_options_t* options,
    _In_  streambuffer_packet_ctx_t* packetCtx,
    _Out_ uuid_t*                    destinationGlobalHandleIDOut)
{
    streambuffer_t* stream;
    size_t          bytesAvailable;
    size_t          payloadLength = message->Length;
    size_t          bytesToAllocate;
    uuid_t          destinationGlobalHandleID;
    oserr_t         oserr;

    if (payloadLength > SIZE_MAX - sizeof(IPCMessageContext_t)) {
        return OS_EINVALPARAMS;
    }
    
    bytesToAllocate = sizeof(IPCMessageContext_t) + payloadLength;
        
    if (message->Address->Type == IPC_ADDRESS_HANDLE) {
        destinationGlobalHandleID = message->Address->Data.GlobalHandleID;
    } else if (message->Address->Type == IPC_ADDRESS_PATH) {
        oserr = LookupHandleByPath(message->Address->Data.Path, &destinationGlobalHandleID);
        if (oserr != OS_EOK) {
            return oserr;
        }
    } else {
        return OS_EINVALPARAMS;
    }

    oserr = SHMAcquireGlobalIPCBuffer(destinationGlobalHandleID, (void**)&stream);
    if (oserr != OS_EOK) {
        return oserr;
    }

    bytesAvailable = streambuffer_write_packet_start(
        stream,
        bytesToAllocate,
        options,
        packetCtx
    );
    if (!bytesAvailable) {
        ReleaseGlobalHandle(destinationGlobalHandleID);
        return OS_ENOENT;
    }
    
    *destinationGlobalHandleIDOut = destinationGlobalHandleID;
    return OS_EOK;
}

static void
__WriteMessage(
    _In_ IPCMessage_t*              message,
    _In_ streambuffer_packet_ctx_t* packetCtx,
    _In_ IPCMessageContext_t*       context)
{
    size_t payloadLength = packetCtx->_length - sizeof(IPCMessageContext_t);
    
    TRACE("__WriteMessage()");
    
    // The kernel supplied the process identity and validated the sender endpoint.
    streambuffer_write_packet_data(
        context,
        sizeof(IPCMessageContext_t),
        packetCtx
    );

    // Write only the payload length recorded in the successful reservation.
    streambuffer_write_packet_data(
        (void*)message->Payload,
        payloadLength,
        packetCtx
    );
}

static oserr_t
__SendMessage(
    _In_ IPCMessage_t*              message,
    _In_ streambuffer_rw_options_t* options,
    _In_ MSContextSecurity_t*       securityContext)
{
    IPCMessage_t              request;
    IPCMessageContext_t       context;
    streambuffer_packet_ctx_t packetContext;
    void*                     senderMapping;
    uuid_t                    destinationGlobalHandleID;
    oserr_t                   oserr;

    if (message == NULL) {
        return OS_EINVALPARAMS;
    }

    // Copy the request before checking the sender ID, so we use the same ID
    // even if the send blocks.
    request = *message;
    if (request.Address == NULL) {
        return OS_EINVALPARAMS;
    }

    context.SenderGroupGlobalHandleID = securityContext->GlobalHandleID;
    context.Capabilities = (uint32_t)securityContext->Capabilities;
    context.SenderGlobalHandleID = request.SenderHandleID;
    if (request.SenderHandleID != UUID_INVALID) {
        oserr = SHMAcquireIPCBufferReference(request.SenderHandleID, &senderMapping);
        if (oserr != OS_EOK) {
            return oserr;
        }
    }

    oserr = __AllocateMessage(&request, options, &packetContext, &destinationGlobalHandleID);
    if (oserr == OS_EOK) {
        __WriteMessage(&request, &packetContext, &context);
        streambuffer_write_packet_end(&packetContext);
        MarkHandle(destinationGlobalHandleID, IOSETIN);
        ReleaseGlobalHandle(destinationGlobalHandleID);
    }

    if (request.SenderHandleID != UUID_INVALID) {
        ReleaseGlobalHandle(request.SenderHandleID);
    }
    return oserr;
}

oserr_t
IpcContextSendMultiple(
    _In_ IPCMessage_t**    messages,
    _In_ int               messageCount,
    _In_ OSTimestamp_t*    deadline,
    _In_ OSAsyncContext_t* asyncContext)
{
    MSContextSecurity_t*      secCtx;
    streambuffer_rw_options_t options = {
            .flags = 0,
            .async_context = asyncContext,
            .deadline = deadline,
    };
    TRACE("IpcContextSendMultiple(count=%i)", messageCount);
    
    if (!messages || messageCount <= 0) {
        return OS_EINVALPARAMS;
    }

    // All IPC messages must be sent from a valid security context.
    secCtx = MemorySpaceGetSecurityContext();
    if (!secCtx) {
        return OS_EPERMISSIONS;
    }
    
    for (int i = 0; i < messageCount; i++) {
        oserr_t status = __SendMessage(messages[i], &options, secCtx);
        if (status != OS_EOK) {
            // todo store status in context and return incomplete
            return OS_EINCOMPLETE;
        }
    }
    return OS_EOK;
}
