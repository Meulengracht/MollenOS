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

#include <ddk/service.h>
#include <gracht/client.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/handle.h>
#include <os/services/net.h>
#include <sys_socket_service_client.h>

struct Socket {
    int                     Type;
    struct sockaddr_storage ConnectedAddress;
    OSHandle_t              Send;
    OSHandle_t              Recv;
};

static void __SocketDestroy(struct OSHandle*);

const OSHandleOps_t g_socketOps = {
        .Destroy = __SocketDestroy
};

struct Socket*
__SocketNew(
        _In_  int domain,
        _In_  int type,
        _In_  int protocol)
{

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

    socket = __SocketNew(domain, type, protocol);
    if (socket == NULL) {
        return OS_EOOM;
    }

    // We need to create the socket object at kernel level, as we need
    // kernel assisted functionality to support a centralized storage of
    // all system sockets. They are the foundation of the microkernel for
    // communication between processes and are needed long before anything else.
    sys_socket_create(GetGrachtClient(), &msg.base, domain, type, protocol);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_socket_create_result(GetGrachtClient(), &msg.base, &oserr, &handleID, &recv_handle, &send_handle);
    if (oserr != OS_EOK) {
        free(socket);
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
    struct Socket*           source;
    uuid_t                   socket_handle;
    uuid_t                   send_handle;
    uuid_t                   recv_handle;
    oserr_t                  status;

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
            &status,
            (uint8_t*)address,
            *addressLength,
            &socket_handle,
            &recv_handle,
            &send_handle
    );
    if (status != OS_EOK) {
        OsErrToErrNo(status);
        return -1;
    }

    *addressLength = (socklen_t)(uint32_t)address->sa_len;

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
    struct vali_link_message msg    = VALI_MSG_INIT_HANDLE(GetNetService());
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

static void
__SocketDestroy(struct OSHandle*)
{

}
