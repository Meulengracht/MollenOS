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

#define __need_minmax
#include <ddk/service.h>
#include <gracht/link/vali.h>
#include <inet/socket.h>
#include <internal/_gracht.h>
#include <internal/_socket.h>
#include <internal/_tls.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/handle.h>
#include <os/notification_queue.h>
#include <os/shm.h>
#include <ddk/convert/grants.h>
#include <os/services/net.h>
#include <os/mutex.h>
#include <sys_socket_service_client.h>

struct Socket {
    int        Type;
    OSHandle_t Send;
    bool       SendMapped;
    OSHandle_t Recv;
    bool       RecvMapped;
    Mutex_t    Lock;
};

static void    __SocketDestroy(struct OSHandle*);
static oserr_t __SocketRelease(struct OSHandle*);

const OSHandleOps_t g_socketOps = {
    .Destroy = __SocketDestroy,
    .Release = __SocketRelease
};

static struct Socket*
__SocketNew(
    _In_ int type)
{
    struct Socket* socket;

    socket = calloc(1, sizeof(struct Socket));
    if (socket == NULL) {
        return NULL;
    }
    socket->Type = type;
    MutexInitialize(&socket->Lock, MUTEX_PLAIN);
    return socket;
}

static enum sys_close_options
__ToProtocolCloseOptions(
    _In_ enum OSocketShutdownType type)
{
    switch (type) {
        case OSSOCKET_SHUTDOWN_RECV:       return SYS_CLOSE_OPTIONS_READ;
        case OSSOCKET_SHUTDOWN_SEND:       return SYS_CLOSE_OPTIONS_WRITE;
        case OSSOCKET_SHUTDOWN_RECVSEND:   return SYS_CLOSE_OPTIONS_READ_WRITE;
        default: return -1; // Invalid value
    }
}

oserr_t
OSSocketShutdown(
    _In_ OSHandle_t*              handle,
    _In_ enum OSocketShutdownType type)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    enum sys_close_options   operation = __ToProtocolCloseOptions(type);
    oserr_t                  status;

    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }
    
    if (handle->Type != OSHANDLE_SOCKET) {
        return OS_ENOTSUPPORTED;
    }
    
    if (operation == -1) {
        return OS_EINVALPARAMS;
    }

    if (sys_socket_close(GetGrachtClient(), &msg.base, handle->ID, operation) != 0 ||
        gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC) != 0 ||
        sys_socket_close_result(GetGrachtClient(), &msg.base, &status) != 0) {
        return OS_EUNKNOWN;
    }
    return status;
}

static oserr_t
__SocketAttachPipe(
    _In_  const struct sys_shm_transfer* description,
    _Out_ OSHandle_t*                   handle)
{
    SHMTransfer_t transfer;
    unsigned int  scope = GrantTokenScopeRead | GrantTokenScopeWrite;
    oserr_t       status;

    status = __FromSysSHMTransfer(description, &transfer);
    if (status != OS_EOK) {
        return status;
    }

    if (transfer.RangeMode == SHMTransferRangeSubrange &&
        (transfer.RelativeOffset != 0 || transfer.RequestedLength != transfer.Token.Options.Data.SHM.Length)) {
        return OS_EINVALPARAMS;
    }

    if ((transfer.Token.Options.Scope & scope) != scope) {
        return OS_EPERMISSIONS;
    }

    status = SHMAttachToken(&transfer.Token, handle);
    if (status != OS_EOK) {
        return status;
    }

    status = SHMMap(
        handle,
        0,
        SHMBufferCapacity(handle),
        SHM_ACCESS_READ | SHM_ACCESS_WRITE
    );
    if (status != OS_EOK) {
        OSHandleDestroy(handle);
    }
    return status;
}

static oserr_t
__SocketAttachPipes(
    _In_ struct Socket*                 socket,
    _In_ const struct sys_shm_transfer* recvBuffer,
    _In_ const struct sys_shm_transfer* sendBuffer)
{
    oserr_t status;

    status = __SocketAttachPipe(recvBuffer, &socket->Recv);
    if (status != OS_EOK) {
        return status;
    }

    status = __SocketAttachPipe(sendBuffer, &socket->Send);
    if (status != OS_EOK) {
        OSHandleDestroy(&socket->Recv);
        return status;
    }

    socket->RecvMapped = true;
    socket->SendMapped = true;
    return OS_EOK;
}

