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
#include <ddk/ringbuffer.h>
#include <inet/socket.h>

#define __NETMANAGER_CREATE_SOCKET      (int)0
#define __NETMANAGER_INHERIT_SOCKET     (int)1
#define __NETMANAGER_BIND_SOCKET        (int)2
#define __NETMANAGER_WRITE_SOCKET       (int)3
#define __NETMANAGER_GET_SOCKET_ADDRESS (int)4

_CODE_BEGIN

/**
 * CreateSocket
 * 
 * @param Domain
 * @param Type
 * @param Protocol
 * @param HandleOut
 * @param RecvQueueOut
 */
DDKDECL(OsStatus_t,
CreateSocket(
    _In_  int            Domain,
    _In_  int            Type,
    _In_  int            Protocol,
    _Out_ UUId_t*        HandleOut,
    _Out_ ringbuffer_t** RecvQueueOut,
    _Out_ ringbuffer_t** SendQueueOut));

/**
 * InheritSockets
 * 
 * @param Handle
 * @param RecvQueueOut
 */
DDKDECL(OsStatus_t,
InheritSocket(
    _In_  UUId_t         Handle,
    _Out_ ringbuffer_t** RecvQueueOut,
    _Out_ ringbuffer_t** SendQueueOut));

/**
 * BindSocket
 * 
 * @param Handle
 * @param Address
 */
DDKDECL(OsStatus_t,
BindSocket(
    _In_ UUId_t                   Handle,
    _In_ struct sockaddr_storage* Address));

/**
 * GetSocketAddress
 * 
 * @param Handle
 * @param AddressOut
 * @param AddressLengthOut
 */
DDKDECL(OsStatus_t,
GetSocketAddress(
    _In_  UUId_t                   Handle,
    _Out_ struct sockaddr_storage* AddressOut,
    _Out_ socklen_t*               AddressLengthOut));

_CODE_END

#endif //!__DDK_SERVICES_NET_H__
