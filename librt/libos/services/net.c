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
#include <internal/_tls.h>
#include <internal/_utils.h>
#include <os/handle.h>
#include <os/notification_queue.h>
#include <os/shm.h>
#include <os/services/net.h>
#include <os/mutex.h>
#include <sys_socket_service_client.h>

struct Socket {
    int                     Type;
    struct sockaddr_storage ConnectedAddress;
    OSHandle_t              Send;
    bool                    SendMapped;
    OSHandle_t              Recv;
    bool                    RecvMapped;
    Mutex_t                 Lock;
};

static size_t __SocketExport(struct OSHandle*, void*);
static size_t __SocketImport(struct OSHandle*, const void*);
static void   __SocketDestroy(struct OSHandle*);

const OSHandleOps_t g_socketOps = {
        .Deserialize = __SocketImport,
        .Serialize = __SocketExport,
        .Destroy = __SocketDestroy
};

struct Socket*
__SocketNew(
        _In_ int    domain,
        _In_ int    type,
        _In_ int    protocol,
        _In_ uuid_t sendHandleID,
        _In_ uuid_t recvHandleID)
{
    struct Socket* socket;

    socket = malloc(sizeof(struct Socket));
    if (socket == NULL) {
        return NULL;
    }

    memset(socket, 0, sizeof(struct Socket));
    socket->Type = type;
    socket->Send.ID = sendHandleID;
    socket->Recv.ID = recvHandleID;
    MutexInitialize(&socket->Lock, MUTEX_PLAIN);
    return socket;
}

static void
__SocketClose(
        _In_ uuid_t handle)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    oserr_t                  oserr;

    sys_socket_close(GetGrachtClient(), &msg.base, handle, SYS_CLOSE_OPTIONS_DESTROY);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_socket_close_result(GetGrachtClient(), &msg.base, &oserr);
}

static oserr_t
__SocketAttachPipes(
        _In_ struct Socket* socket)
{
    oserr_t oserr;

    oserr = SHMAttach(socket->Recv.ID, &socket->Recv);
    if (oserr != OS_EOK) {
        socket->Recv.ID = UUID_INVALID;
        socket->Send.ID = UUID_INVALID;
        return oserr;
    }

    oserr = SHMAttach(socket->Recv.ID, &socket->Recv);
    if (oserr != OS_EOK) {
        OSHandleDestroy(&socket->Recv);
        socket->Recv.ID = UUID_INVALID;
        socket->Send.ID = UUID_INVALID;
    }
    return oserr;
}

oserr_t
OSSocketOpen(
        _In_  int         domain,
        _In_  int         type,
        _In_  int         protocol,
        _Out_ OSHandle_t* handleOut)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    struct Socket*           socket;
    oserr_t                  oserr;
    uuid_t                   handleID;
    uuid_t                   send_handle;
    uuid_t                   recv_handle;

    // We need to create the socket object at kernel level, as we need
    // kernel assisted functionality to support a centralized storage of
    // all system sockets. They are the foundation of the microkernel for
    // communication between processes and are needed long before anything else.
    sys_socket_create(GetGrachtClient(), &msg.base, domain, type, protocol);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_socket_create_result(GetGrachtClient(), &msg.base, &oserr, &handleID, &recv_handle, &send_handle);
    if (oserr != OS_EOK) {
        return oserr;
    }

    socket = __SocketNew(domain, type, protocol, send_handle, recv_handle);
    if (socket == NULL) {
        __SocketClose(handleID);
        return OS_EOOM;
    }

    oserr = __SocketAttachPipes(socket);
    if (socket == NULL) {
        __SocketClose(handleID);
        return oserr;
    }

    oserr = OSHandleWrap(
            handleID,
            OSHANDLE_SOCKET,
            NULL,
            true,
            handleOut
    );
    if (oserr != OS_EOK) {
        __SocketClose(handleID);
        free(socket);
        return oserr;
    }
    return OS_EOK;
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
    struct Socket*           source, *accepted;
    uuid_t                   handleID;
    uuid_t                   send_handle;
    uuid_t                   recv_handle;
    oserr_t                  oserr;

    if (handle == NULL) {
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

    sys_socket_accept(GetGrachtClient(), &msg.base, handle->ID);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_socket_accept_result(
            GetGrachtClient(),
            &msg.base,
            &oserr,
            (uint8_t*)address,
            *addressLength,
            &handleID,
            &recv_handle,
            &send_handle
    );
    if (oserr != OS_EOK) {
        return OsErrToErrNo(oserr);
    }

    accepted = __SocketNew(0, source->Type, 0, send_handle, recv_handle);
    if (accepted == NULL) {
        __SocketClose(handleID);
        return OS_EOOM;
    }

    oserr = __SocketAttachPipes(accepted);
    if (accepted == NULL) {
        __SocketClose(handleID);
        return oserr;
    }

    oserr = OSHandleWrap(
            handleID,
            OSHANDLE_SOCKET,
            NULL,
            true,
            handleOut
    );
    if (oserr != OS_EOK) {
        __SocketClose(handleID);
        free(accepted);
        return oserr;
    }
    *addressLength = (socklen_t)(uint32_t)address->sa_len;
    return OS_EOK;
}

