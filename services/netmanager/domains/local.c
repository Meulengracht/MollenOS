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
#include <ds/collection.h>
#include <internal/_socket.h>
#include <inet/local.h>
#include <io_events.h>
#include <os/ipc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct AddressRecord {
    CollectionItem_t Header;
    Socket_t*        Socket;
} AddressRecord_t;

typedef struct SocketDomain {
    SocketDomainOps_t Ops;
    mtx_t             SyncObject;
    UUId_t            ConnectedSocket;
    AddressRecord_t*  Record;
    Collection_t      ConnectionRequests; // TODO: queue
    Collection_t      AcceptRequests;     // TODO: queue
} SocketDomain_t;

typedef struct ConnectionRequest {
    CollectionItem_t        Header;
    thrd_t                  SourceWaiter;
    struct sockaddr_storage SourceAddress;
} ConnectionRequest_t;

typedef struct AcceptRequest {
    CollectionItem_t Header;
    thrd_t           SourceWaiter;
} AcceptRequest_t;

// TODO: should be hashtable
static Collection_t AddressRegister = COLLECTION_INIT(KeyString);

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
    DataKey_t Key = { 
        .Value.String.Pointer = &Address->sa_data[0],
        .Value.String.Length = strlen(&Address->sa_data[0])
    };
    return (Socket_t*)CollectionGetNodeByKey(&AddressRegister, Key, 0);
}

static OsStatus_t
HandleInvalidType(
    _In_ Socket_t* Socket)
{
    ERROR("... [local socket] HandleInvalidType() send packet");
    return OsNotSupported;
}

static OsStatus_t
HandleSocketStreamData(
    _In_ Socket_t* Socket)
{
    int                   DoRead       = 1;
    streambuffer_t*       SourceStream = GetSocketSendStream(Socket);
    streambuffer_t*       TargetStream;
    Socket_t*             TargetSocket;
    size_t                BytesRead;
    size_t                BytesWritten;
    char                  TemporaryBuffer[1024];
    void*                 StoredBuffer;
    TRACE("HandleSocketStreamData()");
    
    TargetSocket = NetworkManagerSocketGet(Socket->Domain->ConnectedSocket);
    if (!TargetSocket) {
        return OsDoesNotExist; // What the fuck do? TODO
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
        }
        
        BytesWritten = streambuffer_stream_out(TargetStream, &TemporaryBuffer[0], 
            BytesRead, STREAMBUFFER_NO_BLOCK | STREAMBUFFER_ALLOW_PARTIAL);
        if (BytesWritten < BytesRead) {
            StoredBuffer = malloc(BytesRead - BytesWritten);
            memcpy(StoredBuffer, &TemporaryBuffer[BytesWritten], BytesRead - BytesWritten);
            SocketSetQueuedPacket(Socket, StoredBuffer, BytesRead - BytesWritten);
            break;
        }
        
        if (BytesRead < sizeof(TemporaryBuffer)) {
            break;
        }
        DoRead++;
    }
    
    handle_set_activity(Socket->Header.Key.Value.Id, IOEVTIN);
    return OsSuccess;
}

static streambuffer_t*
ProcessSocketPacket(
    _In_ Socket_t* Socket,
    _In_ void*     PacketData,
    _In_ size_t    PacketLength)
{
    streambuffer_t*   TargetStream = NULL;
    struct packethdr* Packet       = (struct packethdr*)PacketData;
    uint8_t*          Pointer      = (uint8_t*)PacketData;
    Socket_t*         TargetSocket;
    struct sockaddr*  Address;
    TRACE("ProcessSocketPacket()");

    // Skip header, pointer now points to the address data
    Pointer += sizeof(struct packethdr);
    if (Packet->addresslen) {
        Address      = (struct sockaddr*)Pointer;
        TargetSocket = GetSocketFromAddress(Address);
        Pointer     += Packet->addresslen;
    }
    else {
        // Are we connected to a socket?
        TargetSocket = NetworkManagerSocketGet(Socket->Domain->ConnectedSocket);
    }
    
    if (Packet->controllen) {
        // TODO handle control data
        // ProcessControlData(Pointer);
        Pointer += Packet->controllen;
    }
    
    if (TargetSocket) {
        TargetStream = GetSocketRecvStream(TargetSocket);
    }
    return TargetStream;
}

