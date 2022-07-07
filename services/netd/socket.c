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
 * Network Manager (Socket interface)
 * - Contains the implementation of the socket infrastructure in the network
 *   manager. There a lot of different types of sockets, like internet, ipc
 *   and bluetooth to name the popular ones.
 */
//#define __TRACE

#include <ddk/handle.h>
#include <ddk/utils.h>
#include "domains/domains.h"
#include "socket.h"
#include <stdlib.h>
#include <string.h>

#include "sys_socket_service_server.h"

// TODO send pipe should have STREAMBUFFER_MULTIPLE_WRITERS only
// TODO recv pipe should have STREAMBUFFER_MULTIPLE_READERS only
static void
InitializeStreambuffer(
    _In_ streambuffer_t* Stream)
{
    size_t       ActualBufferSize = SOCKET_DEFAULT_BUFFER_SIZE - sizeof(streambuffer_t);
    unsigned int BufferOptions    = STREAMBUFFER_MULTIPLE_READERS | STREAMBUFFER_MULTIPLE_WRITERS | STREAMBUFFER_GLOBAL;
    streambuffer_construct(Stream, ActualBufferSize, BufferOptions);
}

static oscode_t
CreateSocketPipe(
    _In_ SocketPipe_t* Pipe)
{
    struct dma_buffer_info Buffer;
    oscode_t             Status;
    TRACE("CreateSocketPipe()");
    
    Buffer.name     = "socket_buffer";
    Buffer.length   = SOCKET_DEFAULT_BUFFER_SIZE;
    Buffer.capacity = SOCKET_SYSMAX_BUFFER_SIZE; // Should be from global settings
    Buffer.flags    = 0;
    Buffer.type     = 0;
    
    Status = dma_create(&Buffer, &Pipe->DmaAttachment);
    if (Status != OsOK) {
        return Status;
    }
    
    Pipe->Stream = Pipe->DmaAttachment.buffer;
    InitializeStreambuffer(Pipe->Stream);
    return OsOK;
}

static void
DestroySocketPipe(
    _In_ SocketPipe_t* Pipe)
{
    (void)dma_attachment_unmap(&Pipe->DmaAttachment);
    (void)dma_detach(&Pipe->DmaAttachment);
}

static void
SetDefaultConfiguration(
    _In_ SocketConfiguration_t* Configuration)
{
    // Only set non-zero members
    Configuration->Blocking = 1;
}

oscode_t
SocketCreateImpl(
    _In_  int        Domain,
    _In_  int        Type,
    _In_  int        Protocol,
    _Out_ Socket_t** SocketOut)
{
    Socket_t*  Socket;
    oscode_t Status;
    uuid_t     Handle;
    TRACE("[net_manager] [socket_create_impl] %i, %i, %i", 
        Domain, Type, Protocol);
    
    Socket = malloc(sizeof(Socket_t));
    if (!Socket) {
        return OsOutOfMemory;
    }
    
    memset(Socket, 0, sizeof(Socket_t));
    Socket->PendingPackets      = ATOMIC_VAR_INIT(0);
    Socket->DomainType          = Domain;
    Socket->Type                = Type;
    Socket->Protocol            = Protocol;
    SetDefaultConfiguration(&Socket->Configuration);
    
    mtx_init(&Socket->SyncObject, mtx_plain);
    queue_construct(&Socket->ConnectionRequests);
    queue_construct(&Socket->AcceptRequests);
    
    Status = handle_create(&Handle);
    if (Status != OsOK) {
        ERROR("Failed to create socket handle");
        return Status;
    }
    RB_LEAF_INIT(&Socket->Header, Handle, Socket);
    
    Status = DomainCreate(Domain, &Socket->Domain);
    if (Status != OsOK) {
        ERROR("Failed to initialize the socket domain");
        (void)handle_destroy(Handle);
        free(Socket);
        return Status;
    }
    
    Status = DomainAllocateAddress(Socket);
    if (Status != OsOK) {
        ERROR("Failed to initialize the socket domain address");
        DomainDestroy(Socket->Domain);
        (void)handle_destroy(Handle);
        free(Socket);
        return Status;
    }
    
    Status = CreateSocketPipe(&Socket->Receive);
    if (Status != OsOK) {
        ERROR("Failed to initialize the socket receive pipe");
        DomainDestroy(Socket->Domain);
        (void)handle_destroy(Handle);
        free(Socket);
        return Status;
    }
    
    Status = CreateSocketPipe(&Socket->Send);
    if (Status != OsOK) {
        ERROR("Failed to initialize the socket send pipe");
        DomainDestroy(Socket->Domain);
        DestroySocketPipe(&Socket->Receive);
        (void)handle_destroy(Handle);
        free(Socket);
        return Status;
    }
    
    *SocketOut = Socket;
    return OsOK;
}