static oserr_t
__SocketBuild(
    _In_  uuid_t                        referenceID,
    _In_  const struct sys_socket_info* info,
    _Out_ OSHandle_t*                   handleOut)
{
    struct Socket* socket;
    oserr_t        status;

    if (info->domain < 0 || info->domain >= AF_MAX) {
        return OS_EPROTOCOL;
    }

    if (info->type < SOCK_STREAM || info->type > SOCK_SEQPACKET) {
        return OS_EPROTOCOL;
    }

    socket = __SocketNew(info->type);
    if (socket == NULL) {
        return OS_EOOM;
    }

    // Create, accept and inherited-reference adoption use the same buffer
    // setup. The local socket is published only after both mappings succeed.
    status = __SocketAttachPipes(socket, &info->recv_buffer, &info->send_buffer);
    if (status == OS_EOK) {
        status = OSHandleWrap(referenceID, OSHANDLE_SOCKET, socket, true, handleOut);
        if (status != OS_EOK) {
            OSHandleDestroy(&socket->Send);
            OSHandleDestroy(&socket->Recv);
        }
    }
    if (status != OS_EOK) {
        free(socket);
    }
    return status;
}

static oserr_t
__SocketImport(
    _In_  const struct sys_grant_token* reference,
    _In_  const struct sys_socket_info* info,
    _Out_ OSHandle_t*                   handleOut)
{
    GrantToken_t token;
    uuid_t       referenceID;
    oserr_t      status;

    status = __FromSysGrantToken(reference, &token);
    if (status != OS_EOK) {
        return status;
    }

    if (token.Options.ResourceType != GrantTokenResourceTypeProvider ||
        token.Options.Scope != OS_SOCKET_ACCESS) {
        return OS_EPROTOCOL;
    }

    status = Syscall_HandleImport(&token, UUID_INVALID, &referenceID);
    if (status != OS_EOK) {
        return status;
    }

    status = __SocketBuild(referenceID, info, handleOut);
    if (status != OS_EOK) {
        // Failed local setup drops only this imported reference. Other clients
        // keep using the same socket until their own references are closed.
        (void)Syscall_HandleDestroy(referenceID);
    }
    return status;
}

oserr_t
OSSocketAcquire(
    _In_  uuid_t      referenceID,
    _Out_ OSHandle_t* handleOut)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    struct sys_socket_info   info = { 0 };
    oserr_t                  status;

    if (handleOut == NULL) {
        return OS_EINVALPARAMS;
    }
    // Processd already installed this reference in the child. The acquire RPC
    // validates it and returns existing ring tokens without adding another
    // socket reference. A repeated RPC therefore creates no server-side state.
    if (sys_socket_acquire(GetGrachtClient(), &msg.base, referenceID) != 0) {
        return OS_EUNKNOWN;
    }

    status = __GrachtAwaitCompleted(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    if (status != OS_EOK) {
        return status;
    }

    if (sys_socket_acquire_result(GetGrachtClient(), &msg.base, &status, &info) != 0) {
        return OS_EPROTOCOL;
    }
    if (status != OS_EOK) {
        return status;
    }

    return __SocketBuild(referenceID, &info, handleOut);
}

