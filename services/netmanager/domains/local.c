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
#define __TRACE

#include "domains.h"
#include "../socket.h"
#include "../manager.h"
#include <ddk/handle.h>
#include <ddk/services/net.h>
#include <ddk/utils.h>
#include <ds/list.h>
#include <internal/_socket.h>
#include <inet/local.h>
#include <io_events.h>
#include <os/ipc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct AddressRecord {
    element_t Header;
    Socket_t* Socket;
} AddressRecord_t;

typedef struct SocketDomain {
    SocketDomainOps_t Ops;
    UUId_t            ConnectedSocket;
    AddressRecord_t*  Record;
} SocketDomain_t;

typedef struct ConnectionRequest {
    element_t Header;
    thrd_t    SourceWaiter;
    UUId_t    SourceSocketHandle;
} ConnectionRequest_t;

typedef struct AcceptRequest {
    element_t Header;
    UUId_t    TargetProcessHandle;
    thrd_t    TargetWaiter;
} AcceptRequest_t;

// TODO: should be hashtable
static list_t AddressRegister = LIST_INIT_CMP(list_cmp_string);

static OsStatus_t HandleInvalidType(Socket_t*);
static OsStatus_t HandleSocketStreamData(Socket_t*);
static OsStatus_t HandleSocketPacketData(Socket_t*);

typedef OsStatus_t (*LocalTypeHandler)(Socket_t*);
static LocalTypeHandler LocalTypeHandlers[6] = {
    HandleInvalidType,
    HandleSocketStreamData, // SOCK_STREAM
    HandleSocketPacketData, // SOCK_DGRAM
    HandleSocketPacketData, // SOCK_RAW
    HandleSocketPacketData, // SOCK_RDM
    HandleSocketStreamData  // SOCK_SEQPACKET
};

static Socket_t*
GetSocketFromAddress(
    _In_ const struct sockaddr* Address)
{
    AddressRecord_t* Record;
    element_t*       Element = list_find(&AddressRegister, (void*)&Address->sa_data[0]);
    if (!Element) {
        return NULL;
    }
    
    Record = Element->value;
    return Record->Socket;
}

static OsStatus_t
HandleInvalidType(
    _In_ Socket_t* Socket)
{
    ERROR("[socket] [local] HandleInvalidType() send packet");
    return OsNotSupported;
}

static OsStatus_t
DomainLocalGetAddress(
    _In_ Socket_t*        Socket,
    _In_ int              Source,
    _In_ struct sockaddr* Address)
{
    struct sockaddr_lc* LcAddress = (struct sockaddr_lc*)Address;
    AddressRecord_t*    Record    = Socket->Domain->Record;
    
    LcAddress->slc_len    = sizeof(struct sockaddr_lc);
    LcAddress->slc_family = AF_LOCAL;
    
    switch (Source) {
        case SOCKET_GET_ADDRESS_SOURCE_THIS: {
            if (!Record) {
                return OsDoesNotExist;
            }
            strcpy(&LcAddress->slc_addr[0], (const char*)Record->Header.key);
            return OsSuccess;
        } break;
        
        case SOCKET_GET_ADDRESS_SOURCE_PEER: {
            Socket_t* PeerSocket = NetworkManagerSocketGet(Socket->Domain->ConnectedSocket);
            if (!PeerSocket) {
                return OsDoesNotExist;
            }
            return DomainLocalGetAddress(PeerSocket, 
                SOCKET_GET_ADDRESS_SOURCE_THIS, Address);
        } break;
    }
    return OsInvalidParameters;
}

