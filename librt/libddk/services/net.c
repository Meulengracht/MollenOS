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

#include <ddk/services/net.h>
#include <ddk/services/service.h>
#include <os/ipc.h>
#include <os/services/process.h>
#include <string.h>

/////////////////////////////////////////////////////
// APPLICATIONS => NetworkService
// The communication between applications and the network service
// consists of the use of streambuffers that are essentially a little more
// complex ringbuffers. They support some advanced use cases to fit the 
// inet/socket.h interface. This also means they are pretty useless for anything
// else than socket communication. Applications both read and write from/to the
// streambuffers, which are read and written by the network service.

/////////////////////////////////////////////////////
// NetworkService => DRIVERS
// The communication between the drivers and the network service are a little more
// dump. The NetworkService allocates two memory pools per driver as shared buffers.
// The first one, the send buffer, is then filled with data received from applications.
// The send buffer is split up into frames of N size (determined by max-packet
// from the driver), and then queued up by the NetworkService.
// The second one, the recv buffer, is filled with data received from the driver.
// The recv buffer is split up into frames of N size (determined by max-packet 
// from the driver), and queued up for listening.

OsStatus_t
CreateSocket(
    _In_  int     Domain,
    _In_  int     Type,
    _In_  int     Protocol,
    _Out_ UUId_t* HandleOut,
    _Out_ UUId_t* SendBufferHandleOut,
    _Out_ UUId_t* RecvBufferHandleOut)
{
	IpcMessage_t               Request;
	SocketDescriptorPackage_t* Package;
	OsStatus_t                 Status;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __NETMANAGER_CREATE_SOCKET);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Domain);
	IPC_SET_TYPED(&Request, 3, Type);
	IPC_SET_TYPED(&Request, 4, Protocol);
	
	Status = IpcInvoke(GetNetService(), &Request, 0, 0, (void**)&Package);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	if (Package->Status == OsSuccess) {
	    *HandleOut           = Package->SocketHandle;
	    *SendBufferHandleOut = Package->SendBufferHandle;
	    *RecvBufferHandleOut = Package->RecvBufferHandle;
	}
    return Package->Status;
}

OsStatus_t
CloseSocket(
    _In_ UUId_t       Handle,
    _In_ unsigned int Options)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __NETMANAGER_CLOSE_SOCKET);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	IPC_SET_TYPED(&Request, 3, Options);
	
	Status = IpcInvoke(GetNetService(), &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	return IPC_CAST_AND_DEREF(Result, OsStatus_t);
}

OsStatus_t
BindSocket(
    _In_ UUId_t                 Handle,
    _In_ const struct sockaddr* Address)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __NETMANAGER_BIND_SOCKET);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	IpcSetUntypedArgument(&Request, 0, (void*)Address, Address->sa_len);
	
	Status = IpcInvoke(GetNetService(), &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	return IPC_CAST_AND_DEREF(Result, OsStatus_t);
}

OsStatus_t
ConnectSocket(
    _In_ UUId_t                 Handle,
    _In_ const struct sockaddr* Address)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __NETMANAGER_CONNECT_SOCKET);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	IpcSetUntypedArgument(&Request, 0, (void*)Address, Address->sa_len);
	
	Status = IpcInvoke(GetNetService(), &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	return IPC_CAST_AND_DEREF(Result, OsStatus_t);
}

OsStatus_t
AcceptSocket(
    _In_ UUId_t           Handle,
    _In_ struct sockaddr* Address)
{
	IpcMessage_t               Request;
	GetSocketAddressPackage_t* Package;
	OsStatus_t                 Status;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __NETMANAGER_ACCEPT_SOCKET);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	
	Status = IpcInvoke(GetNetService(), &Request, 0, 0, (void**)&Package);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	if (Package->Status == OsSuccess && Address) {
	    memcpy(Address, &Package->Address, Package->Address.__ss_len);
	}
	return Package->Status;
}

OsStatus_t
ListenSocket(
    _In_ UUId_t Handle,
    _In_ int    ConnectionQueueSize)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __NETMANAGER_LISTEN_SOCKET);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	IPC_SET_TYPED(&Request, 3, ConnectionQueueSize);
	
	Status = IpcInvoke(GetNetService(), &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	return IPC_CAST_AND_DEREF(Result, OsStatus_t);
}

OsStatus_t
SetSocketOption(
    _In_ UUId_t       Handle,
    _In_ int          Protocol,
    _In_ unsigned int Option,
    _In_ const void*  Data,
    _In_ socklen_t    DataLength)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __NETMANAGER_SET_SOCKET_OPTION);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	IPC_SET_TYPED(&Request, 3, Protocol);
	IPC_SET_TYPED(&Request, 4, Option);
	IpcSetUntypedArgument(&Request, 0, (void*)Data, DataLength);
	
	Status = IpcInvoke(GetNetService(), &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	return IPC_CAST_AND_DEREF(Result, OsStatus_t);
}

OsStatus_t
GetSocketOption(
    _In_ UUId_t       Handle,
    _In_ int          Protocol,
    _In_ unsigned int Option,
    _In_ void*        Data,
    _In_ socklen_t*   DataLength)
{
	IpcMessage_t              Request;
	GetSocketOptionPackage_t* Package;
	OsStatus_t                Status;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __NETMANAGER_GET_SOCKET_OPTION);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	IPC_SET_TYPED(&Request, 3, Protocol);
	IPC_SET_TYPED(&Request, 4, Option);
	
	Status = IpcInvoke(GetNetService(), &Request, 0, 0, (void**)&Package);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	if (Package->Status == OsSuccess) {
	    memcpy(Data, &Package->Data[0], MIN(*DataLength, Package->Length));
	    *DataLength = Package->Length;
	}
	return Package->Status;
}

OsStatus_t
GetSocketAddress(
    _In_    UUId_t           Handle,
    _In_    int              Source,
    _In_    struct sockaddr* Address,
    _InOut_ socklen_t*       AddressLengthOut)
{
	IpcMessage_t               Request;
	GetSocketAddressPackage_t* Package;
	OsStatus_t                 Status;
	size_t                     BytesToCopy;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __NETMANAGER_GET_SOCKET_ADDRESS);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	IPC_SET_TYPED(&Request, 3, Source);
	
	Status = IpcInvoke(GetNetService(), &Request, 0, 0, (void**)&Package);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	BytesToCopy = MIN(*AddressLengthOut, Package->Address.__ss_len);
	if (Package->Status == OsSuccess && Address) {
	    memcpy(Address, &Package->Address, BytesToCopy);
	}
	*AddressLengthOut = Package->Address.__ss_len;
	return Package->Status;
}
