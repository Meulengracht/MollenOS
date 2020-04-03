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

#include <ds/streambuffer.h>
#include <ds/rbtree.h>
#include <ds/queue.h>
#include <inet/socket.h>
#include <os/dmabuf.h>
#include <os/osdefs.h>

#define SOCKET_DEFAULT_BUFFER_SIZE (16 * 4096)
#define SOCKET_SYSMAX_BUFFER_SIZE  (256 * 4096)

typedef struct SocketConfiguration {
    unsigned int Blocking   : 1;
    unsigned int Passive    : 1;
    unsigned int Bound      : 1;
    unsigned int Connected  : 1;
    unsigned int Connecting : 1;
    unsigned int Unused     : 27;
    int          Backlog;
} SocketConfiguration_t;

typedef struct QueuedPacket {
    size_t Length;
    void*  Data;
} QueuedPacket_t;

typedef struct SocketPipe {
    struct dma_attachment DmaAttachment;
    streambuffer_t*       Stream;
} SocketPipe_t;

typedef struct Socket {
    rb_leaf_t             Header;
    _Atomic(int)          PendingPackets;
    int                   DomainType;
    int                   Type;
    int                   Protocol;
    SocketConfiguration_t Configuration;
    
    mtx_t                 SyncObject;
    //NetworkAdapter_t*   Adapter;
    SocketDomain_t*       Domain;
    SocketPipe_t          Send;
    SocketPipe_t          Receive;
    QueuedPacket_t        QueuedPacket;
    queue_t               ConnectionRequests;
    queue_t               AcceptRequests;
} Socket_t;

/* SocketCreateImpl
 * Creates and initializes a new socket of default options. The socket
 * will be assigned an temporary address, and resource will be allocated. */
OsStatus_t
SocketCreateImpl(
    _In_  int              Domain,
    _In_  int              Type,
    _In_  int              Protocol,
    _Out_ Socket_t**       SocketOut);

/* SocketShutdownImpl
 * Shutsdown or closes certain aspects (or all) of a socket. This will also
 * close down any active connections, and notify of disconnect. */
OsStatus_t
SocketShutdownImpl(
    _In_ Socket_t* Socket,
    _In_ int       Options);

/* SocketListenImpl
 * Marks the socket as passive, and enables the use of accept operation. The
 * ConnectionCount parameter controls the number of connection requests that can
 * be queued up. */
OsStatus_t
SocketListenImpl(
    _In_ Socket_t* Socket,
    _In_ int       ConnectionCount);

/* SetSocketOptionImpl
 * Sets the option given for the protocol given. The option data and length must
 * be specified. */
OsStatus_t
SetSocketOptionImpl(
    _In_ Socket_t*        Socket,
    _In_ int              Protocol,
    _In_ unsigned int     Option,
    _In_ const void*      Data,
    _In_ socklen_t        DataLength);

/* GetSocketOptionImpl
 * Retrieves the option of the specified protocol for the socket handle given. The
 * data will be returned in the provided buffer, and the length specified. */
OsStatus_t
GetSocketOptionImpl(
    _In_ Socket_t*         Socket,
    _In_  int              Protocol,
    _In_  unsigned int     Option,
    _In_  void*            Data,
    _Out_ socklen_t*       DataLengthOut);

streambuffer_t*
GetSocketSendStream(
    _In_ Socket_t* Socket);

streambuffer_t*
GetSocketRecvStream(
    _In_ Socket_t* Socket);

OsStatus_t
SocketSetQueuedPacket(
    _In_ Socket_t*   Socket,
    _In_ const void* Payload,
    _In_ size_t      Length);

size_t
SocketGetQueuedPacket(
    _In_ Socket_t* Socket,
    _In_ void**    Buffer);

#endif //!__NETMANAGER_SOCKET_H__