static OsStatus_t
HandleSocketStreamData(
    _In_ Socket_t* Socket)
{
    int             DoRead       = 1;
    streambuffer_t* SourceStream = GetSocketSendStream(Socket);
    streambuffer_t* TargetStream;
    Socket_t*       TargetSocket;
    size_t          BytesRead;
    size_t          BytesWritten;
    char            TemporaryBuffer[1024];
    void*           StoredBuffer;
    TRACE("[socket] [local] [send_stream]");
    
    TargetSocket = NetworkManagerSocketGet(Socket->Domain->ConnectedSocket);
    if (!TargetSocket) {
        TRACE("[socket] [local] [send_stream] target socket %u was not found",
            LODWORD(Socket->Domain->ConnectedSocket));
        return OsDoesNotExist;
    }
    
    TargetStream = GetSocketRecvStream(TargetSocket);
    BytesRead    = SocketGetQueuedPacket(Socket, &StoredBuffer);
    if (BytesRead) {
        memcpy(&TemporaryBuffer, StoredBuffer, BytesRead);
        free(StoredBuffer);
        DoRead = 0;
    }
    while (1) {
        if (DoRead) {
            BytesRead = streambuffer_stream_in(SourceStream, &TemporaryBuffer[0], 
                sizeof(TemporaryBuffer), STREAMBUFFER_NO_BLOCK | STREAMBUFFER_ALLOW_PARTIAL);
            if (!BytesRead) {
                // This can happen if the first event or last event got out of sync
                // we handle this by ignoring the event and just returning. Do not mark
                // anything
                return OsSuccess;
            }
        }
        DoRead = 1;
        
        BytesWritten = streambuffer_stream_out(TargetStream, &TemporaryBuffer[0], 
            BytesRead, STREAMBUFFER_NO_BLOCK | STREAMBUFFER_ALLOW_PARTIAL);
        if (BytesWritten < BytesRead) {
            StoredBuffer = malloc(BytesRead - BytesWritten);
            if (!StoredBuffer) {
                return OsOutOfMemory;
            }
            
            memcpy(StoredBuffer, &TemporaryBuffer[BytesWritten], BytesRead - BytesWritten);
            SocketSetQueuedPacket(Socket, StoredBuffer, BytesRead - BytesWritten);
            break;
        }
        
        if (BytesRead < sizeof(TemporaryBuffer)) {
            break;
        }
    }
    handle_set_activity((UUId_t)TargetSocket->Header.key, IOEVTIN);
    return OsSuccess;
}

static Socket_t*
ProcessSocketPacket(
    _In_ Socket_t* Socket,
    _In_ void*     PacketData,
    _In_ size_t    PacketLength)
{
    struct packethdr* Packet  = (struct packethdr*)PacketData;
    uint8_t*          Pointer = (uint8_t*)PacketData;
    Socket_t*         TargetSocket;
    struct sockaddr*  Address;
    TRACE("[socket] [local] [process_packet]");

    // Skip header, pointer now points to the address data.
    Pointer += sizeof(struct packethdr);
    if (Packet->addresslen) {
        Address      = (struct sockaddr*)Pointer;
        TargetSocket = GetSocketFromAddress(Address);
        TRACE("[socket] [local] [process_packet] target address %s", &Address->sa_data[0]);
    }
    else {
        TRACE("[socket] [local] [process_packet] no target address provided");
        TargetSocket = NetworkManagerSocketGet(Socket->Domain->ConnectedSocket);
        
        // Are we connected to a socket if no address was provided? If none is provided
        // we must move all the data and make room for one in the buffer. The buffer was
        // allocated with larger space if an address was required. The addresslen in the packet
        // header must also be updated
        memmove((void*)(Pointer + sizeof(struct sockaddr_lc)), (const void*)Pointer, PacketLength - sizeof(struct packethdr));
        Packet->addresslen = sizeof(struct sockaddr_lc);
    }
    
    // We must set the client address at this point. Replace the target address with
    // the source address for the reciever.
    DomainLocalGetAddress(Socket, SOCKET_GET_ADDRESS_SOURCE_THIS, (struct sockaddr*)Pointer);
    Pointer += Packet->addresslen;
    
    if (Packet->controllen) {
        // TODO handle control data
        // ProcessControlData(Pointer);
        Pointer += Packet->controllen;
    }
    return TargetSocket;
}

