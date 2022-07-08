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
 * Network Manager (Domain Handler)
 * - Contains the implementation of the socket domain type in the network
 *   manager. There a lot of different types of sockets, like internet, ipc
 *   and bluetooth to name the popular ones.
 */

#ifndef __NETMANAGER_DOMAINS_H__
#define __NETMANAGER_DOMAINS_H__

#include <os/osdefs.h>
#include <gracht/link/vali.h>
#include <threads.h>

struct sockaddr;
typedef struct Socket Socket_t;
typedef struct SocketDomain SocketDomain_t;

typedef oserr_t (*DomainAllocateAddressFn)(Socket_t*);
typedef void       (*DomainFreeAddressFn)(Socket_t*);
typedef oserr_t (*DomainBindFn)(Socket_t*, const struct sockaddr*);
typedef oserr_t (*DomainConnectFn)(struct gracht_message*, Socket_t*, const struct sockaddr*);
typedef oserr_t (*DomainDisconnectFn)(Socket_t*);
typedef oserr_t (*DomainAcceptFn)(struct gracht_message*, Socket_t*);
typedef oserr_t (*DomainSendFn)(Socket_t*);
typedef oserr_t (*DomainReceiveFn)(Socket_t*);
typedef oserr_t (*DomainPairFn)(Socket_t*, Socket_t*);
typedef oserr_t (*DomainGetAddressFn)(Socket_t*, int, struct sockaddr*);
typedef void       (*DomainDestroyFn)(SocketDomain_t*);

typedef struct SocketDomainOps {
    DomainAllocateAddressFn  AddressAllocate;
    DomainFreeAddressFn      AddressFree;
    DomainBindFn             Bind;
    DomainConnectFn          Connect;
    DomainDisconnectFn       Disconnect;
    DomainAcceptFn           Accept;
    DomainSendFn             Send;
    DomainReceiveFn          Receive;
    DomainPairFn             Pair;
    DomainGetAddressFn       GetAddress;
    DomainDestroyFn          Destroy;
} SocketDomainOps_t;

oserr_t
DomainCreate(
    _In_ int              DomainType,
    _In_ SocketDomain_t** DomainOut);

void
DomainDestroy(
    _In_ SocketDomain_t* Domain);

oserr_t
DomainAllocateAddress(
    _In_ Socket_t* Socket);

oserr_t
DomainUpdateAddress(
    _In_ Socket_t*              Socket,
    _In_ const struct sockaddr* Address);

void
DomainFreeAddress(
    _In_ Socket_t* Socket);

oserr_t
DomainConnect(
    _In_ struct gracht_message* message,
    _In_ Socket_t*              socket,
    _In_ const struct sockaddr* address);

oserr_t
DomainDisconnect(
    _In_ Socket_t* Socket);

oserr_t
DomainAccept(
    _In_ struct gracht_message* message,
    _In_ Socket_t*              socket);

oserr_t
DomainPair(
    _In_ Socket_t* Socket1,
    _In_ Socket_t* Socket2);

oserr_t
DomainSend(
    _In_ Socket_t* Socket);

oserr_t
DomainReceive(
    _In_ Socket_t* Socket);

oserr_t
DomainGetAddress(
    _In_ Socket_t*        Socket,
    _In_ int              Source,
    _In_ struct sockaddr* Address);

#endif //!__NETMANAGER_DOMAINS_H__