oserr_t
OSSocketOpen(
    _In_  int         domain,
    _In_  int         type,
    _In_  int         protocol,
    _Out_ OSHandle_t* handleOut)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    struct sys_grant_token   reference = { 0 };
    struct sys_socket_info   info = { 0 };
    oserr_t                  status;

    if (handleOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (sys_socket_create(GetGrachtClient(), &msg.base, domain, type, protocol) != 0) {
        return OS_EUNKNOWN;
    }

    status = __GrachtAwaitCompleted(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    if (status != OS_EOK) {
        return status;
    }

    if (sys_socket_create_result(GetGrachtClient(), &msg.base, &status, &reference, &info) != 0) {
        return OS_EPROTOCOL;
    }
    if (status != OS_EOK) {
        return status;
    }

    return __SocketImport(&reference, &info, handleOut);
}

oserr_t
OSSocketPair(
    _In_  OSHandle_t* sock0,
    _In_  OSHandle_t* sock1)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    oserr_t                  oserr;

    sys_socket_pair(GetGrachtClient(), &msg.base, sock0->ID, sock1->ID);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_socket_pair_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

oserr_t
OSSocketAccept(
    _In_  OSHandle_t*      handle,
    _In_  struct sockaddr* address,
    _In_  socklen_t*       addressLength,
    _Out_ OSHandle_t*      handleOut)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    struct Socket*           source;
    struct sockaddr_storage  peerAddress = { 0 };
    uint32_t                 peerAddressLength = sizeof(struct sockaddr_storage);
    struct sys_grant_token   reference = { 0 };
    struct sys_socket_info   info = { 0 };
    oserr_t                  oserr = OS_EPROTOCOL;

    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }

    if (address != NULL && addressLength == NULL) {
        return OS_EINVALPARAMS;
    }

    // Accept is only valid on handles that are of socket type
    if (handle->Type != OSHANDLE_SOCKET) {
        return OS_ENOTSUPPORTED;
    }

    // Accept is only supported on sockets that are sequential or streams
    source = handle->Payload;
    if (source->Type != SOCK_SEQPACKET && source->Type != SOCK_STREAM) {
        return OS_ENOTSUPPORTED;
    }

    if (sys_socket_accept(GetGrachtClient(), &msg.base, handle->ID) != 0) {
        return OS_EUNKNOWN;
    }
    
    oserr = __GrachtAwaitCompleted(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    if (oserr != OS_EOK) {
        return oserr;
    }

    if (sys_socket_accept_result(
        GetGrachtClient(),
        &msg.base,
        &oserr,
        (uint8_t*)&peerAddress,
        &peerAddressLength,
        &reference,
        &info
    ) != 0) {
        return OS_EPROTOCOL;
    }
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __SocketImport(&reference, &info, handleOut);
    if (oserr != OS_EOK) {
        return oserr;
    }
    if (address != NULL) {
        memcpy(address, &peerAddress, MIN(*addressLength, peerAddress.__ss_len));
        *addressLength = peerAddress.__ss_len;
    }
    return OS_EOK;
}

oserr_t
OSSocketBind(
    _In_ OSHandle_t*            handle,
    _In_ const struct sockaddr* address,
    _In_ socklen_t              addressLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    oserr_t                  oserr = OS_EPROTOCOL;

    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }

    // Accept is only valid on handles that are of socket type
    if (handle->Type != OSHANDLE_SOCKET) {
        return OS_ENOTSUPPORTED;
    }

    sys_socket_bind(
        GetGrachtClient(),
        &msg.base,
        handle->ID,
        (const uint8_t*)address,
        addressLength
    );
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_socket_bind_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

oserr_t
OSSocketConnect(
    _In_ OSHandle_t*            handle,
    _In_ const struct sockaddr* address,
    _In_ socklen_t              addressLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    oserr_t                  oserr = OS_EPROTOCOL;

    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }

    // Accept is only valid on handles that are of socket type
    if (handle->Type != OSHANDLE_SOCKET) {
        return OS_ENOTSUPPORTED;
    }

    sys_socket_connect(
        GetGrachtClient(),
        &msg.base,
        handle->ID,
        (const uint8_t*)address,
        addressLength
    );
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_socket_connect_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

oserr_t
OSSocketAddress(
    _In_ OSHandle_t*      handle,
    _In_ int              type,
    _In_ struct sockaddr* address,
    _In_ socklen_t        addressMaxSize)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    oserr_t                  oserr = OS_EPROTOCOL;
    uint32_t                 decodedLength = addressMaxSize;

    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }

    // Accept is only valid on handles that are of socket type
    if (handle->Type != OSHANDLE_SOCKET) {
        return OS_ENOTSUPPORTED;
    }

    sys_socket_get_address(GetGrachtClient(), &msg.base, handle->ID, type);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_socket_get_address_result(
        GetGrachtClient(), &msg.base,
        &oserr,
        (uint8_t*)address,
        &decodedLength
    );
    return oserr;
}

