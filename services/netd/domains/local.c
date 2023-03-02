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

//#define __TRACE

#include "domains.h"
#include "../socket.h"
#include "../manager.h"
#include "os/notification_queue.h"
#include <ddk/utils.h>
#include <ds/list.h>
#include <internal/_socket.h>
#include <inet/local.h>
#include <ioset.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys_socket_service_server.h>

typedef struct AddressRecord {
    element_t Header;
    Socket_t* Socket;
} AddressRecord_t;

typedef struct SocketDomain {
    SocketDomainOps_t Ops;
    uuid_t            ConnectedSocket;
    AddressRecord_t*  Record;
} SocketDomain_t;

typedef struct ConnectionRequest {
    element_t             Header;
    uuid_t                SourceSocketHandle;
    struct gracht_message Response[];
} ConnectionRequest_t;

typedef struct AcceptRequest {
    element_t             Header;
    struct gracht_message Response[];
} AcceptRequest_t;

// TODO: should be hashtable
static list_t AddressRegister = LIST_INIT_CMP(list_cmp_string);

static oserr_t HandleInvalidType(Socket_t*);
static oserr_t HandleSocketStreamData(Socket_t*);
static oserr_t HandleSocketPacketData(Socket_t*);

typedef oserr_t (*LocalTypeHandler)(Socket_t*);
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

static oserr_t
HandleInvalidType(
    _In_ Socket_t* Socket)
{
    ERROR("[socket] [local] HandleInvalidType() send packet");
    return OS_ENOTSUPPORTED;
}

static oserr_t
DomainLocalGetAddress(
    _In_ Socket_t*        socket,
    _In_ int              source,
    _In_ struct sockaddr* address)
{
    struct sockaddr_lc* lcAddress = (struct sockaddr_lc*)address;
    AddressRecord_t*    record    = socket->Domain->Record;

    lcAddress->slc_len    = sizeof(struct sockaddr_lc);
    lcAddress->slc_family = AF_LOCAL;
    
    switch (source) {
        case SYS_ADDRESS_TYPE_THIS: {
            if (!record) {
                return OS_ENOENT;
            }
            strcpy(&lcAddress->slc_addr[0], (const char*)record->Header.key);
            return OS_EOK;
        } break;
        
        case SYS_ADDRESS_TYPE_PEER: {
            Socket_t* peerSocket = NetworkManagerSocketGet(socket->Domain->ConnectedSocket);
            if (!peerSocket) {
                return OS_ENOENT;
            }
            return DomainLocalGetAddress(peerSocket,
                                         SYS_ADDRESS_TYPE_THIS,
                                         address);
        } break;

        default:
            break;
    }
    return OS_EINVALPARAMS;
}