static OsStatus_t
HandleSocketPacketData(
    _In_ Socket_t* Socket)
{
    streambuffer_t* SourceStream = GetSocketSendStream(Socket);
    Socket_t*       TargetSocket;
    unsigned int    Base, State;
    void*           Buffer;
    size_t          BytesRead;
    int             DoRead = 1;
    TRACE("[socket] [local] [send_packet]");
    
    BytesRead = SocketGetQueuedPacket(Socket, &Buffer);
    if (BytesRead) {
        DoRead = 0;
    }
    
    while (1) {
        if (DoRead) {
            BytesRead = streambuffer_read_packet_start(SourceStream, 
                STREAMBUFFER_NO_BLOCK, &Base, &State);
            if (!BytesRead) {
                break;
            }

            // Read the entire packet in one go, then process the data. Due to possible
            // alterations in the data, like having to add an address that was not provided
            // we would like to allocate extra space for the address
            Buffer = malloc(BytesRead + sizeof(struct sockaddr_lc));
            if (!Buffer) {
                ERROR("[socket] [local] [send_packet] out of memory, failed to allocate buffer");
                return OsOutOfMemory;
            }
            
            streambuffer_read_packet_data(SourceStream, Buffer, BytesRead, &State);
            streambuffer_read_packet_end(SourceStream, Base, BytesRead);
        }
        else {
            DoRead = 1;
        }
        
        TargetSocket = ProcessSocketPacket(Socket, Buffer, BytesRead);
        if (TargetSocket) {
            streambuffer_t* TargetStream = GetSocketRecvStream(TargetSocket);
            size_t          BytesWritten = streambuffer_write_packet_start(TargetStream,
                BytesRead, STREAMBUFFER_NO_BLOCK, &Base, &State);
            if (!BytesWritten) {
                WARNING("[socket] [local] [send_packet] ran out of space in target stream, requested %" PRIuIN, BytesRead);
                SocketSetQueuedPacket(Socket, Buffer, BytesRead);
                break;
            }
            
            streambuffer_write_packet_data(TargetStream, Buffer, BytesRead, &State);
            streambuffer_write_packet_end(TargetStream, Base, BytesRead);
            handle_set_activity((UUId_t)TargetSocket->Header.key, IOEVTIN);
        }
        else {
            WARNING("[socket] [local] [send_packet] target was not found");
        }
        free(Buffer);
    }
    return OsSuccess;
}

static OsStatus_t
DomainLocalSend(
    _In_ Socket_t* Socket)
{
    TRACE("[socket] [local] [send]");
    return LocalTypeHandlers[Socket->Type](Socket);
}

static OsStatus_t
DomainLocalReceive(
    _In_ Socket_t* Socket)
{
    TRACE("DomainLocalReceive()");
    return OsNotSupported;
}

static OsStatus_t
DomainLocalPair(
    _In_ Socket_t* Socket1,
    _In_ Socket_t* Socket2)
{
    Socket1->Domain->ConnectedSocket = (UUId_t)(uintptr_t)Socket2->Header.key;
    Socket2->Domain->ConnectedSocket = (UUId_t)(uintptr_t)Socket1->Header.key;
    return OsSuccess;
}

static OsStatus_t
DomainLocalAllocateAddress(
    _In_ Socket_t* Socket)
{
    AddressRecord_t* Record;
    char             AddressBuffer[16];
    TRACE("[socket] [local] allocate address 0x%" PRIxIN " [%u]", 
        Socket, (UUId_t)Socket->Header.key);
    
    if (Socket->Domain->Record) {
        ERROR("[socket] [local] domain address 0x%" PRIxIN " already registered",
            Socket->Domain->Record);
        return OsExists;
    }
    
    Record = malloc(sizeof(AddressRecord_t));
    if (!Record) {
        return OsOutOfMemory;
    }
    
    // Create a new address of the form /lc/{id}
    sprintf(&AddressBuffer[0], "/lc/%u", (UUId_t)(uintptr_t)Socket->Header.key);
    TRACE("[socket] [local] address created %s", &AddressBuffer[0]);
    if (list_find(&AddressRegister, &AddressBuffer[0]) != NULL) {
        ERROR("[socket] [local] address %s exists in register", &AddressBuffer[0]);
        free(Record);
        return OsExists;
    }
    
    ELEMENT_INIT(&Record->Header, strdup(&AddressBuffer[0]), Record);
    Record->Socket = Socket;
    Socket->Domain->Record = Record;
    list_append(&AddressRegister, &Record->Header);
    return OsSuccess;
}

