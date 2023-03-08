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
#include <os/types/net.h>
#include <sys_socket_service_client.h>

struct Socket {
    OSHandle_t Send;
    OSHandle_t Recv;
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
OSSocketAccept(
        _In_  OSHandle_t*      handle,
        _In_  struct sockaddr* address,
        _In_  socklen_t*       addressLength,
        _Out_ OSHandle_t*      handleOut)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    uuid_t                   socket_handle;
    uuid_t                   send_handle;
    uuid_t                   recv_handle;
    oserr_t                  status;
    int                      accept_iod;

    if (!handle) {
        return -1;
    }

    if (handle->object.type != STDIO_HANDLE_SOCKET) {
        _set_errno(ENOTSOCK);
        return -1;
    }

    if (handle->object.data.socket.type != SOCK_SEQPACKET &&
        handle->object.data.socket.type != SOCK_STREAM) {
        _set_errno(ESOCKTNOSUPPORT);
        return -1;
    }

    sys_socket_accept(GetGrachtClient(), &msg.base, handle->object.handle);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_socket_accept_result(GetGrachtClient(), &msg.base, &status, (uint8_t*)address, *addressLength,
                             &socket_handle, &recv_handle, &send_handle);
    if (status != OS_EOK) {
        OsErrToErrNo(status);
        return -1;
    }

    *addressLength = (socklen_t)(uint32_t)address->sa_len;

}

static void
__SocketDestroy(struct OSHandle*)
{

}