oserr_t
OSSocketListen(
    _In_ OSHandle_t* handle,
    _In_ int         queueSize)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    struct Socket*           socket;
    oserr_t                  oserr = OS_EPROTOCOL;

    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }

    if (handle->Type != OSHANDLE_SOCKET) {
        return OS_ENOTSUPPORTED;
    }

    socket = handle->Payload;
    if (socket->Type != SOCK_SEQPACKET && socket->Type != SOCK_STREAM) {
        return OS_ENOTSUPPORTED;
    }

    sys_socket_listen(GetGrachtClient(), &msg.base, handle->ID, queueSize);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_socket_listen_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

oserr_t
OSSocketSetOption(
    _In_ OSHandle_t* handle,
    _In_ int         protocol,
    _In_ int         option,
    _In_ const void* data,
    _In_ socklen_t   length)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    oserr_t                  oserr = OS_EPROTOCOL;

    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }

    if (handle->Type != OSHANDLE_SOCKET) {
        return OS_ENOTSUPPORTED;
    }

    sys_socket_set_option(GetGrachtClient(), &msg.base, handle->ID,
                          protocol, option, data, length, length);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_socket_set_option_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

oserr_t
OSSocketOption(
    _In_    OSHandle_t* handle,
    _In_    int         protocol,
    _In_    int         option,
    _In_    void*       data,
        _InOut_ socklen_t*  length)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    oserr_t                  oserr = OS_EPROTOCOL;
    int                      sizeOfData;
    uint32_t                 decodedLength;

    if (handle == NULL || length == NULL) {
        return OS_EINVALPARAMS;
    }

    if (handle->Type != OSHANDLE_SOCKET) {
        return OS_ENOTSUPPORTED;
    }

    sys_socket_get_option(GetGrachtClient(), &msg.base, handle->ID, protocol, option);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    decodedLength = (uint32_t)*length;
    sys_socket_get_option_result(
        GetGrachtClient(), &msg.base,
        &oserr,
        data,
        &decodedLength,
        &sizeOfData
    );
    if (oserr == OS_EOK) {
        if (sizeOfData < 0 || (uint32_t)sizeOfData != decodedLength) {
            return OS_EPROTOCOL;
        }
        *length = (socklen_t)decodedLength;
    }
    return oserr;
}

oserr_t
OSSocketSendPipe(
    _In_  OSHandle_t*      handle,
    _Out_ streambuffer_t** pipeOut)
{
    struct OSSocketSendState* state;
    struct Socket*            socket;
    oserr_t                   oserr = OS_EOK;

    if (handle == NULL || handle->Type != OSHANDLE_SOCKET) {
        return OS_EINVALPARAMS;
    }

    socket = handle->Payload;
    
    // Two cases can happen here, either the pipe is not mapped (socket was
    // created by this process), or the pipe may already be mapped (it has
    // been mapped by a previous call to OSSocketSendPipe, or it has been
    // inheritted).
    MutexLock(&socket->Lock);
    if (socket->SendMapped) {
        state = SHMBuffer(&socket->Send);
        
        *pipeOut = state != NULL ? __SocketSendStream(state) : NULL;
        if (*pipeOut == NULL) {
            oserr = OS_ECANCELLED;
        }
        goto exit;
    }

    oserr = SHMMap(
        &socket->Send,
        0,
        SHMBufferCapacity(&socket->Send),
        SHM_ACCESS_READ | SHM_ACCESS_WRITE
    );
    if (oserr != OS_EOK) {
        OSHandleDestroy(&socket->Send);
        goto exit;
    }

    socket->SendMapped = true;
    
    *pipeOut = __SocketSendStream(SHMBuffer(&socket->Send));

exit:
    MutexUnlock(&socket->Lock);
    return oserr;
}

static oserr_t
__SendStream(
    _In_  OSHandle_t*                handle,
    _In_  streambuffer_t*            stream,
    _In_  const struct msghdr*       message,
    _In_  streambuffer_rw_options_t* rwOptions,
    _Out_ size_t*                    bytesSentOut)
{
    size_t  bytesSent = 0;
    oserr_t oserr;

    for (int i = 0; i < message->msg_iovlen; i++) {
        struct iovec* iov = &message->msg_iov[i];
        size_t bytes_streamed = streambuffer_stream_out(
            stream,
            iov->iov_base,
            iov->iov_len,
            rwOptions
        );
        if (!bytes_streamed) {
            break;
        }
        bytesSent += bytes_streamed;
    }

    oserr = OSNotificationQueuePost(handle,IOSETOUT);
    if (oserr != OS_EOK) {
        return oserr;
    }

    *bytesSentOut = bytesSent;
    return OS_EOK;
}

