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
#define __TRACE

#include <assert.h>
#include <ddk/handle.h>
#include <ddk/services/net.h>
#include <ddk/utils.h>
#include "domains/domains.h"
#include <inet/local.h>
#include <io_events.h>
#include "manager.h"
#include "socket.h"
#include <stdlib.h>
#include <string.h>
#include <threads.h>

// This socket tree contains all the local system sockets that were created by
// this machine. All remote sockets are maintained by the domains
static rb_tree_t Sockets;
static UUId_t    SocketSet;
static thrd_t    SocketMonitorHandle;

/////////////////////////////////////////////////////
// APPLICATIONS => NetworkService
// The communication between applications and the network service
// consists of the use of streambuffers that are essentially a little more
// complex ringbuffers. They support some advanced use cases to fit the 
// inet/socket.h interface. This also means they are pretty useless for anything
// else than socket communication. Applications both read and write from/to the
// streambuffers, which are read and written by the network service.
static void
HandleSocketEvent(
    _In_ handle_event_t* Event)
{
    Socket_t* Socket;
    TRACE("[socket monitor] data from %u", Event->handle);
    
    Socket = NetworkManagerSocketGet(Event->handle);
    if (!Socket) {
        // Process is probably in the process of removing this, ignore this
        return;
    }
    
    // Sanitize the number of pending packets, it must be 0 for us to continue
    if (atomic_load(&Socket->PendingPackets)) {
        // Already packets pending, ignore the event
        WARNING("[socket_monitor] socket already has data pending");
        return;
    }
    
    // Make sure the socket is not passive, they are not allowed to send data
    if (Socket->Configuration.Passive) {
        ERROR("[socket_monitor] passive socket sent data, this is no-go");
        return;
    }
    
    // Data has been sent to the socket, process it and forward
    if (Event->events & IOEVTOUT) {
        OsStatus_t Status = DomainSend(Socket);
        if (Status != OsSuccess) {
            // TODO deliver ESOCKPIPE signal to process
            // proc_signal()
        }
    }
}

// socket_monitor thread:
// 1. Acquire a transfer buffer for protocol headers + payload size
//    1.1 If out of buffers, create a pending state and set PendingPackets to 1
//    1.2 Exit
// 2. Read payload data into buffer
// 3. Add to packet queue
static int
SocketMonitor(
    _In_ void* Context)
{
    int             RunForever = 1;
    handle_event_t* Events;
    int             EventCount;
    int             i;
    OsStatus_t      Status;
    
    _CRT_UNUSED(Context);
    TRACE("... [socket monitor] starting");
    
    Events = malloc(sizeof(handle_event_t) * NETWORK_MANAGER_MONITOR_MAX_EVENTS);
    if (!Events) {
        return ENOMEM;
    }
    
    while (RunForever) {
        Status = handle_set_wait(SocketSet, Events,
            NETWORK_MANAGER_MONITOR_MAX_EVENTS, 0, &EventCount);
        if (Status != OsSuccess) {
            ERROR("... [socket_monitor] WaitForHandleSet FAILED: %u", Status);
            continue;
        }
        
        for (i = 0; i < EventCount; i++) {
            HandleSocketEvent(&Events[i]);
        }
    }
    return 0;
}

/////////////////////////////////////////////////////
// NetworkService => DRIVERS
// The communication between the drivers and the network service are a little more
// dumb. The NetworkService allocates two memory pools per driver as shared buffers.
// The first one, the send buffer, is then filled with data received from applications.
// The send buffer is split up into frames of N size (determined by max-packet
// from the driver), and then queued up by the NetworkService.
// The second one, the recv buffer, is filled with data received from the driver.
// The recv buffer is split up into frames of N size (determined by max-packet 
// from the driver), and queued up for listening.

// network_monitor thread:
// 1. Receive tx event
// 2. Check if the tx event matches a socket that has data pending for tx
static int
NetworkMonitor(
    _In_ void* Context)
{
    int RunForever = 1;
    
    while (RunForever) {
        
    }
    return 0;
}

