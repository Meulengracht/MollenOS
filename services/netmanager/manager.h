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
 * Network Manager
 * - Contains the implementation of the infrastructure in the network
 *   manager. There a lot of different types of sockets, like internet, ipc
 *   and bluetooth to name the popular ones.
 */

#ifndef __NET_MANAGER_H__
#define __NET_MANAGER_H__

#include <os/osdefs.h>
#include <gracht/link/vali.h>
#include <inet/socket.h>

typedef struct Socket Socket_t;
typedef struct SocketDescriptor SocketDescriptor_t;

#define NETWORK_MANAGER_MONITOR_MAX_EVENTS 32

OsStatus_t
NetworkManagerInitialize(void);

OsStatus_t
NetworkManagerSocketCreate(
    _In_  int     Domain,
    _In_  int     Type,
    _In_  int     Protocol,
    _Out_ UUId_t* HandleOut,
    _Out_ UUId_t* SendBufferHandleOut,
    _Out_ UUId_t* RecvBufferHandleOut);

OsStatus_t
NetworkManagerSocketShutdown(
    _In_ UUId_t Handle,
    _In_ int    Options);

OsStatus_t
NetworkManagerSocketBind(
    _In_ UUId_t                 Handle,
    _In_ const struct sockaddr* Address);

OsStatus_t
NetworkManagerSocketConnect(
    _In_ struct gracht_recv_message* message,
    _In_ UUId_t                      handle,
    _In_ const struct sockaddr*      address);

OsStatus_t
NetworkManagerSocketAccept(
    _In_ struct gracht_recv_message* message,
    _In_ UUId_t                      handle);

OsStatus_t
NetworkManagerSocketPair(
    _In_ UUId_t Handle1,
    _In_ UUId_t Handle2);

OsStatus_t
NetworkManagerSocketListen(
    _In_ UUId_t Handle,
    _In_ int    ConnectionCount);

OsStatus_t
NetworkManagerSocketSetOption(
    _In_ UUId_t           Handle,
    _In_ int              Protocol,
    _In_ unsigned int     Option,
    _In_ const void*      Data,
    _In_ socklen_t        DataLength);

OsStatus_t
NetworkManagerSocketGetOption(
    _In_  UUId_t           Handle,
    _In_  int              Protocol,
    _In_  unsigned int     Option,
    _In_  void*            Data,
    _Out_ socklen_t*       DataLengthOut);

OsStatus_t
NetworkManagerSocketGetAddress(
    _In_ UUId_t           Handle,
    _In_ int              Source,
    _In_ struct sockaddr* Address);

Socket_t*
NetworkManagerSocketGet(
    _In_ UUId_t Handle);

#endif //!__NET_MANAGER_H__
