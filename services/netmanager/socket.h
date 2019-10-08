/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Network Manager (Socket interface)
 * - Contains the implementation of the socket infrastructure in the network
 *   manager. There a lot of different types of sockets, like internet, ipc
 *   and bluetooth to name the popular ones.
 */
 
#ifndef __NETMANAGER_SOCKET_H__
#define __NETMANAGER_SOCKET_H__

#include <ddk/streambuffer.h>
#include <inet/socket.h>
#include <os/dmabuf.h>
#include <os/osdefs.h>

#define SOCKET_DEFAULT_BUFFER_SIZE (16 * 4096)

typedef struct {
    struct dma_attachment DmaAttachment;
    streambuffer_t*       Stream;
} SocketPipe_t;

typedef struct Socket {
    UUId_t           Handle;
    int              Domain;
    int              Type;
    int              Protocol;
    Flags_t          Flags;
    
    struct sockaddr_storage Address;
    SocketPipe_t            Receive;
    SocketPipe_t            Send;
} Socket_t;

/* SocketCreateImpl
 * Creates and initializes a new socket of default options. The socket
 * will be assigned an temporary address, and resource will be allocated. */
OsStatus_t
SocketCreateImpl(
    _In_  UUId_t  ProcessHandle,
    _In_  int     Domain,
    _In_  int     Type,
    _In_  int     Protocol,
    _Out_ UUId_t* HandleOut,
    _Out_ UUId_t* SendBufferHandleOut,
    _Out_ UUId_t* RecvBufferHandleOut);

/* SocketInheritImpl
 * Inherits a socket by another application than the owner, this will increase refcount
 * and pass on the resource handles. */
OsStatus_t
SocketInheritImpl(
    _In_  UUId_t  ProcessHandle,
    _In_  UUId_t  Handle,
    _Out_ UUId_t* SendBufferHandleOut,
    _Out_ UUId_t* RecvBufferHandleOut);

/* SocketShutdownImpl
 * Shutsdown or closes certain aspects (or all) of a socket. This will also
 * close down any active connections, and notify of disconnect. */
OsStatus_t
SocketShutdownImpl(
    _In_ UUId_t ProcessHandle,
    _In_ UUId_t Handle,
    _In_ int    Options);

/* SocketBindImpl
 * Binds a socket to an address and allow others to 'look it up' for 
 * communication. This must be performed before certain other operations. */
OsStatus_t
SocketBindImpl(
    _In_ UUId_t                 ProcessHandle,
    _In_ UUId_t                 Handle,
    _In_ const struct sockaddr* Address);

/* SocketConnectImpl
 * Connect the socket to an address. If any socket is listening on the address
 * the socket will be passed on to the listening party. */
OsStatus_t
SocketConnectImpl(
    _In_ UUId_t                 ProcessHandle,
    _In_ UUId_t                 Handle,
    _In_ const struct sockaddr* Address);

/* SocketAcceptImpl
 * Accepts an incoming connection on the socket. If none are available it will
 * block the request untill one arrives (If configured to do so). */
OsStatus_t
SocketAcceptImpl(
    _In_ UUId_t           ProcessHandle,
    _In_ UUId_t           Handle,
    _In_ struct sockaddr* Address);

/* SocketListenImpl
 * Marks the socket as passive, and enables the use of accept operation. The
 * ConnectionCount parameter controls the number of connection requests that can
 * be queued up. */
OsStatus_t
SocketListenImpl(
    _In_ UUId_t ProcessHandle,
    _In_ UUId_t Handle,
    _In_ int    ConnectionCount);

/* GetSocketAddressImpl
 * Retrieves the address of the given socket handle. */
OsStatus_t
GetSocketAddressImpl(
    _In_ UUId_t           ProcessHandle,
    _In_ UUId_t           Handle,
    _In_ struct sockaddr* Address);

#endif //!__NETMANAGER_SOCKET_H__