static oserr_t
__SendPacket(
    _In_  OSHandle_t*                handle,
    _In_  streambuffer_t*            stream,
    _In_  const struct msghdr*       message,
    _In_  int                        flags,
    _In_  streambuffer_rw_options_t* rwOptions,
    _Out_ size_t*                    bytesSentOut)
{
    struct Socket*            socket = handle->Payload;
    struct OSSocketSendState* state;
    size_t                    bytesSent   = 0;
    size_t                    payload_len = 0;
    size_t                    meta_len;
    struct packethdr          packet = { 0 };
    size_t                    avail_len;
    struct iovec*             iov;
    size_t                    count;
    oserr_t                   oserr;
    streambuffer_packet_ctx_t packetCtx;

    packet.flags = flags & (MSG_OOB | MSG_DONTROUTE);
    packet.controllen = message->msg_control != NULL ? message->msg_controllen : 0;
    packet.addresslen = message->msg_name != NULL ? message->msg_namelen : 0;
    packet.destination = UUID_INVALID;
    if (packet.addresslen == 0) {
        // Reading the shared peer keeps inherited handles and reconnects in
        // agreement. Store it in the packet before any wait for ring space.
        state = SHMBuffer(&socket->Send);
        packet.destination = atomic_load_explicit(&state->Peer, memory_order_acquire);
        if (socket->Type != SOCK_SEQPACKET && packet.destination == UUID_INVALID) {
            return OS_ENOTCONNECTED;
        }
    }

    meta_len = sizeof(struct packethdr) + packet.addresslen + packet.controllen;

    // Writing an packet is an atomic action and the entire packet must be written
    // at once. So don't support STREAMBUFFER_ALLOW_PARTIAL
    rwOptions->flags &= ~(STREAMBUFFER_ALLOW_PARTIAL);

    // Otherwise we must build a packet, to do this we need to know the entire
    // length of the message before committing.
    for (int i = 0; i < message->msg_iovlen; i++) {
        payload_len += message->msg_iov[i].iov_len;
    }

    packet.payloadlen = payload_len;

    avail_len = streambuffer_write_packet_start(
        stream,
        meta_len + payload_len,
        rwOptions,
        &packetCtx
    );
    if (avail_len < (meta_len + payload_len)) {
        if (!(flags & MSG_DONTWAIT)) {
            _set_errno(EPIPE);
            return -1;
        }
        return 0;
    }

    streambuffer_write_packet_data(&packet, sizeof(struct packethdr), &packetCtx);
    if (message->msg_name && message->msg_namelen) {
        streambuffer_write_packet_data(message->msg_name, message->msg_namelen, &packetCtx);
    }
    if (message->msg_control && message->msg_controllen) {
        streambuffer_write_packet_data(message->msg_control, message->msg_controllen, &packetCtx);
    }

    for (int i = 0; i < message->msg_iovlen; i++) {
        iov = &message->msg_iov[i];
        count = MIN(avail_len - meta_len, iov->iov_len);
        if (!count) {
            break;
        }

        streambuffer_write_packet_data(iov->iov_base, iov->iov_len, &packetCtx);
        meta_len += count;
        bytesSent += count;
    }

    streambuffer_write_packet_end(&packetCtx);
    oserr = OSNotificationQueuePost(handle,IOSETOUT);
    if (oserr != OS_EOK) {
        return oserr;
    }

    *bytesSentOut = bytesSent;
    return OS_EOK;
}