static void
DestroyAddressRecord(
    _In_ AddressRecord_t* Record)
{
    TRACE("DestroyAddressRecord()");
    list_remove(&AddressRegister, &Record->Header);
    free(Record->Header.key);
    free(Record);
}

static void
DomainLocalFreeAddress(
    _In_ Socket_t* Socket)
{
    TRACE("DomainLocalFreeAddress()");
    if (Socket->Domain->Record) {
        DestroyAddressRecord(Socket->Domain->Record);
        Socket->Domain->Record = NULL;
    }
}

static OsStatus_t
DomainLocalBind(
    _In_ Socket_t*              Socket,
    _In_ const struct sockaddr* Address)
{
    char* PreviousBuffer;
    TRACE("[domain] [local] [bind] %s", &Address->sa_data[0]);
    
    if (!Socket->Domain->Record) {
        ERROR("[domain] [local] [bind] no record");
        return OsError; // Should not happen tho
    }
    
    if (list_find(&AddressRegister, (void*)&Address->sa_data[0]) != NULL) {
        ERROR("[domain] [local] [bind] address already bound");
        return OsExists;
    }
    
    // Update key
    PreviousBuffer = (char*)Socket->Domain->Record->Header.key;
    Socket->Domain->Record->Header.key = strdup(&Address->sa_data[0]);
    free(PreviousBuffer);
    return OsSuccess;
}

static ConnectionRequest_t*
CreateConnectionRequest(
    _In_ UUId_t SourceSocketHandle,
    _In_ thrd_t SourceWaiter)
{
    ConnectionRequest_t* ConnectionRequest = malloc(sizeof(ConnectionRequest_t));
    if (!ConnectionRequest) {
        return NULL;
    }
    
    ELEMENT_INIT(&ConnectionRequest->Header, 0, ConnectionRequest);
    ConnectionRequest->SourceWaiter       = SourceWaiter;
    ConnectionRequest->SourceSocketHandle = SourceSocketHandle;
    return ConnectionRequest;
}

static AcceptRequest_t*
CreateAcceptRequest(
    _In_ UUId_t ProcessHandle,
    _In_ thrd_t Waiter)
{
    AcceptRequest_t* AcceptRequest = malloc(sizeof(AcceptRequest_t));
    if (!AcceptRequest) {
        return NULL;
    }
    
    ELEMENT_INIT(&AcceptRequest->Header, 0, AcceptRequest);
    AcceptRequest->TargetProcessHandle = ProcessHandle;
    AcceptRequest->TargetWaiter        = Waiter;
    return AcceptRequest;
}

static void
AcceptConnectionRequest(
    _In_ UUId_t    ProcessHandle,
    _In_ thrd_t    AcceptWaiter,
    _In_ Socket_t* ConnectSocket,
    _In_ thrd_t    ConnectWaiter)
{
    AcceptSocketPackage_t Package = { 0 };
    IpcMessage_t          Message = { 0 };
    TRACE("[net_manager] [accept_request]");
    
    // Get address of the connector socket
    DomainLocalGetAddress(ConnectSocket, SOCKET_GET_ADDRESS_SOURCE_THIS, (struct sockaddr*)&Package.Address);
    
    // Create a new socket for the acceptor. This socket will be paired with
    // the connector socket.
    Package.Status = NetworkManagerSocketCreate(ProcessHandle, ConnectSocket->DomainType,
        ConnectSocket->Type, ConnectSocket->Protocol, &Package.SocketHandle,
        &Package.SendBufferHandle, &Package.RecvBufferHandle);
    if (Package.Status == OsSuccess) {
        NetworkManagerSocketPair(ProcessHandle, Package.SocketHandle, 
            (UUId_t)(uintptr_t)ConnectSocket->Header.key);
    }
    
    // Reply to the connector (the thread that called connect())
    Message.Sender = ConnectWaiter;
    (void)IpcReply(&Message, &Package.Status, sizeof(OsStatus_t));
    
    // Reply to the accepter (the thread that called accept())
    Message.Sender = AcceptWaiter;
    (void)IpcReply(&Message, &Package, sizeof(AcceptSocketPackage_t));
}