static oserr_t
HandleSocketStreamData(
    _In_ Socket_t* socket)
{
    int             DoRead       = 1;
    streambuffer_t* SourceStream = GetSocketSendStream(socket);
    streambuffer_t* TargetStream;
    Socket_t*       TargetSocket;
    size_t          BytesRead;
    size_t          BytesWritten;
    char            TemporaryBuffer[1024];
    void*           StoredBuffer;
    TRACE("[socket] [local] [send_stream]");
    
    TargetSocket = NetworkManagerSocketGet(socket->Domain->ConnectedSocket);
    if (!TargetSocket) {
        TRACE("[socket] [local] [send_stream] target socket %u was not found",
            LODWORD(socket->Domain->ConnectedSocket));
        return OS_ENOENT;
    }
    
    TargetStream = GetSocketRecvStream(TargetSocket);
    BytesRead    = SocketGetQueuedPacket(socket, &StoredBuffer);
    if (BytesRead) {
        memcpy(&TemporaryBuffer, StoredBuffer, BytesRead);
        free(StoredBuffer);
        DoRead = 0;
    }
    while (1) {
        if (DoRead) {
            BytesRead = streambuffer_stream_in(
                    SourceStream,
                    &TemporaryBuffer[0],
                    sizeof(TemporaryBuffer),
                    &(streambuffer_rw_options_t) {
                        .flags = STREAMBUFFER_NO_BLOCK | STREAMBUFFER_ALLOW_PARTIAL,
                        .async_context = NULL,
                        .deadline = NULL
                    }
            );
            TRACE("[socket] [local] [send_stream] read %" PRIuIN " bytes from source", BytesRead);
            if (!BytesRead) {
                // This can happen if the first event or last event got out of sync
                // we handle this by ignoring the event and just returning. Do not mark
                // anything
                return OS_EOK;
            }
        }
        DoRead = 1;
        
        BytesWritten = streambuffer_stream_out(
                TargetStream,
                &TemporaryBuffer[0],
                BytesRead,
                &(streambuffer_rw_options_t) {
                    .flags = STREAMBUFFER_NO_BLOCK | STREAMBUFFER_ALLOW_PARTIAL,
                    .async_context = NULL,
                    .deadline = NULL
                }
        );
        TRACE("[socket] [local] [send_stream] wrote %" PRIuIN " bytes to target", BytesWritten);
        if (BytesWritten < BytesRead) {
            StoredBuffer = malloc(BytesRead - BytesWritten);
            if (!StoredBuffer) {
                OSNotificationQueuePost((uuid_t)(uintptr_t)TargetSocket->Header.key, IOSETIN);
                return OS_EOOM;
            }
            
            memcpy(StoredBuffer, &TemporaryBuffer[BytesWritten], BytesRead - BytesWritten);
            SocketSetQueuedPacket(socket, StoredBuffer, BytesRead - BytesWritten);
            break;
        }
        
        if (BytesRead < sizeof(TemporaryBuffer)) {
            break;
        }
    }
    OSNotificationQueuePost((uuid_t)(uintptr_t)TargetSocket->Header.key, IOSETIN);
    return OS_EOK;
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
    DomainLocalGetAddress(Socket, SYS_ADDRESS_TYPE_THIS, (struct sockaddr*)Pointer);
    Pointer += Packet->addresslen;
    
    if (Packet->controllen) {
        // TODO handle control data
        // ProcessControlData(Pointer);
        Pointer += Packet->controllen;
    }
    return TargetSocket;
}

static oserr_t
HandleSocketPacketData(
    _In_ Socket_t* Socket)
{
    streambuffer_t*           sourceStream = GetSocketSendStream(Socket);
    Socket_t*                 targetSocket;
    void*                     buffer;
    size_t                    bytesRead;
    int                       doRead = 1;
    streambuffer_packet_ctx_t packetCtx;
    TRACE("[socket] [local] [send_packet]");

    bytesRead = SocketGetQueuedPacket(Socket, &buffer);
    if (bytesRead) {
        doRead = 0;
    }
    
    while (1) {
        if (doRead) {
            bytesRead = streambuffer_read_packet_start(
                    sourceStream,
                    &(streambuffer_rw_options_t) {
                        .flags = STREAMBUFFER_NO_BLOCK,
                        .async_context = NULL,
                        .deadline = NULL
                    },
                    &packetCtx
            );
            if (!bytesRead) {
                TRACE("[socket] [local] [send_packet] no bytes read from stream");
                break;
            }

            // Read the entire packet in one go, then process the data. Due to possible
            // alterations in the data, like having to add an address that was not provided
            // we would like to allocate extra space for the address
            buffer = malloc(bytesRead + sizeof(struct sockaddr_lc));
            if (!buffer) {
                ERROR("[socket] [local] [send_packet] out of memory, failed to allocate buffer");
                return OS_EOOM;
            }
            
            streambuffer_read_packet_data(buffer, bytesRead, &packetCtx);
            streambuffer_read_packet_end(&packetCtx);
        }
        else {
            doRead = 1;
        }

        targetSocket = ProcessSocketPacket(Socket, buffer, bytesRead);
        if (targetSocket) {
            streambuffer_t* TargetStream = GetSocketRecvStream(targetSocket);
            size_t          BytesWritten = streambuffer_write_packet_start(
                    TargetStream,
                    bytesRead,
                    &(streambuffer_rw_options_t) {
                            .flags = STREAMBUFFER_NO_BLOCK,
                            .async_context = NULL,
                            .deadline = NULL
                    },
                    &packetCtx
            );
            if (!BytesWritten) {
                WARNING("[socket] [local] [send_packet] ran out of space in target stream, requested %" PRIuIN, bytesRead);
                SocketSetQueuedPacket(Socket, buffer, bytesRead);
                break;
            }
            
            streambuffer_write_packet_data(buffer, bytesRead, &packetCtx);
            streambuffer_write_packet_end(&packetCtx);
            OSNotificationQueuePost((uuid_t)(uintptr_t)targetSocket->Header.key, IOSETIN);
        }
        else {
            WARNING("[socket] [local] [send_packet] target was not found");
        }
        free(buffer);
    }
    return OS_EOK;
}