// Valid flags for send are
// MSG_CONFIRM      (Not supported)
// MSG_EOR          (Not supported)
// MSG_OOB          (No OOB support)
// MSG_MORE         (Not supported)
// MSG_DONTROUTE    (Dunno what this is)
// MSG_DONTWAIT     (Supported)
// MSG_NOSIGNAL     (Ignored on Vali)
// MSG_CMSG_CLOEXEC (Ignored on Vali)
static oserr_t
__SendMessage(
    _In_  OSHandle_t*          handle,
    _In_  streambuffer_t*      stream,
    _In_  const struct msghdr* message,
    _In_  int                  flags,
    _Out_ size_t*              bytesSentOut)
{
    struct Socket*            socket = handle->Payload;
    OSAsyncContext_t*         asyncContext = __tls_current()->async_context;
    streambuffer_rw_options_t rwOptions = {
        .flags = 0,
        .async_context = asyncContext,
        .deadline = NULL
    };

    if (flags & MSG_OOB) {
        rwOptions.flags |= STREAMBUFFER_PRIORITY;
    }

    if (flags & MSG_DONTWAIT) {
        rwOptions.flags |= STREAMBUFFER_NO_BLOCK | STREAMBUFFER_ALLOW_PARTIAL;
    }

    // For stream sockets we don't need to build the packet header. Simply just
    // write all the bytes possible to the send socket and return
    if (asyncContext) {
        OSAsyncContextInitialize(asyncContext);
    }
    if (socket->Type == SOCK_STREAM) {
        return __SendStream(handle, stream, message, &rwOptions, bytesSentOut);
    }
    return __SendPacket(handle, stream, message, flags, &rwOptions, bytesSentOut);
}

oserr_t
OSSocketSend(
    _In_  OSHandle_t*          handle,
    _In_  const struct msghdr* message,
    _In_  int                  flags,
    _Out_ size_t*              bytesSentOut)
{
    streambuffer_t* stream;
    oserr_t         oserr;

    oserr = OSSocketSendPipe(handle, &stream);
    if (oserr != OS_EOK) {
        return oserr;
    }

    if (message == NULL) {
        return OS_EINVALPARAMS;
    }

    if (streambuffer_has_option(stream, STREAMBUFFER_DISABLED)) {
        return OS_ELINKINVAL;
    }

    return __SendMessage(handle, stream, message, flags, bytesSentOut);
}

oserr_t
OSSocketRecvPipe(
    _In_  OSHandle_t*      handle,
    _Out_ streambuffer_t** pipeOut)
{
    struct Socket* socket;
    oserr_t        oserr = OS_EOK;

    if (handle == NULL || handle->Type != OSHANDLE_SOCKET) {
        return OS_EINVALPARAMS;
    }

    socket = handle->Payload;
    // Two cases can happen here, either the pipe is not mapped (socket was
    // created by this process), or the pipe may already be mapped (it has
    // been mapped by a previous call to OSSocketRecvPipe, or it has been
    // inheritted).
    MutexLock(&socket->Lock);
    if (socket->RecvMapped) {
        *pipeOut = SHMBuffer(&socket->Recv);
        if (*pipeOut == NULL) {
            oserr = OS_ECANCELLED;
        }
        goto exit;
    }

    oserr = SHMMap(
            &socket->Recv,
            0,
            SHMBufferCapacity(&socket->Recv),
            SHM_ACCESS_READ | SHM_ACCESS_WRITE
    );
    if (oserr != OS_EOK) {
        OSHandleDestroy(&socket->Recv);
        goto exit;
    }

    socket->RecvMapped = true;
    *pipeOut = SHMBuffer(&socket->Recv);

exit:
    MutexUnlock(&socket->Lock);
    return oserr;
}

static inline unsigned int
__StreambufferFlags(int flags)
{
    unsigned int sb_options = 0;

    if (flags & MSG_OOB) {
        sb_options |= STREAMBUFFER_PRIORITY;
    }

    if (flags & MSG_DONTWAIT) {
        sb_options |= STREAMBUFFER_NO_BLOCK;
    }

    if (!(flags & MSG_WAITALL)) {
        sb_options |= STREAMBUFFER_ALLOW_PARTIAL;
    }

    if (flags & MSG_PEEK) {
        sb_options |= STREAMBUFFER_PEEK;
    }

    return sb_options;
}