static OsStatus_t
HandleLocalConnectionRequest(
    _In_ thrd_t    SourceWaiter,
    _In_ Socket_t* SourceSocket,
    _In_ Socket_t* TargetSocket)
{
    ConnectionRequest_t* ConnectionRequest;
    AcceptRequest_t*     AcceptRequest;
    element_t*           Element;
    
    TRACE("[domain] [local] [handle_connect] %u, %u => %u", LODWORD(SourceWaiter),
        LODWORD(SourceSocket->Header.key), LODWORD(TargetSocket->Header.key));
    
    // Check for active accept requests, otherwise we need to queue it up. If the backlog
    // is full, we need to reject the connection request.
    mtx_lock(&TargetSocket->SyncObject);
    Element = queue_pop(&TargetSocket->AcceptRequests);
    if (!Element) {
        TRACE("[domain] [local] [handle_connect] creating request");
        ConnectionRequest = CreateConnectionRequest((UUId_t)(uintptr_t)SourceSocket->Header.key, SourceWaiter);
        if (!ConnectionRequest) {
            mtx_unlock(&TargetSocket->SyncObject);
            ERROR("[domain] [local] [handle_connect] failed to allocate memory for connection request");
            return OsOutOfMemory;
        }
        
        // TODO If the backlog is full, reject
        // return OsConnectionRefused
        queue_push(&TargetSocket->ConnectionRequests, &ConnectionRequest->Header);
        handle_set_activity((UUId_t)(uintptr_t)TargetSocket->Header.key, IOEVTCTL);
    }
    mtx_unlock(&TargetSocket->SyncObject);
    
    // Handle the accept request we popped earlier here, this means someone
    // has called accept() on the socket and is actively waiting
    if (Element) {
        AcceptRequest = Element->value;
        AcceptConnectionRequest(AcceptRequest->TargetProcessHandle,
            AcceptRequest->TargetWaiter, SourceSocket, SourceWaiter);
        free(AcceptRequest);
    }
    return OsSuccess;
}

static OsStatus_t
DomainLocalConnect(
    _In_ thrd_t                 Waiter,
    _In_ Socket_t*              Socket,
    _In_ const struct sockaddr* Address)
{
    Socket_t* Target = GetSocketFromAddress(Address);
    TRACE("[domain] [local] [connect] %s", &Address->sa_data[0]);
    
    if (!Target) {
        TRACE("[domain] [local] [connect] %s did not exist", &Address->sa_data[0]);
        return OsHostUnreachable;
    }
    
    if (Socket->Type != Target->Type) {
        TRACE("[domain] [local] [connect] target is valid, but protocol was invalid source (%i, %i, %i) != target (%i, %i, %i)",
            Socket->DomainType, Socket->Type, Socket->Protocol, Target->DomainType, Target->Type, Target->Protocol);
        return OsInvalidProtocol;
    }
    
    if (Socket->Type == SOCK_STREAM || Socket->Type == SOCK_SEQPACKET) {
        return HandleLocalConnectionRequest(Waiter, Socket, Target);
    }
    else {
        // Don't handle this scenario. It is handled locally in libc
        return OsSuccess;
    }
}

static OsStatus_t
DomainLocalDisconnect(
    _In_ Socket_t* Socket)
{
    Socket_t*  PeerSocket = NetworkManagerSocketGet(Socket->Domain->ConnectedSocket);
    OsStatus_t Status     = OsHostUnreachable;
    TRACE("[domain] [local] [disconnect] %u => %u", LODWORD(Socket->Header.key),
        LODWORD(Socket->Domain->ConnectedSocket));
    
    // Send a disconnect request if socket was valid
    if (PeerSocket) {
        handle_set_activity(Socket->Domain->ConnectedSocket, IOEVTCTL);
        Status = OsSuccess;
    }
    
    Socket->Domain->ConnectedSocket = UUID_INVALID;
    Socket->Configuration.Connected = 0;
    return Status;
}