oscode_t
SocketShutdownImpl(
    _In_ Socket_t* Socket,
    _In_ int       Options)
{
    mtx_lock(&Socket->SyncObject);
    if (Options & SYS_CLOSE_OPTIONS_DESTROY) {
        if (Socket->Configuration.Connected) {
            DomainDisconnect(Socket);
        }
        
        mtx_destroy(&Socket->SyncObject);
        DomainDestroy(Socket->Domain);
        DestroySocketPipe(&Socket->Receive);
        DestroySocketPipe(&Socket->Send);
        (void)handle_destroy((uuid_t)(uintptr_t)Socket->Header.key);
        free(Socket);
        return OsOK;
    }
    else {
        if (Options & SYS_CLOSE_OPTIONS_WRITE) {
            // Disable pipe
            // TODO
        }
        
        if (Options & SYS_CLOSE_OPTIONS_READ) {
            // Disable pipe
            // TODO
        }
    }
    mtx_unlock(&Socket->SyncObject);
    return OsNotSupported;
}

oscode_t
SocketListenImpl(
    _In_ Socket_t* Socket,
    _In_ int       ConnectionCount)
{
    if (ConnectionCount < 0) {
        return OsInvalidParameters;
    }
    
    Socket->Configuration.Passive = 1;
    Socket->Configuration.Backlog = ConnectionCount;
    return OsOK;
}

oscode_t
SetSocketOptionImpl(
    _In_ Socket_t*        Socket,
    _In_ int              Protocol,
    _In_ unsigned int     Option,
    _In_ const void*      Data,
    _In_ socklen_t        DataLength)
{
    return OsNotSupported;
}
    
oscode_t
GetSocketOptionImpl(
    _In_ Socket_t*         Socket,
    _In_  int              Protocol,
    _In_  unsigned int     Option,
    _In_  void*            Data,
    _Out_ socklen_t*       DataLengthOut)
{
    return OsNotSupported;
}

streambuffer_t*
GetSocketSendStream(
    _In_ Socket_t* Socket)
{
    return (streambuffer_t*)Socket->Send.Stream;
}

streambuffer_t*
GetSocketRecvStream(
    _In_ Socket_t* Socket)
{
    return (streambuffer_t*)Socket->Receive.Stream;
}

oscode_t
SocketSetQueuedPacket(
    _In_ Socket_t*   Socket,
    _In_ const void* Payload,
    _In_ size_t      Length)
{
    Socket->QueuedPacket.Length = Length;
    Socket->QueuedPacket.Data   = (void*)Payload;
    return OsOK;
}

size_t
SocketGetQueuedPacket(
    _In_ Socket_t* Socket,
    _In_ void**    Buffer)
{
    size_t Length = Socket->QueuedPacket.Length;
    if (Length) {
        *Buffer = (void*)Socket->QueuedPacket.Data;
        Socket->QueuedPacket.Data   = NULL;
        Socket->QueuedPacket.Length = 0;
    }
    return Length;
}
