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

#include "socket.h"

OsStatus_t
SocketCreateImpl(
    _In_  UUId_t  ProcessHandle,
    _In_  int     Domain,
    _In_  int     Type,
    _In_  int     Protocol,
    _Out_ UUId_t* HandleOut,
    _Out_ UUId_t* SendBufferHandleOut,
    _Out_ UUId_t* RecvBufferHandleOut)
{
    return OsNotSupported;
}

OsStatus_t
SocketInheritImpl(
    _In_  UUId_t  ProcessHandle,
    _In_  UUId_t  Handle,
    _Out_ UUId_t* SendBufferHandleOut,
    _Out_ UUId_t* RecvBufferHandleOut)
{
    return OsNotSupported;
}

OsStatus_t
SocketShutdownImpl(
    _In_ UUId_t ProcessHandle,
    _In_ UUId_t Handle,
    _In_ int    Options)
{
    return OsNotSupported;
}
    
OsStatus_t
SocketBindImpl(
    _In_ UUId_t                 ProcessHandle,
    _In_ UUId_t                 Handle,
    _In_ const struct sockaddr* Address)
{
    return OsNotSupported;
}

OsStatus_t
SocketConnectImpl(
    _In_ UUId_t                 ProcessHandle,
    _In_ UUId_t                 Handle,
    _In_ const struct sockaddr* Address)
{
    return OsNotSupported;
}

OsStatus_t
SocketAcceptImpl(
    _In_ UUId_t           ProcessHandle,
    _In_ UUId_t           Handle,
    _In_ struct sockaddr* Address)
{
    return OsNotSupported;
}


OsStatus_t
SocketListenImpl(
    _In_ UUId_t ProcessHandle,
    _In_ UUId_t Handle,
    _In_ int    ConnectionCount)
{
    return OsNotSupported;
}

OsStatus_t
GetSocketAddressImpl(
    _In_ UUId_t           ProcessHandle,
    _In_ UUId_t           Handle,
    _In_ struct sockaddr* Address)
{
    return OsNotSupported;
}