static OsStatus_t
HandleSocketPacketData(
    _In_ Socket_t* Socket)
{
    streambuffer_t* SourceStream = GetSocketSendStream(Socket);
    streambuffer_t* TargetStream = NULL;
    unsigned int    Base, State;
    void*           Buffer;
    size_t          BytesRead;
    int             DoRead = 1;
    TRACE("HandleSocketPacketData()");
    
    BytesRead = SocketGetQueuedPacket(Socket, &Buffer);
    if (BytesRead) {
        DoRead = 0;
    }
    
    while (1) {
        if (DoRead) {
            size_t BytesRead = streambuffer_read_packet_start(SourceStream, 
                STREAMBUFFER_NO_BLOCK, &Base, &State);
            if (!BytesRead) {
                break;
            }

            // Read the entire packet in one go, then process the data
            Buffer = malloc(BytesRead);
            streambuffer_read_packet_data(SourceStream, Buffer, BytesRead, &State);
            streambuffer_read_packet_end(SourceStream, Base, BytesRead);
        }
        
        TargetStream = ProcessSocketPacket(Socket, Buffer, BytesRead);
        if (TargetStream) {
            size_t BytesWritten = streambuffer_write_packet_start(TargetStream, BytesRead, 
                STREAMBUFFER_NO_BLOCK, &Base, &State);
            if (!BytesWritten) {
                SocketSetQueuedPacket(Socket, Buffer, BytesRead);
                break;
            }    
            
            streambuffer_write_packet_data(TargetStream, Buffer, BytesRead, &State);
            streambuffer_write_packet_end(TargetStream, Base, BytesRead);
        }
        free(Buffer);
    }
    
    handle_set_activity(Socket->Header.Key.Value.Id, IOEVTIN);
    return OsSuccess;
}

