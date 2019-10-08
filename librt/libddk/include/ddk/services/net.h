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
 * Network Service (Protected) Definitions & Structures
 * - This header describes the base networking-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DDK_SERVICES_NET_H__
#define __DDK_SERVICES_NET_H__

#include <ddk/ddkdefs.h>
#include <ddk/services/service.h>
#include <ddk/streambuffer.h>
#include <inet/socket.h>

#define __NETMANAGER_CREATE_SOCKET      (int)0
#define __NETMANAGER_CLOSE_SOCKET       (int)1
#define __NETMANAGER_BIND_SOCKET        (int)2
#define __NETMANAGER_CONNECT_SOCKET     (int)3
#define __NETMANAGER_ACCEPT_SOCKET      (int)4
#define __NETMANAGER_LISTEN_SOCKET      (int)5
#define __NETMANAGER_SET_SOCKET_OPTION  (int)6
#define __NETMANAGER_GET_SOCKET_OPTION  (int)7
#define __NETMANAGER_GET_SOCKET_ADDRESS (int)8

#define SOCKET_GET_ADDRESS_SOURCE_THIS 0
#define SOCKET_GET_ADDRESS_SOURCE_PEER 1

PACKED_TYPESTRUCT(SocketDescriptorPackage, {
    OsStatus_t Status;
    UUId_t     SocketHandle;
    UUId_t     SendBufferHandle;
    UUId_t     RecvBufferHandle;
});

PACKED_TYPESTRUCT(GetSocketOptionPackage, {
    OsStatus_t Status;
    socklen_t  Length;
    uint8_t    Data[1];
});

PACKED_TYPESTRUCT(GetSocketAddressPackage, {
    OsStatus_t              Status;
    struct sockaddr_storage Address;
});

_CODE_BEGIN

/**
 * CreateSocket
 * * Creates and initializes a new socket of default options, the socket will have
 * * a temporary address that can be changed using the Bind operation.
 * @param Domain
 * @param Type
 * @param Protocol
 * @param HandleOut
 * @param RecvQueueOut
 * @param SendQueueOut
 */
DDKDECL(OsStatus_t,
CreateSocket(
    _In_  int     Domain,
    _In_  int     Type,
    _In_  int     Protocol,
    _Out_ UUId_t* HandleOut,
    _Out_ UUId_t* SendBufferHandleOut,
    _Out_ UUId_t* RecvBufferHandleOut));

/**
 * CloseSocket
 * * Closes the specified socket operations. If a full shutdown has been requested
 * * then all resources are released as well.
 * @param Handle  [In] The socket handle, on which the operation is done.
 * @param Options [In] The supplied shutdown options.
 */
DDKDECL(OsStatus_t,
CloseSocket(
    _In_ UUId_t       Handle,
    _In_ unsigned int Options));

/**
 * BindSocket
 * * Binds the socket to the requested address, this fails if the address is currently
 * * already allocated by another socket.
 * @param Handle  [In] The socket handle, on which the operation is done.
 * @param Address [In] The requested address that the socket should be bound by.
 */
DDKDECL(OsStatus_t,
BindSocket(
    _In_ UUId_t                 Handle,
    _In_ const struct sockaddr* Address));

/**
 * ConnectSocket
 * * Tries to establish a connection to the supplied address. This is only supported
 * * on socket protocols that support connection-modes.
 * @param Handle  [In] The socket handle, on which the operation is done.
 * @param Address [In] The requested address that the socket should connect to.
 */
DDKDECL(OsStatus_t,
ConnectSocket(
    _In_ UUId_t                 Handle,
    _In_ const struct sockaddr* Address));

/**
 * AcceptSocket
 * * Accepts a new socket-client on the given handle, the socket given must be
 * * enabled for this operation by using ListenSocket first.
 * @param Handle  [In] The socket handle, on which the operation is done.
 * @param Address [In] The address of the accepted socket client.
 */
DDKDECL(OsStatus_t,
AcceptSocket(
    _In_ UUId_t           Handle,
    _In_ struct sockaddr* Address));

/**
 * ListenSocket
 * * Enables the socket for listening mode, this will prevent the socket from being
 * * able to connect explicitly to other sockets.
 * @param Handle              [In] The socket handle, on which the operation is done.
 * @param ConnectionQueueSize [In] The number of connections that can be queued up.
 */
DDKDECL(OsStatus_t,
ListenSocket(
    _In_ UUId_t Handle,
    _In_ int    ConnectionQueueSize));

/**
 * SetSocketOption
 * * Sets a socket option for either the socket or a specific protocol.
 * @param Handle     [In] The socket handle, on which the operation is done.
 * @param Protocol   [In] The protocol which the option is relevant for.
 * @param Option     [In] The option identifier.
 * @param Data       [In] The option data.
 * @param DataLength [In] Length of the option data passed.
 */
DDKDECL(OsStatus_t,
SetSocketOption(
    _In_ UUId_t       Handle,
    _In_ int          Protocol,
    _In_ unsigned int Option,
    _In_ const void*  Data,
    _In_ socklen_t    DataLength));

/**
 * GetSocketOption
 * * Gets a socket option for either the socket or a specific protocol.
 * @param Handle     [In]  The socket handle, on which the operation is done.
 * @param Protocol   [In]  The protocol which the option is relevant for.
 * @param Option     [In]  The option identifier.
 * @param Data       [In]  A buffer to store the option data.
 * @param DataLength [Out] Where the length of the data will be stored.
 */
DDKDECL(OsStatus_t,
GetSocketOption(
    _In_ UUId_t       Handle,
    _In_ int          Protocol,
    _In_ unsigned int Option,
    _In_ void*        Data,
    _In_ socklen_t*   DataLength));

/**
 * GetSocketAddress
 * * Retrieves the socket address specified by @Source.
 * @param Handle           [In]  The socket handle, on which the operation is done.
 * @param Source           [In]  The address source, this specified which kind of address that needs to be retrieved.
 * @param Address          [In]  Storage for the address that is retrieved.
 * @param AddressLengthOut [Out] Length of the address retrieved.
 */
DDKDECL(OsStatus_t,
GetSocketAddress(
    _In_    UUId_t           Handle,
    _In_    int              Source,
    _In_    struct sockaddr* Address,
    _InOut_ socklen_t*       AddressLengthOut));

_CODE_END

#endif //!__DDK_SERVICES_NET_H__