oserr_t
OSSocketBind(
        _In_ OSHandle_t*            handle,
        _In_ const struct sockaddr* address,
        _In_ socklen_t              addressLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    oserr_t                  oserr;

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
    struct Socket*           socket;
    oserr_t                  oserr;

    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }

    // Accept is only valid on handles that are of socket type
    if (handle->Type != OSHANDLE_SOCKET) {
        return OS_ENOTSUPPORTED;
    }

    socket = handle->Payload;
    sys_socket_connect(
            GetGrachtClient(),
            &msg.base,
            handle->ID,
            (const uint8_t*)address,
            addressLength
    );
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_socket_connect_result(GetGrachtClient(), &msg.base, &oserr);
    if (oserr == OS_EOK) {
        memcpy(&socket->ConnectedAddress, address, addressLength);
    }
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
    oserr_t                  oserr;

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
            GetGrachtClient(),
            &msg.base,
            &oserr,
            (uint8_t*)address,
            addressMaxSize
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
    oserr_t                  oserr;

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
    oserr_t                  oserr;

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
    oserr_t                  oserr;
    int                      sizeOfData;

    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }

    if (handle->Type != OSHANDLE_SOCKET) {
        return OS_ENOTSUPPORTED;
    }

    sys_socket_get_option(GetGrachtClient(), &msg.base, handle->ID, protocol, option);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_socket_get_option_result(
            GetGrachtClient(),
            &msg.base,
            &oserr,
            data,
            (uint32_t)(*length),
            &sizeOfData
    );
    if (oserr == OS_EOK) {
        *length = (socklen_t)sizeOfData;
    }
    return oserr;
}