static oserr_t
__RecvStream(
    _In_  OSHandle_t*                handle,
    _In_  streambuffer_t*            stream,
    _In_  struct msghdr*             message,
    _In_  streambuffer_rw_options_t* rwOptions,
    _Out_ size_t*                    bytesRecievedOut)
{
    size_t total = 0;
    streambuffer_rw_options_t options = *rwOptions;
    // WAITALL must return credit between partial reads, including between
    // iovecs, or a request larger than the receive pipe can deadlock.
    options.flags |= STREAMBUFFER_ALLOW_PARTIAL;
    for (int i = 0; i < message->msg_iovlen; i++) {
        struct iovec* iov = &message->msg_iov[i];
        size_t offset = 0;
        while (offset < iov->iov_len) {
            size_t count = streambuffer_stream_in(stream,
                    (uint8_t*)iov->iov_base + offset, iov->iov_len - offset, &options);
            total += count;
            offset += count;
            if (count) {
                OSNotificationQueuePost(handle, IOSETCREDIT);
            }
            if (!count || (rwOptions->flags & (STREAMBUFFER_ALLOW_PARTIAL | STREAMBUFFER_NO_BLOCK))) {
                if (offset < iov->iov_len) {
                    *bytesRecievedOut = total;
                    return total || (options.flags & STREAMBUFFER_NO_BLOCK) ? OS_EOK : OS_ELINKINVAL;
                }
            }
        }
    }
    *bytesRecievedOut = total;
    return OS_EOK;
}

// Valid flags for recv are
// MSG_OOB          (No OOB support)
// MSG_PEEK         (No peek support)
// MSG_WAITALL      (Supported)
// MSG_DONTWAIT     (Supported)
// MSG_NOSIGNAL     (Ignored on Vali)
// MSG_CMSG_CLOEXEC (Ignored on Vali)
static oserr_t
__RecvMessage(
    _In_  OSHandle_t*                handle,
    _In_  streambuffer_t*            stream,
    _In_  struct msghdr*             message,
    _In_  streambuffer_rw_options_t* rwOptions,
    _Out_ size_t*                    bytesRecievedOut)
{
    struct Socket*            socket = handle->Payload;
    intmax_t                  numbytes;
    struct packethdr          packet;
    int                       i;
    size_t                    addressBytes;
    streambuffer_packet_ctx_t packetCtx;

    // In case of stream sockets we simply just read as many bytes as requested
    // or available and return, unless WAITALL has been specified.
    if (socket->Type == SOCK_STREAM) {
        message->msg_namelen = 0;
        return __RecvStream(handle, stream, message, rwOptions, bytesRecievedOut);
    }

    // Reading a packet is an atomic action and the entire packet must be read
    // at once. So don't support STREAMBUFFER_ALLOW_PARTIAL
    rwOptions->flags &= ~(STREAMBUFFER_ALLOW_PARTIAL);
    numbytes = (intmax_t)streambuffer_read_packet_start(stream, rwOptions, &packetCtx);
    if (numbytes < sizeof(struct packethdr)) {
        if (!numbytes) {
            if (!(rwOptions->flags & STREAMBUFFER_ALLOW_PARTIAL)) {
                _set_errno(EPIPE);
                return -1;
            }
            return 0;
        }

        // If we read an invalid number of bytes then something evil happened.
        streambuffer_read_packet_end(&packetCtx);
        OSNotificationQueuePost(handle, IOSETCREDIT);
        _set_errno(EPIPE);
        return -1;
    }

    // Reset the message flag so we can properly report status of message.
    streambuffer_read_packet_data(&packet, sizeof(struct packethdr), &packetCtx);
    message->msg_flags = packet.flags;

    // The packet's source remains valid even after a reconnect. Copy only the
    // caller's capacity, but report the full address length on truncation.
    addressBytes = message->msg_name != NULL ? MIN(message->msg_namelen, packet.addresslen) : 0;
    if (addressBytes != 0) {
        streambuffer_read_packet_data(message->msg_name, addressBytes, &packetCtx);
    }
    
    packetCtx._state += packet.addresslen - addressBytes;
    message->msg_namelen = message->msg_name != NULL ? packet.addresslen : 0;

    // Handle control data, and set the appropriate flags and update the actual
    // length read of control data if it is shorter than the buffer provided.
    if (packet.controllen && message->msg_control && message->msg_controllen) {
        size_t bytes_to_copy    = MIN(message->msg_controllen, packet.controllen);
        size_t bytes_to_discard = packet.controllen - bytes_to_copy;

        streambuffer_read_packet_data(message->msg_control, bytes_to_copy, &packetCtx);
        packetCtx._state += bytes_to_discard; // hack
    }
    else {
        packetCtx._state += packet.controllen; // discard, hack
        message->msg_controllen = 0;
    }

    if (packet.controllen > message->msg_controllen) {
        message->msg_flags |= MSG_CTRUNC;
    }

    // Finally read the payload data
    if (packet.payloadlen) {
        size_t bytes_remaining = packet.payloadlen;
        int    iov_not_filled  = 0;

        for (i = 0; i < message->msg_iovlen && bytes_remaining; i++) {
            struct iovec* iov = &message->msg_iov[i];
            if (!iov->iov_len) {
                break;
            }

            size_t bytes_to_copy = MIN(iov->iov_len, bytes_remaining);
            if (iov->iov_len > bytes_to_copy) {
                iov_not_filled = 1;
            }

            streambuffer_read_packet_data(iov->iov_base, bytes_to_copy, &packetCtx);
            bytes_remaining -= bytes_to_copy;
        }
        streambuffer_read_packet_end(&packetCtx);
        OSNotificationQueuePost(handle, IOSETCREDIT);

        // The first special case is when there is more data available than we
        // requested, that means we simply trunc the data.
        if (bytes_remaining) {
            message->msg_flags |= MSG_TRUNC;
        }

        // The second case is a lot more complex, that means we are missing data
        // though we requested more, if WAITALL is set, then we need to keep reading
        // untill we read all the data
        else if (iov_not_filled && !(rwOptions->flags & STREAMBUFFER_ALLOW_PARTIAL)) {
            // However on message-based sockets, we must read datagrams as atomic
            // operations, and thus MSG_WAITALL has no effect as this is effectively
            // the standard mode of operation.
        }
    }
    else {
        streambuffer_read_packet_end(&packetCtx);
        OSNotificationQueuePost(handle, IOSETCREDIT);
        for (i = 0; i < message->msg_iovlen; i++) {
            struct iovec* iov = &message->msg_iov[i];
            iov->iov_len = 0;
        }
    }

    *bytesRecievedOut = packet.payloadlen;
    return OS_EOK;
}

