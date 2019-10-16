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
 * Network Manager (Domain Handler)
 * - Contains the implementation of the socket domain type in the network
 *   manager. There a lot of different types of sockets, like internet, ipc
 *   and bluetooth to name the popular ones.
 */

#ifndef __NETMANAGER_DOMAINS_H__
#define __NETMANAGER_DOMAINS_H__

#include <os/osdefs.h>

struct sockaddr;
typedef struct Socket Socket_t;
typedef struct SocketDomain SocketDomain_t;

typedef OsStatus_t (*DomainAllocateAddressFn)(Socket_t*, struct sockaddr*);
typedef void       (*DomainFreeAddressFn)(Socket_t*);
typedef OsStatus_t (*DomainBindFn)(Socket_t*, const struct sockaddr*);
typedef OsStatus_t (*DomainConnectFn)(Socket_t*, const struct sockaddr*);
typedef OsStatus_t (*DomainAcceptConnectionFn)(Socket_t*, struct sockaddr*);
typedef OsStatus_t (*DomainSendFn)(Socket_t*);
typedef OsStatus_t (*DomainReceiveFn)(Socket_t*);
typedef void       (*DomainDestroyFn)(SocketDomain_t*);

typedef struct SocketDomainOps {
    DomainAllocateAddressFn  AddressAllocate;
    DomainFreeAddressFn      AddressFree;
    DomainBindFn             Bind;
    DomainConnectFn          Connect;
    DomainAcceptConnectionFn Accept;
    DomainSendFn             Send;
    DomainReceiveFn          Receive;
    DomainDestroyFn          Destroy;
} SocketDomainOps_t;

OsStatus_t
DomainCreate(
    _In_ int              DomainType,
    _In_ SocketDomain_t** DomainOut);

void
DomainDestroy(
    _In_ SocketDomain_t* Domain);

OsStatus_t
DomainAllocateAddress(
    _In_ Socket_t*        Socket, 
    _In_ struct sockaddr* Address);

OsStatus_t
DomainUpdateAddress(
    _In_ Socket_t*              Socket,
    _In_ const struct sockaddr* Address);

void
DomainFreeAddress(
    _In_ Socket_t* Socket);
    
OsStatus_t
DomainConnect(
    _In_ Socket_t*              Socket,
    _In_ const struct sockaddr* Address);

OsStatus_t
DomainAcceptConnection(
    _In_ Socket_t*        Socket, 
    _In_ struct sockaddr* Address);

OsStatus_t
DomainSend(
    _In_ Socket_t* Socket);

OsStatus_t
DomainReceive(
    _In_ Socket_t* Socket);

#endif //!__NETMANAGER_DOMAINS_H__