static OsStatus_t
DomainLocalAccept(
    _In_ UUId_t    ProcessHandle,
    _In_ thrd_t    Waiter,
    _In_ Socket_t* Socket)
{
    Socket_t*            ConnectSocket;
    ConnectionRequest_t* ConnectionRequest;
    AcceptRequest_t*     AcceptRequest;
    element_t*           Element;
    OsStatus_t           Status = OsSuccess;
    TRACE("[domain] [local] [accept] %u", LODWORD(Socket->Header.key));
    
    // Check if there is any requests available
    mtx_lock(&Socket->SyncObject);
    Element = queue_pop(&Socket->ConnectionRequests);
    if (Element) {
        mtx_unlock(&Socket->SyncObject);
        ConnectionRequest = Element->value;
        
        // Lookup the socket handle, to check if it is still valid
        ConnectSocket = NetworkManagerSocketGet(ConnectionRequest->SourceSocketHandle);
        if (ConnectSocket) {
            AcceptConnectionRequest(ProcessHandle, Waiter, ConnectSocket,
                ConnectionRequest->SourceWaiter);
        }
        else {
            Status = OsConnectionAborted;
        }
        free(ConnectionRequest);
    }
    else {
        // Only wait if configured to blocking, otherwise return OsBusy ish
        if (Socket->Configuration.Blocking) {
            AcceptRequest = CreateAcceptRequest(ProcessHandle, Waiter);
            if (AcceptRequest) {
                queue_push(&Socket->AcceptRequests, &AcceptRequest->Header);
            }
            else {
                Status = OsOutOfMemory;
            }
        }
        else {
            Status = OsBusy; // TODO: OsTryAgain
        }
        mtx_unlock(&Socket->SyncObject);
    }
    return Status;
}

static void
RejectConnectionRequest(
    _In_ element_t* Element,
    _In_ void*      Context)
{
    
}

static void
RejectAcceptRequest(
    _In_ element_t* Element,
    _In_ void*      Context)
{
    
}

static void
DomainLocalDestroy(
    _In_ SocketDomain_t* Domain)
{
    TRACE("DomainLocalDestroy()");
    
    // Go through connection requests and reject them
    //list_clear(&Socket->ConnectionRequests, RejectConnectionRequest, NULL);

    // Go through accept requests and reject them
    //list_clear(&Socket->AcceptRequests, RejectAcceptRequest, NULL);
    
    if (Domain->Record) {
        DestroyAddressRecord(Domain->Record);
    }
    free(Domain);
}

OsStatus_t
DomainLocalCreate(
    _Out_ SocketDomain_t** DomainOut)
{
    SocketDomain_t* Domain = malloc(sizeof(SocketDomain_t));
    if (!Domain) {
        return OsOutOfMemory;
    }
    TRACE("DomainLocalCreate()");
    
    // Setup operations
    Domain->Ops.AddressAllocate = DomainLocalAllocateAddress;
    Domain->Ops.AddressFree     = DomainLocalFreeAddress;
    Domain->Ops.Bind            = DomainLocalBind;
    Domain->Ops.Connect         = DomainLocalConnect;
    Domain->Ops.Disconnect      = DomainLocalDisconnect;
    Domain->Ops.Accept          = DomainLocalAccept;
    Domain->Ops.Send            = DomainLocalSend;
    Domain->Ops.Receive         = DomainLocalReceive;
    Domain->Ops.Pair            = DomainLocalPair;
    Domain->Ops.GetAddress      = DomainLocalGetAddress;
    Domain->Ops.Destroy         = DomainLocalDestroy;
    
    Domain->ConnectedSocket = UUID_INVALID;
    Domain->Record          = NULL;
    
    *DomainOut = Domain;
    return OsSuccess;
}