OsStatus_t
NetworkManagerInitialize(void)
{
    OsStatus_t Status;
    int        Code;
    TRACE("[net_manager] initialize");
    
    rb_tree_construct(&Sockets);
    Status = handle_set_create(0, &SocketSet);
    if (Status != OsSuccess) {
        ERROR("[net_manager] failed to create socket handle set");
        return Status;
    }
    
    // Spawn the socket monitor thread, the network monitor threads are
    // only spawned once a network card is registered.
    TRACE("[net_manager] creating thread");
    Code = thrd_create(&SocketMonitorHandle, SocketMonitor, NULL);
    if (Code != thrd_success) {
        ERROR("[net_manager] thrd_create failed %i", Code);
        return OsError;
    }
    TRACE("[net_manager] done");
    return Status;
}

OsStatus_t
NetworkManagerSocketCreate(
    _In_  UUId_t  ProcessHandle,
    _In_  int     Domain,
    _In_  int     Type,
    _In_  int     Protocol,
    _Out_ UUId_t* HandleOut,
    _Out_ UUId_t* SendBufferHandleOut,
    _Out_ UUId_t* RecvBufferHandleOut)
{
    Socket_t*  Socket;
    OsStatus_t Status;
    
    TRACE("[net_manager] [create] %u, %i, %i, %i", 
        LODWORD(ProcessHandle), Domain, Type, Protocol);
    
    if (Domain >= AF_MAX || Domain < 0) {
        WARNING("Invalid Domain type specified %i", Domain);
        return OsInvalidParameters;
    }
    
    Status = SocketCreateImpl(Domain, Type, Protocol, &Socket);
    if (Status != OsSuccess) {
        WARNING("SocketCreateImpl failed with %u", Status);
        return Status;
    }
    
    // Add it to the handle set
    Status = handle_set_ctrl(SocketSet, IO_EVT_DESCRIPTOR_ADD,
        (UUId_t)(uintptr_t)Socket->Header.key, IOEVTIN | IOEVTOUT, NULL);
    if (Status != OsSuccess) {
        // what the fuck TODO
        assert(0);
    }
    
    rb_tree_append(&Sockets, &Socket->Header);
    *HandleOut           = (UUId_t)(uintptr_t)Socket->Header.key;
    *SendBufferHandleOut = Socket->Send.DmaAttachment.handle;
    *RecvBufferHandleOut = Socket->Receive.DmaAttachment.handle;
    TRACE("[net_manager] [create] => %u", *HandleOut);
    return Status;
}

OsStatus_t
NetworkManagerSocketShutdown(
    _In_ UUId_t ProcessHandle,
    _In_ UUId_t Handle,
    _In_ int    Options)
{
    Socket_t*  Socket;
    OsStatus_t Status;
    
    TRACE("[net_manager] [shutdown] %u, %u, %i", 
        LODWORD(ProcessHandle), LODWORD(Handle), Options);
    
    Socket = NetworkManagerSocketGet(Handle);
    if (!Socket) {
        ERROR("[net_manager] [shutdown] invalid handle %u", Handle);
        return OsDoesNotExist;
    }
    
    if (Options & SOCKET_SHUTDOWN_DESTROY) {
        if (Socket->Configuration.Connected) {
            DomainDisconnect(Socket);
        }
        
        (void)rb_tree_remove(&Sockets, (void*)Handle);
        Status = handle_set_ctrl(SocketSet, IO_EVT_DESCRIPTOR_DEL, Handle, 0, NULL);
        if (Status != OsSuccess) {
            // what the fuck TODO
            assert(0);
        }
    }
    return SocketShutdownImpl(Socket, Options);
}