oserr_t
OSSocketSendPipe(
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
    // been mapped by a previous call to OSSocketSendPipe, or it has been
    // inheritted).
    MutexLock(&socket->Lock);
    if (socket->SendMapped) {
        *pipeOut = SHMBuffer(&socket->Send);
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
    *pipeOut = SHMBuffer(&socket->Send);

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
    size_t           bytesSent   = 0;
    size_t           payload_len = 0;
    size_t           meta_len    = sizeof(struct packethdr) + message->msg_namelen + message->msg_controllen;
    struct packethdr packet;
    size_t           avail_len;
    oserr_t          oserr;
    streambuffer_packet_ctx_t packetCtx;
    int              i;

    // Writing an packet is an atomic action and the entire packet must be written
    // at once. So don't support STREAMBUFFER_ALLOW_PARTIAL
    rwOptions->flags &= ~(STREAMBUFFER_ALLOW_PARTIAL);

    // Otherwise we must build a packet, to do this we need to know the entire
    // length of the message before committing.
    for (i = 0; i < message->msg_iovlen; i++) {
        payload_len += message->msg_iov[i].iov_len;
    }

    packet.flags = flags & (MSG_OOB | MSG_DONTROUTE);
    packet.controllen = (message->msg_control != NULL) ? message->msg_controllen : 0;
    packet.addresslen = (message->msg_name != NULL) ? message->msg_namelen : 0;
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

    for (i = 0; i < message->msg_iovlen; i++) {
        struct iovec* iov = &message->msg_iov[i];
        size_t        count = MIN(avail_len - meta_len, iov->iov_len);
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
    struct Socket*  socket;
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

    // Lastly, make sure we actually have a destination address. For the rest of
    // the socket types, we use the stored address ('connected address'), or the
    // one provided.
    socket = handle->Payload;
    if (socket->Type == SOCK_STREAM || socket->Type == SOCK_SEQPACKET) {
        // TODO return ENOTCONN / EISCONN before writing data. Lack of effecient way
    } else {
        if (message->msg_name == NULL) {
            if (socket->ConnectedAddress.__ss_family == AF_UNSPEC) {
                return OS_ENOTCONNECTED;
            }
            struct msghdr* msg_ptr = (struct msghdr*)message;
            msg_ptr->msg_name    = &socket->ConnectedAddress;
            msg_ptr->msg_namelen = socket->ConnectedAddress.__ss_len;
        }
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
        goto exit;
    }

    oserr = SHMAttach(socket->Recv.ID, &socket->Recv);
    if (oserr != OS_EOK){
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
        _In_  streambuffer_t*            stream,
        _In_  struct msghdr*             message,
        _In_  streambuffer_rw_options_t* rwOptions,
        _Out_ size_t*                    bytesRecievedOut)
{
    size_t bytesRecieved = 0;
    int    i;

    for (i = 0; i < message->msg_iovlen; i++) {
        struct iovec* iov = &message->msg_iov[i];
        bytesRecieved += (intmax_t)streambuffer_stream_in(
                stream,
                iov->iov_base,
                iov->iov_len,
                rwOptions
        );
        if (bytesRecieved < iov->iov_len) {
            if (!(rwOptions->flags & STREAMBUFFER_NO_BLOCK)) {
                return OS_ELINKINVAL;
            }
            break;
        }
    }
    *bytesRecievedOut = bytesRecieved;
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
    streambuffer_packet_ctx_t packetCtx;

    // In case of stream sockets we simply just read as many bytes as requested
    // or available and return, unless WAITALL has been specified.
    if (socket->Type == SOCK_STREAM) {
        return __RecvStream(stream, message, rwOptions, bytesRecievedOut);
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
        _set_errno(EPIPE);
        return -1;
    }

    // Reset the message flag so we can properly report status of message.
    streambuffer_read_packet_data(&packet, sizeof(struct packethdr), &packetCtx);
    message->msg_flags = packet.flags;

    // Handle the source address that is given in the packet
    if (packet.addresslen && message->msg_name && message->msg_namelen) {
        size_t bytes_to_copy    = MIN(message->msg_namelen, packet.addresslen);
        size_t bytes_to_discard = packet.addresslen - bytes_to_copy;

        streambuffer_read_packet_data(message->msg_name, bytes_to_copy, &packetCtx);
        packetCtx._state += bytes_to_discard; // hack
    }
    else {
        packetCtx._state += packet.addresslen; // discard, hack
        message->msg_namelen = 0;
    }

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
    struct Socket*    socket;
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

    socket = handle->Payload;
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
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Fill in the source address if one was provided already, and overwrite
    // the one provided by the packet.
    if (*bytesRecievedOut > 0 && message->msg_name != NULL) {
        if (socket->ConnectedAddress.__ss_family != AF_UNSPEC) {
            memcpy(message->msg_name, &socket->ConnectedAddress,
                   socket->ConnectedAddress.__ss_len);
            message->msg_namelen = socket->ConnectedAddress.__ss_len;
        }
    }
    return OS_EOK;
}


static size_t
__SocketExport(
        _In_ struct OSHandle* handle,
        _In_ void*            data)
{
    struct Socket* socket = handle->Payload;
    uint8_t*       data8 = data;

    // When exporting we only save the connected address and
    // the type
    *((int*)&data8[0]) = socket->Type;
    *((uuid_t*)&data8[sizeof(int)]) = socket->Send.ID;
    *((uuid_t*)&data8[sizeof(int) + sizeof(uuid_t)]) = socket->Send.ID;
    memcpy(
            &data8[sizeof(int) + (2 * sizeof(uuid_t))],
            &socket->ConnectedAddress,
            sizeof(struct sockaddr_storage)
    );
    return sizeof(int) + (2 * sizeof(uuid_t)) + sizeof(struct sockaddr_storage);
}

static size_t
__SocketImport(
        _In_ struct OSHandle* handle,
        _In_ const void*      data)
{
    struct Socket* socket;
    const uint8_t* data8 = data;
    int            type;
    uuid_t         sendID, recvID;

    type = *((int*)&data8[0]);
    sendID = *((uuid_t*)&data8[sizeof(int)]);
    recvID = *((uuid_t*)&data8[sizeof(int) + sizeof(uuid_t)]);

    // When importing, we do a few extra steps. We need to recreate and setup
    // the Socket data-structure.
    socket = __SocketNew(0, type, 0, sendID, recvID);
    if (socket == NULL) {
        goto exit;
    }

    // When the SHM buffers are imported, they are automatically attached to, they are however
    // not mapped.
    memcpy(
            &socket->ConnectedAddress,
            &data[sizeof(int) + (2 * sizeof(uuid_t))],
            sizeof(struct sockaddr_storage)
    );

exit:
    return sizeof(int) + (2 * sizeof(uuid_t)) + sizeof(struct sockaddr_storage);
}

static void
__SocketDestroy(struct OSHandle* handle)
{
    struct Socket* socket = handle->Payload;
    OSHandleDestroy(&socket->Send);
    OSHandleDestroy(&socket->Recv);
    __SocketClose(handle->ID);
}
