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
#include <ddk/utils.h>
#include <ds/collection.h>
#include <internal/_socket.h>
#include <io_events.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct AddressRecord {
    CollectionItem_t Header;
    Socket_t*        Socket;
} AddressRecord_t;

typedef struct SocketDomain {
    SocketDomainOps_t Ops;
    UUId_t            ConnectedSocket;
    AddressRecord_t*  Record;
} SocketDomain_t;

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
    
    TargetSocket = NetworkManagerSocketGet(Socket->Domain->ConnectedSocket);
    if (!TargetSocket) {
        return OsDoesNotExist; // What the fuck do? TODO
    }
    
    TargetStream = GetSocketRecvStream(TargetSocket);
    BytesRead    = SocketGetQueuedPacket(Socket, &TemporaryBuffer,
        sizeof(TemporaryBuffer));
    if (BytesRead) {
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
            SocketSetQueuedPacket(Socket, &TemporaryBuffer[BytesWritten], 
                BytesRead - BytesWritten);
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
    _In_ void*  PacketData,
    _In_ size_t PacketLength)
{
    struct packethdr*       Packet = (struct packethdr*)PacketData;
    Socket_t*               TargetSocket;
    struct sockaddr_storage Address;
    
        // Get header
        streambuffer_read_packet_data(SourceStream, &Packet, 
            sizeof(struct packethdr), &State);
        
        // Get address, either from packet or from stored connection
        if (Packet.addresslen) {
            streambuffer_read_packet_data(SourceStream, &Address, 
                Packet.addresslen, &State);
            TargetSocket = GetSocketFromAddress((const struct sockaddr*)&Address);
        }
        else {
            // Are we connected to a socket?
            TargetSocket = NetworkManagerSocketGet(Socket->Domain->ConnectedSocket);
        }
        
        if (Packet.controllen) {
            streambuffer_read_packet_data(SourceStream, &ControlBuffer[0], 
                Packet.controllen, &State);
            // TODO handle control data
        }
        
        if (TargetSocket && Packet.payloadlen) {
            Buffer = malloc(Packet.payloadlen);
            streambuffer_read_packet_data(SourceStream, &Buffer[0], 
                Packet.payloadlen, &State);
            
            // Get target stream
            TargetStream = GetSocketRecvStream(TargetSocket);
        }
}

static OsStatus_t
HandleSocketPacketData(
    _In_ Socket_t* Socket)
{
    streambuffer_t*         SourceStream = GetSocketSendStream(Socket);
    streambuffer_t*         TargetStream = NULL;
    unsigned int            Base, State;
    char*                   Buffer;
    
    while (1) {
        size_t BytesRead = streambuffer_read_packet_start(SourceStream, 
            STREAMBUFFER_NO_BLOCK, &Base, &State);
        if (!BytesRead) {
            break;
        }
        
        // Read the entire packet in one go, then process the data
        Buffer = malloc(BytesRead);
        streambuffer_read_packet_data(SourceStream, &Buffer[0], BytesRead, &State);
        streambuffer_read_packet_end(SourceStream, Base, BytesRead);
        
        TargetStream = ProcessSocketPacket(Buffer, BytesRead);
        if (TargetStream) {
            size_t BytesWritten = streambuffer_write_packet_start(TargetStream, BytesRead, 
                STREAMBUFFER_NO_BLOCK, &Base, &State);
            if (!BytesWritten) {
                SocketSetQueuedPacket(Socket, &Buffer[0], BytesRead);
                break;
            }    
            
            streambuffer_write_packet_data(TargetStream, &Buffer[0], BytesRead, &State);
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
    return LocalTypeHandlers[Socket->Type](Socket);
}

static OsStatus_t
DomainLocalAllocateAddress(
    _In_ Socket_t*        Socket,
    _In_ struct sockaddr* Address)
{
    AddressRecord_t* Record;
    char             AddressBuffer[16];
    
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
    CollectionRemoveByNode(&AddressRegister, &Record->Header);
    free((void*)Record->Header.Key.Value.String.Pointer);
    free(Record);
}

static void
DomainLocalFreeAddress(
    _In_ Socket_t* Socket)
{
    if (Socket->Domain->Record) {
        DestroyAddressRecord(Socket->Domain->Record);
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

static OsStatus_t
DomainLocalConnect(
    _In_ Socket_t*              Socket,
    _In_ const struct sockaddr* Address)
{
    Socket_t* Target = GetSocketFromAddress(Address);
    if (!Target) {
        return OsDoesNotExist;
    }
    
    // Create a connection request on the socket
    
    
    
    return OsNotSupported;
}

static OsStatus_t
DomainLocalAccept(
    _In_ Socket_t*        Socket,
    _In_ struct sockaddr* Address)
{
    return OsNotSupported;
}

static void
DomainLocalDestroy(
    _In_ SocketDomain_t* Domain)
{
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
    
    // Setup operations
    Domain->Ops.AddressAllocate = DomainLocalAllocateAddress;
    Domain->Ops.AddressFree     = DomainLocalFreeAddress;
    Domain->Ops.Bind            = DomainLocalBind;
    Domain->Ops.Connect         = DomainLocalConnect;
    Domain->Ops.Accept          = DomainLocalAccept;
    Domain->Ops.Send            = DomainLocalSend;
    Domain->Ops.Receive         = 0;
    Domain->Ops.Destroy         = DomainLocalDestroy;
    
    Domain->Record = NULL;
    
    *DomainOut = Domain;
    return OsSuccess;
}