OsStatus_t
NetworkManagerSocketBind(
    _In_ UUId_t                 ProcessHandle,
    _In_ UUId_t                 Handle,
    _In_ const struct sockaddr* Address)
{
    Socket_t*  Socket;
    OsStatus_t Status;
    
    TRACE("[net_manager] [bind] %u, %u", 
        LODWORD(ProcessHandle), LODWORD(Handle));
    
    Socket = NetworkManagerSocketGet(Handle);
    if (!Socket) {
        ERROR("[net_manager] [bind] invalid handle %u", Handle);
        return OsDoesNotExist;
    }
    
    if (Socket->Configuration.Connecting || Socket->Configuration.Connected) {
        return OsConnectionInProgress;
    }
    
    Status = DomainUpdateAddress(Socket, Address);
    if (Status == OsSuccess) {
        Socket->Configuration.Bound = 1;
    }
    return Status;
}

// Asynchronous operation, does not send a reply on success, but instead a reply will
// be sent by Domain 
OsStatus_t
NetworkManagerSocketConnect(
    _In_ UUId_t                 ProcessHandle,
    _In_ thrd_t                 Waiter,
    _In_ UUId_t                 Handle,
    _In_ const struct sockaddr* Address)
{
    Socket_t*  Socket;
    OsStatus_t Status;
    ERROR("[net_manager] [connect] %u, %u, %u", LODWORD(ProcessHandle),
        LODWORD(Waiter), LODWORD(Handle));
    
    Socket = NetworkManagerSocketGet(Handle);
    if (!Socket) {
        ERROR("[net_manager] [connect] invalid handle %u", Handle);
        return OsHostUnreachable;
    }
    
    // If the socket is passive, then we don't allow active actions
    // like connecting to other sockets.
    if (Socket->Configuration.Passive) {
        return OsNotSupported;
    }
    
    /* Generally, connection-based protocol sockets may successfully connect() only once; 
     * connectionless protocol sockets may use connect() multiple times to change their association. 
     * Connectionless sockets may dissolve the association by connecting to an address with the 
     * sa_family member of sockaddr set to AF_UNSPEC. */
    if (Socket->Type == SOCK_STREAM || Socket->Type == SOCK_SEQPACKET) {
        if (Socket->Configuration.Connecting) {
            return OsConnectionInProgress;
        }
        
        if (Socket->Configuration.Connected) {
            return OsAlreadyConnected;
        }
    }
    else {
        // TODO
        return OsSuccess;
    }

    Socket->Configuration.Connecting = 1;
    Status = DomainConnect(Waiter, Socket, Address);
    if (Status != OsSuccess) {
        Socket->Configuration.Connecting = 0;
    }
    return Status;
}

// Asynchronous operation, does not send a reply on success, but instead a reply will
// be sent by Domain. 
OsStatus_t
NetworkManagerSocketAccept(
    _In_ UUId_t ProcessHandle,
    _In_ thrd_t Waiter,
    _In_ UUId_t Handle)
{
    Socket_t* Socket;
        ERROR("[net_manager] [accept] %u, %u, %u", LODWORD(ProcessHandle),
            LODWORD(Waiter), LODWORD(Handle));
    
    Socket = NetworkManagerSocketGet(Handle);
    if (!Socket) {
        ERROR("[net_manager] [accept] invalid handle %u", Handle);
        return OsDoesNotExist;
    }
    
    if (!Socket->Configuration.Passive) {
        return OsNotSupported;
    }
    return DomainAccept(ProcessHandle, Waiter, Socket);
}

OsStatus_t
NetworkManagerSocketListen(
    _In_ UUId_t ProcessHandle,
    _In_ UUId_t Handle,
    _In_ int    ConnectionCount)
{
    Socket_t* Socket;
    
    TRACE("[net_manager] [listen] %u, %u, %i", 
        LODWORD(ProcessHandle), LODWORD(Handle), ConnectionCount);
    
    Socket = NetworkManagerSocketGet(Handle);
    if (!Socket) {
        ERROR("[net_manager] [listen] invalid handle %u", Handle);
        return OsDoesNotExist;
    }
    
    if (Socket->Configuration.Connecting || Socket->Configuration.Connected) {
        return OsConnectionInProgress;
    }
    return SocketListenImpl(Socket, ConnectionCount);
}