static oserr_t
DomainLocalSend(
    _In_ Socket_t* Socket)
{
    TRACE("[socket] [local] [send]");
    return LocalTypeHandlers[Socket->Type](Socket);
}

static oserr_t
DomainLocalReceive(
    _In_ Socket_t* Socket)
{
    TRACE("DomainLocalReceive()");
    return OS_ENOTSUPPORTED;
}

static oserr_t
DomainLocalPair(
    _In_ Socket_t* Socket1,
    _In_ Socket_t* Socket2)
{
    Socket1->Domain->ConnectedSocket = (uuid_t)(uintptr_t)Socket2->Header.key;
    Socket2->Domain->ConnectedSocket = (uuid_t)(uintptr_t)Socket1->Header.key;
    return OS_EOK;
}

static oserr_t
DomainLocalAllocateAddress(
    _In_ Socket_t* Socket)
{
    AddressRecord_t* Record;
    char             AddressBuffer[16];
    TRACE("[socket] [local] allocate address 0x%" PRIxIN " [%u]", 
        Socket, (uuid_t)(uintptr_t)Socket->Header.key);
    
    if (Socket->Domain->Record) {
        ERROR("[socket] [local] domain address 0x%" PRIxIN " already registered",
            Socket->Domain->Record);
        return OS_EEXISTS;
    }
    
    Record = malloc(sizeof(AddressRecord_t));
    if (!Record) {
        return OS_EOOM;
    }
    
    // Create a new address of the form /lc/{id}
    sprintf(&AddressBuffer[0], "/lc/%u", (uuid_t)(uintptr_t)Socket->Header.key);
    TRACE("[socket] [local] address created %s", &AddressBuffer[0]);
    if (list_find(&AddressRegister, &AddressBuffer[0]) != NULL) {
        ERROR("[socket] [local] address %s exists in register", &AddressBuffer[0]);
        free(Record);
        return OS_EEXISTS;
    }
    
    ELEMENT_INIT(&Record->Header, strdup(&AddressBuffer[0]), Record);
    Record->Socket = Socket;
    Socket->Domain->Record = Record;
    list_append(&AddressRegister, &Record->Header);
    return OS_EOK;
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

static oserr_t
DomainLocalBind(
    _In_ Socket_t*              Socket,
    _In_ const struct sockaddr* Address)
{
    char* PreviousBuffer;
    TRACE("[domain] [local] [bind] %s", &Address->sa_data[0]);
    
    if (!Socket->Domain->Record) {
        ERROR("[domain] [local] [bind] no record");
        return OS_EUNKNOWN; // Should not happen tho
    }
    
    if (list_find(&AddressRegister, (void*)&Address->sa_data[0]) != NULL) {
        ERROR("[domain] [local] [bind] address already bound");
        return OS_EEXISTS;
    }
    
    // Update key
    PreviousBuffer = (char*)Socket->Domain->Record->Header.key;
    Socket->Domain->Record->Header.key = strdup(&Address->sa_data[0]);
    free(PreviousBuffer);
    return OS_EOK;
}

static ConnectionRequest_t*
CreateConnectionRequest(
        _In_ uuid_t                 sourceSocketHandle,
        _In_ struct gracht_message* message)
{
    ConnectionRequest_t* request = malloc(sizeof(ConnectionRequest_t) + GRACHT_MESSAGE_DEFERRABLE_SIZE(message));
    if (!request) {
        return NULL;
    }
    
    ELEMENT_INIT(&request->Header, 0, request);
    request->SourceSocketHandle = sourceSocketHandle;
    gracht_server_defer_message(message, &request->Response[0]);
    return request;
}

static AcceptRequest_t*
CreateAcceptRequest(
    _In_ struct gracht_message* message)
{
    AcceptRequest_t* request = malloc(sizeof(AcceptRequest_t) + GRACHT_MESSAGE_DEFERRABLE_SIZE(message));
    if (!request) {
        return NULL;
    }
    
    ELEMENT_INIT(&request->Header, 0, request);
    gracht_server_defer_message(message, &request->Response[0]);
    return request;
}

static void
AcceptConnectionRequest(
    _In_ struct gracht_message* acceptMessage,
    _In_ Socket_t*              connectSocket,
    _In_ struct gracht_message* connectMessage)
{
    uuid_t                  handle, recv_handle, send_handle;
    struct sockaddr_storage address;
    oserr_t              status;
    
    TRACE("[net_manager] [accept_request]");
    
    // Get address of the connector socket
    DomainLocalGetAddress(connectSocket, SYS_ADDRESS_TYPE_THIS, (struct sockaddr*)&address);
    
    // Create a new socket for the acceptor. This socket will be paired with
    // the connector socket.
    status = NetworkManagerSocketCreate(connectSocket->DomainType,
        connectSocket->Type, connectSocket->Protocol, &handle,
        &recv_handle, &send_handle);
    if (status == OS_EOK) {
        NetworkManagerSocketPair(handle, (uuid_t)(uintptr_t)connectSocket->Header.key);
    }
    
    // Reply to the connector (the thread that called connect())
    sys_socket_connect_response(connectMessage, status);
    
    // Reply to the accepter (the thread that called accept())
    sys_socket_accept_response(acceptMessage, status, (uint8_t*)&address, address.__ss_len,
        handle, recv_handle, send_handle);
}

static oserr_t
HandleLocalConnectionRequest(
    _In_ struct gracht_message* message,
    _In_ Socket_t*              sourceSocket,
    _In_ Socket_t*              targetSocket)
{
    ConnectionRequest_t* connectionRequest;
    AcceptRequest_t*     acceptRequest;
    element_t*           element;
    
    TRACE("[domain] [local] [handle_connect] %u => %u",
        LODWORD(sourceSocket->Header.key), LODWORD(targetSocket->Header.key));
    
    // Check for active accept requests, otherwise we need to queue it up. If the backlog
    // is full, we need to reject the connection request.
    mtx_lock(&targetSocket->SyncObject);
    element = queue_pop(&targetSocket->AcceptRequests);
    if (!element) {
        TRACE("[domain] [local] [handle_connect] creating request");
        connectionRequest = CreateConnectionRequest((uuid_t)(uintptr_t)sourceSocket->Header.key, message);
        if (!connectionRequest) {
            mtx_unlock(&targetSocket->SyncObject);
            ERROR("[domain] [local] [handle_connect] failed to allocate memory for connection request");
            return OS_EOOM;
        }
        
        // TODO If the backlog is full, reject
        // return OsConnectionRefused
        queue_push(&targetSocket->ConnectionRequests, &connectionRequest->Header);
        OSNotificationQueuePost((uuid_t)(uintptr_t)targetSocket->Header.key, IOSETCTL);
    }
    mtx_unlock(&targetSocket->SyncObject);
    
    // Handle the accept request we popped earlier here, this means someone
    // has called accept() on the socket and is actively waiting
    if (element) {
        acceptRequest = element->value;
        AcceptConnectionRequest(&acceptRequest->Response[0], sourceSocket, message);
        free(acceptRequest);
    }
    return OS_EOK;
}

static oserr_t
DomainLocalConnect(
    _In_ struct gracht_message* message,
    _In_ Socket_t*              socket,
    _In_ const struct sockaddr* address)
{
    Socket_t* target = GetSocketFromAddress(address);
    TRACE("[domain] [local] [connect] %s", &address->sa_data[0]);
    
    if (!target) {
        TRACE("[domain] [local] [connect] %s did not exist", &address->sa_data[0]);
        return OS_EHOSTUNREACHABLE;
    }
    
    if (socket->Type != target->Type) {
        TRACE("[domain] [local] [connect] target is valid, but protocol was invalid source (%i, %i, %i) != target (%i, %i, %i)",
            socket->DomainType, socket->Type, socket->Protocol, target->DomainType, target->Type, target->Protocol);
        return OS_EPROTOCOL;
    }
    
    if (socket->Type == SOCK_STREAM || socket->Type == SOCK_SEQPACKET) {
        return HandleLocalConnectionRequest(message, socket, target);
    }
    else {
        // Don't handle this scenario. It is handled locally in libc
        return OS_EOK;
    }
}

static oserr_t
DomainLocalDisconnect(
    _In_ Socket_t* socket)
{
    Socket_t*  peerSocket = NetworkManagerSocketGet(socket->Domain->ConnectedSocket);
    oserr_t osStatus   = OS_ENOTCONNECTED;
    TRACE("[domain] [local] [disconnect] %u => %u", LODWORD(socket->Header.key),
        LODWORD(socket->Domain->ConnectedSocket));
    
    // Send a disconnect request if socket was valid
    if (peerSocket) {
        // disconnect peer-socket as-well, and notify them of the disconnect
        peerSocket->Domain->ConnectedSocket = UUID_INVALID;
        peerSocket->Configuration.Connected = 0;
        OSNotificationQueuePost(socket->Domain->ConnectedSocket, IOSETCTL);
        osStatus = OS_EOK;
    }

    // update our stats
    socket->Domain->ConnectedSocket = UUID_INVALID;
    socket->Configuration.Connected = 0;
    return osStatus;
}

static oserr_t
DomainLocalAccept(
    _In_ struct gracht_message* message,
    _In_ Socket_t*              socket)
{
    Socket_t*            connectSocket;
    ConnectionRequest_t* connectionRequest;
    AcceptRequest_t*     acceptRequest;
    element_t*           element;
    oserr_t           status = OS_EOK;
    TRACE("[domain] [local] [accept] %u", LODWORD(socket->Header.key));
    
    // Check if there is any requests available
    mtx_lock(&socket->SyncObject);
    element = queue_pop(&socket->ConnectionRequests);
    if (element) {
        mtx_unlock(&socket->SyncObject);
        connectionRequest = element->value;
        
        // Lookup the socket handle, to check if it is still valid
        connectSocket = NetworkManagerSocketGet(connectionRequest->SourceSocketHandle);
        if (connectSocket) {
            AcceptConnectionRequest(message, connectSocket,
                &connectionRequest->Response[0]);
        }
        else {
            status = OS_ECONNABORTED;
        }
        free(connectionRequest);
    }
    else {
        // Only wait if configured to blocking, otherwise return OsBusy ish
        if (socket->Configuration.Blocking) {
            acceptRequest = CreateAcceptRequest(message);
            if (acceptRequest) {
                queue_push(&socket->AcceptRequests, &acceptRequest->Header);
            }
            else {
                status = OS_EOOM;
            }
        }
        else {
            status = OS_EBUSY; // TODO: OsTryAgain
        }
        mtx_unlock(&socket->SyncObject);
    }
    return status;
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

oserr_t
DomainLocalCreate(
    _Out_ SocketDomain_t** DomainOut)
{
    SocketDomain_t* Domain = malloc(sizeof(SocketDomain_t));
    if (!Domain) {
        return OS_EOOM;
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
    return OS_EOK;
}