static OsStatus_t
DomainLocalSend(
    _In_ Socket_t* Socket)
{
    TRACE("DomainLocalSend()");
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
DomainLocalAllocateAddress(
    _In_ Socket_t* Socket)
{
    AddressRecord_t* Record;
    char             AddressBuffer[16];
    TRACE("DomainLocalAllocateAddress()");
    
    if (Socket->Domain->Record) {
        return OsExists;
    }
    
    Record = malloc(sizeof(AddressRecord_t));
    if (!Record) {
        return OsOutOfMemory;
    }
    
    // Create a new address of the form /lc/{id}
    sprintf(&AddressBuffer[0], "/lc/%u", Socket->Header.Key.Value.Id);
    Record->Header.Key.Value.String.Pointer = &AddressBuffer[0];
    Record->Header.Key.Value.String.Length = strlen(&AddressBuffer[0]);
    if (CollectionGetNodeByKey(&AddressRegister, Record->Header.Key, 0) != NULL) {
        free(Record);
        return OsExists;
    }
    
    Record->Header.Key.Value.String.Pointer = strdup(&AddressBuffer[0]);
    Record->Socket = Socket;
    
    Socket->Domain->Record = Record;
    return CollectionAppend(&AddressRegister, &Record->Header);
}

static void
DestroyAddressRecord(
    _In_ AddressRecord_t* Record)
{
    TRACE("DestroyAddressRecord()");
    CollectionRemoveByNode(&AddressRegister, &Record->Header);
    free((void*)Record->Header.Key.Value.String.Pointer);
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
    char*     PreviousBuffer;
    DataKey_t Key = { 
        .Value.String.Pointer = &Address->sa_data[0],
        .Value.String.Length = strlen(&Address->sa_data[0])
    };
    TRACE("DomainLocalBind()");
    
    if (!Socket->Domain->Record) {
        return OsError; // Should not happen tho
    }
    
    if (CollectionGetNodeByKey(&AddressRegister, Key, 0) != NULL) {
        return OsExists;
    }
    
    // Update key
    PreviousBuffer = (char*)Socket->Domain->Record->Header.Key.Value.String.Pointer;
    Socket->Domain->Record->Header.Key.Value.String.Pointer = strdup(&Address->sa_data[0]);
    Socket->Domain->Record->Header.Key.Value.String.Length = Key.Value.String.Length;
    free(PreviousBuffer);
    return OsSuccess;
}

static void
SignalAcceptRequest(
    _In_ thrd_t           Connector,
    _In_ AddressRecord_t* ConnectorAddress,
    _In_ AcceptRequest_t* Request)
{
    GetSocketAddressPackage_t Package = { .Status = OsSuccess };
    OsStatus_t                Status  = OsSuccess;
    IpcMessage_t              Message;
    struct sockaddr_lc*       Address = sstolc(&Package.Address);
    TRACE("SignalAcceptRequest()");
    
    Address->slc_len    = sizeof(struct sockaddr_lc);
    Address->slc_family = AF_LOCAL;
    memcpy(&Address->slc_addr[0], 
        ConnectorAddress->Header.Key.Value.String.Pointer,
        ConnectorAddress->Header.Key.Value.String.Length);
    
    // Reply to the connector
    Message.Sender = Connector;
    (void)IpcReply(&Message, &Status, sizeof(OsStatus_t));
    
    // Reply to the accepter
    Message.Sender = Request->SourceWaiter;
    (void)IpcReply(&Message, &Package, sizeof(GetSocketAddressPackage_t));
}

static OsStatus_t
DomainLocalConnect(
    _In_ thrd_t                 Waiter,
    _In_ Socket_t*              Socket,
    _In_ const struct sockaddr* Address)
{
    AcceptRequest_t*     Request;
    ConnectionRequest_t* ConnectionRequest;
    Socket_t*            Target = GetSocketFromAddress(Address);
    TRACE("DomainLocalConnect()");
    
    if (!Target) {
        return OsDoesNotExist;
    }
    
    if (!Target->Configuration.Passive) {
        return OsInvalidPermissions;
    }
    
    // Create a connection request on the socket
    mtx_lock(&Socket->Domain->SyncObject);
    Request = (AcceptRequest_t*)CollectionPopFront(&Socket->Domain->AcceptRequests);
    if (Request) {
        SignalAcceptRequest(Waiter, Socket->Domain->Record, Request);
        free(Request);
    }
    else {
        ConnectionRequest = malloc(sizeof(ConnectionRequest_t));
        ConnectionRequest->SourceWaiter = Waiter;
        CollectionAppend(&Socket->Domain->ConnectionRequests, &ConnectionRequest->Header);
    }
    mtx_unlock(&Socket->Domain->SyncObject);
    return OsSuccess;
}

static void
AcceptConnectionRequest(
    _In_ thrd_t               Waiter,
    _In_ ConnectionRequest_t* Request)
{
    GetSocketAddressPackage_t Package = { .Status = OsSuccess };
    OsStatus_t                Status  = OsSuccess;
    IpcMessage_t              Message;
    TRACE("AcceptConnectionRequest()");
    
    memcpy(&Package.Address, &Request->SourceAddress, 
        sizeof(struct sockaddr_storage));
    
    // Reply to the connector
    Message.Sender = Request->SourceWaiter;
    (void)IpcReply(&Message, &Status, sizeof(OsStatus_t));
    
    // Reply to the accepter
    Message.Sender = Waiter;
    (void)IpcReply(&Message, &Package, sizeof(GetSocketAddressPackage_t));
}

static OsStatus_t
DomainLocalAccept(
    _In_ thrd_t    Waiter,
    _In_ Socket_t* Socket)
{
    ConnectionRequest_t* Request;
    AcceptRequest_t*     AcceptRequest;
    OsStatus_t           Status = OsSuccess;
    TRACE("DomainLocalAccept()");
    
    // Check if there is any requests available
    mtx_lock(&Socket->Domain->SyncObject);
    Request = (ConnectionRequest_t*)CollectionPopFront(&Socket->Domain->ConnectionRequests);
    if (Request) {
        AcceptConnectionRequest(Waiter, Request);
        free(Request);
    }
    else {
        // Only wait if configured to blocking, otherwise return OsBusy ish
        if (Socket->Configuration.Blocking) {
            AcceptRequest = malloc(sizeof(AcceptRequest_t));
            AcceptRequest->SourceWaiter = Waiter;
            CollectionAppend(&Socket->Domain->AcceptRequests, &AcceptRequest->Header);
        }
        else {
            Status = OsBusy; // TODO: OsTryAgain
        }
    }
    mtx_unlock(&Socket->Domain->SyncObject);
    return Status;
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
            memcpy(&LcAddress->slc_addr[0], 
                Record->Header.Key.Value.String.Pointer,
                Record->Header.Key.Value.String.Length);
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

static void
DomainLocalDestroy(
    _In_ SocketDomain_t* Domain)
{
    TRACE("DomainLocalDestroy()");
    
    // Go through connection requests and reject them
    
    // Go through accept requests and reject them
    
    mtx_destroy(&Domain->SyncObject);
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
    Domain->Ops.Accept          = DomainLocalAccept;
    Domain->Ops.Send            = DomainLocalSend;
    Domain->Ops.Receive         = DomainLocalReceive;
    Domain->Ops.GetAddress      = DomainLocalGetAddress;
    Domain->Ops.Destroy         = DomainLocalDestroy;
    mtx_init(&Domain->SyncObject, mtx_plain);
    
    CollectionConstruct(&Domain->ConnectionRequests, KeyInteger);
    CollectionConstruct(&Domain->AcceptRequests, KeyInteger);
    Domain->ConnectedSocket = UUID_INVALID;
    Domain->Record          = NULL;
    
    *DomainOut = Domain;
    return OsSuccess;
}
