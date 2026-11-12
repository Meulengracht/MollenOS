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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Network Manager (Socket interface)
 * - Contains the implementation of the socket infrastructure in the network
 *   manager. There a lot of different types of sockets, like internet, ipc
 *   and bluetooth to name the popular ones.
 */
 
#ifndef __NETMANAGER_SOCKET_H__
#define __NETMANAGER_SOCKET_H__

#include "domains/domains.h"
#include <ds/streambuffer.h>
#include <ds/rbtree.h>
#include <ds/queue.h>
#include <inet/socket.h>
#include <os/types/handle.h>
#include <os/types/shm.h>
#include <os/types/grants.h>

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
    OSHandle_t      SHM;
    SHMTransfer_t   Transfer;
    streambuffer_t* Stream;
} SocketPipe_t;

typedef struct SocketWorkQueue {
    struct Socket* Head;
    struct Socket* Tail;
} SocketWorkQueue_t;

typedef struct SocketReferences {
    OSHandle_t Initial;
    uuid_t     ExportControl;
} SocketReferences_t;

typedef struct Socket {
    rb_leaf_t             Header;
    OSHandle_t            Handle;
    SocketReferences_t    References;
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
    
    // A socket can be queued once, for work or to wait for receive space.
    SocketWorkQueue_t*    WorkQueue;
    struct Socket*        WorkPrevious;
    struct Socket*        WorkNext;
    SocketWorkQueue_t     ReceiveWaiters;
    
    queue_t               ConnectionRequests;
    queue_t               AcceptRequests;
} Socket_t;

// Implementation entry points require the manager socket execution lock.
// This protects socket and domain lifetimes in addition to per-socket state.

/**
 * @brief Creates a socket with default options, buffers and a temporary address.
 *
 * @param Domain The socket domain.
 * @param Registration Netd's handle-service registration.
 * @param Type The socket type.
 * @param Protocol The protocol within the domain.
 * @param SocketOut Receives the socket on success.
 * @return OS_EOK on success, or an error from socket setup.
 */
oserr_t
SocketCreateImpl(
    _In_  OSHandle_t*     Registration,
    _In_  int              Domain,
    _In_  int              Type,
    _In_  int              Protocol,
    _Out_ Socket_t**       SocketOut);

/**
 * @brief Disconnects and frees a socket when Options is SYS_CLOSE_OPTIONS_DESTROY.
 * Closing only the read or write side is not supported yet.
 *
 * @param Socket The socket to close.
 * @param Options The requested close operation.
 * @return OS_EOK if destroyed, OS_ENOTSUPPORTED for a partial close, or
 *         OS_EINVALPARAMS if Options is invalid.
 */
oserr_t
SocketShutdownImpl(
    _In_ Socket_t* Socket,
    _In_ int       Options);

/**
 * @brief Marks a socket as passive so it can accept connections.
 *
 * @param Socket The socket to listen on.
 * @param ConnectionCount Maximum queued connection requests. Must be nonnegative.
 * @return OS_EOK on success, or OS_EINVALPARAMS if ConnectionCount is negative.
 */
oserr_t
SocketListenImpl(
    _In_ Socket_t* Socket,
    _In_ int       ConnectionCount);

/**
 * @brief Sets a protocol option. Not implemented yet.
 *
 * @param Socket The socket to configure.
 * @param Protocol The protocol the option belongs to.
 * @param Option The option to set.
 * @param Data The option value.
 * @param DataLength Number of bytes in Data.
 * @return OS_ENOTSUPPORTED.
 */
oserr_t
SetSocketOptionImpl(
    _In_ Socket_t*        Socket,
    _In_ int              Protocol,
    _In_ unsigned int     Option,
    _In_ const void*      Data,
    _In_ socklen_t        DataLength);

/**
 * @brief Reads a protocol option. Not implemented yet.
 *
 * @param Socket The socket to query.
 * @param Protocol The protocol the option belongs to.
 * @param Option The option to read.
 * @param Data Buffer for the option value.
 * @param DataLengthOut Receives the value's length when supported.
 * @return OS_ENOTSUPPORTED. The output arguments are unchanged.
 */
oserr_t
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

oserr_t
SocketSetQueuedPacket(
    _In_ Socket_t*   Socket,
    _In_ const void* Payload,
    _In_ size_t      Length);

size_t
SocketGetQueuedPacket(
    _In_ Socket_t* Socket,
    _In_ void**    Buffer);

#endif //!__NETMANAGER_SOCKET_H__