oserr_t
OSSocketRecv(
    _In_  OSHandle_t*    handle,
    _In_  struct msghdr* message,
    _In_  int            flags,
    _Out_ size_t*        bytesRecievedOut)
{
    streambuffer_t*   stream;
    oserr_t           oserr;
    OSAsyncContext_t* asyncContext;

    oserr = OSSocketRecvPipe(handle, &stream);
    if (oserr != OS_EOK) {
        return oserr;
    }

    if (message == NULL) {
        return OS_EINVALPARAMS;
    }

    if (streambuffer_has_option(stream, STREAMBUFFER_DISABLED)) {
        return OS_ELINKINVAL;
    }

    asyncContext = __tls_current()->async_context;
    if (asyncContext) {
        OSAsyncContextInitialize(asyncContext);
    }
    oserr = __RecvMessage(
        handle,
        stream,
        message,
        &(streambuffer_rw_options_t) {
            .flags = __StreambufferFlags(flags),
            .async_context = asyncContext,
            .deadline = NULL
        },
        bytesRecievedOut
    );
    return oserr;
}


static oserr_t
__SocketRelease(
    _In_ struct OSHandle* handle)
{
    struct Socket* socket = handle->Payload;
    oserr_t        status;

    // Release runs only for the final local alias. Each successful stage clears
    // its attachment, so a failed unmap can be retried without repeating work.
    status = SHMRelease(&socket->Send);
    if (status != OS_EOK) {
        return status;
    }

    status = SHMRelease(&socket->Recv);
    if (status != OS_EOK) {
        return status;
    }

    // Close this process's service reference. Netd receives the existing
    // cleanup event only after every process has released its references.
    // Sending a destroy RPC here would also break other processes' sockets.
    status = Syscall_HandleDestroy(handle->ID);
    return status == OS_EINCOMPLETE ? OS_EOK : status;
}

static void
__SocketDestroy(
    _In_ struct OSHandle* handle)
{
    free(handle->Payload);
}
