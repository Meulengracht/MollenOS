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
#define __NETMANAGER_INHERIT_SOCKET     (int)1
#define __NETMANAGER_CLOSE_SOCKET       (int)2
#define __NETMANAGER_BIND_SOCKET        (int)3
#define __NETMANAGER_CONNECT_SOCKET     (int)4
#define __NETMANAGER_ACCEPT_SOCKET      (int)5
#define __NETMANAGER_LISTEN_SOCKET      (int)6
#define __NETMANAGER_SET_SOCKET_OPTION  (int)7
#define __NETMANAGER_GET_SOCKET_OPTION  (int)8
#define __NETMANAGER_GET_SOCKET_ADDRESS (int)9

PACKED_TYPESTRUCT(SocketDescriptorPackage, {
    OsStatus_t Status;
    UUId_t     SocketHandle;
    UUId_t     SendBufferHandle;
    UUId_t     RecvBufferHandle;
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
 */
DDKDECL(OsStatus_t,
CreateSocket(
    _In_  int              Domain,
    _In_  int              Type,
    _In_  int              Protocol,
    _Out_ UUId_t*          HandleOut,
    _Out_ streambuffer_t** RecvQueueOut,
    _Out_ streambuffer_t** SendQueueOut));

/**
 * InheritSocket
 * 
 * @param Handle
 * @param RecvQueueOut
 */
DDKDECL(OsStatus_t,
InheritSocket(
    _In_  UUId_t           Handle,
    _Out_ streambuffer_t** RecvQueueOut,
    _Out_ streambuffer_t** SendQueueOut));

/**
 * CloseSocket
 * 
 * @param Handle
 * @param RecvQueueOut
 */
DDKDECL(OsStatus_t,
CloseSocket(
    _In_ UUId_t       Handle,
    _In_ unsigned int Options));

/**
 * BindSocket
 * 
 * @param Handle
 * @param Address
 */
DDKDECL(OsStatus_t,
BindSocket(
    _In_ UUId_t                 Handle,
    _In_ const struct sockaddr* Address));

/**
 * ConnectSocket
 * 
 * @param Handle
 * @param Address
 */
DDKDECL(OsStatus_t,
ConnectSocket(
    _In_ UUId_t                 Handle,
    _In_ const struct sockaddr* Address));

/**
 * AcceptSocket
 * 
 * @param Handle
 * @param Address
 */
DDKDECL(OsStatus_t,
AcceptSocket(
    _In_ UUId_t           Handle,
    _In_ struct sockaddr* Address));

/**
 * ListenSocket
 * 
 * @param Handle
 * @param ConnectionQueueSize
 */
DDKDECL(OsStatus_t,
ListenSocket(
    _In_ UUId_t Handle,
    _In_ int    ConnectionQueueSize));

/**
 * GetSocketAddress
 * 
 * @param Handle
 * @param AddressOut
 * @param AddressLengthOut
 */
DDKDECL(OsStatus_t,
GetSocketAddress(
    _In_    UUId_t           Handle,
    _In_    struct sockaddr* Address,
    _InOut_ socklen_t*       AddressLengthOut));

_CODE_END

#endif //!__DDK_SERVICES_NET_H__