OsStatus_t
NetworkManagerSocketPair(
    _In_ UUId_t ProcessHandle,
    _In_ UUId_t Handle1,
    _In_ UUId_t Handle2)
{
    Socket_t*  Socket1;
    Socket_t*  Socket2;
    OsStatus_t Status;
    
    TRACE("[net_manager] [pair] %u, %u, %u", 
        LODWORD(ProcessHandle), LODWORD(Handle1), LODWORD(Handle2));
    
    Socket1 = NetworkManagerSocketGet(Handle1);
    Socket2 = NetworkManagerSocketGet(Handle2);
    if (!Socket1 || !Socket2) {
        if (!Socket1) {
            ERROR("[net_manager] [pair] invalid handle1 %u", Handle1);
        }
        
        if (!Socket2) {
            ERROR("[net_manager] [pair] invalid handle2 %u", Handle2);
        }
        return OsDoesNotExist;
    }
    
    if (Socket1->Configuration.Passive || Socket2->Configuration.Passive) {
        ERROR("[net_manager] [pair] either Socket1/2 was marked passive (%u, %u)", 
            Socket1->Configuration.Passive, Socket2->Configuration.Passive);
        return OsNotSupported;
    }
    
    if (Socket1->Configuration.Connected || Socket2->Configuration.Connected) {
        ERROR("[net_manager] [pair] either Socket1/2 was marked connected (%u, %u)", 
            Socket1->Configuration.Connected, Socket2->Configuration.Connected);
        return OsAlreadyConnected;
    }
    
    Status = DomainPair(Socket1, Socket2);
    if (Status == OsSuccess) {
        Socket1->Configuration.Connecting = 0;
        Socket1->Configuration.Connected  = 1;
        
        Socket2->Configuration.Connecting = 0;
        Socket2->Configuration.Connected  = 1;
    }
    return Status;
}

OsStatus_t
NetworkManagerSocketSetOption(
    _In_ UUId_t           ProcessHandle,
    _In_ UUId_t           Handle,
    _In_ int              Protocol,
    _In_ unsigned int     Option,
    _In_ const void*      Data,
    _In_ socklen_t        DataLength)
{
    Socket_t* Socket;
    
    Socket = NetworkManagerSocketGet(Handle);
    if (!Socket) {
        return OsDoesNotExist;
    }
    return SetSocketOptionImpl(Socket, Protocol, Option, Data, DataLength);
}

OsStatus_t
NetworkManagerSocketGetOption(
    _In_  UUId_t           ProcessHandle,
    _In_  UUId_t           Handle,
    _In_  int              Protocol,
    _In_  unsigned int     Option,
    _In_  void*            Data,
    _Out_ socklen_t*       DataLengthOut)
{
    Socket_t* Socket;
    
    Socket = NetworkManagerSocketGet(Handle);
    if (!Socket) {
        return OsDoesNotExist;
    }
    return GetSocketOptionImpl(Socket, Protocol, Option, Data, DataLengthOut);
}

OsStatus_t
NetworkManagerSocketGetAddress(
    _In_ UUId_t           ProcessHandle,
    _In_ UUId_t           Handle,
    _In_ int              Source,
    _In_ struct sockaddr* Address)
{
    Socket_t* Socket;
    
    Socket = NetworkManagerSocketGet(Handle);
    if (!Socket) {
        return OsDoesNotExist;
    }
    return DomainGetAddress(Socket, Source, Address);
}

Socket_t*
NetworkManagerSocketGet(
    _In_ UUId_t Handle)
{
    return (Socket_t*)rb_tree_lookup_value(&Sockets, (void*)(uintptr_t)Handle);
}
