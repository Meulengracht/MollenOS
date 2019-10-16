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

#include <ddk/handle.h>
#include "domains/domains.h"
#include "socket.h"
#include <stdlib.h>

static OsStatus_t
CreateSocketPipe(
    _In_ SocketPipe_t* Pipe)
{
    struct dma_buffer_info Buffer;
    OsStatus_t             Status;
    
    Buffer.name     = "socket_buffer";
    Buffer.length   = SOCKET_DEFAULT_BUFFER_SIZE;
    Buffer.capacity = SOCKET_SYSMAX_BUFFER_SIZE; // Should be from global settings
    Buffer.flags    = 0;
    
    Status = dma_create(&Buffer, &Pipe->DmaAttachment);
    if (Status != OsSuccess) {
        return Status;
    }
    
    Pipe->Stream = Pipe->DmaAttachment.buffer;
    return OsSuccess;
}

static void
DestroySocketPipe(
    _In_ SocketPipe_t* Pipe)
{
    (void)dma_attachment_unmap(&Pipe->DmaAttachment);
    (void)dma_detach(&Pipe->DmaAttachment);
}

OsStatus_t
SocketCreateImpl(
    _In_  int        Domain,
    _In_  int        Type,
    _In_  int        Protocol,
    _Out_ Socket_t** SocketOut)
{
    struct sockaddr_storage Address;
    Socket_t*               Socket;
    OsStatus_t              Status;
    
    Socket = malloc(sizeof(Socket_t));
    if (!Socket) {
        return OsOutOfMemory;
    }
    
    memset(Socket, 0, sizeof(Socket_t));
    Socket->PendingPackets      = ATOMIC_VAR_INIT(0);
    Socket->Domain              = Domain;
    Socket->Type                = Type;
    Socket->Protocol            = Protocol;
    
    Status = handle_create(&Socket->Header.Key.Value.Id);
    if (Status != OsSuccess) {
        return Status;
    }
    
    Status = DomainCreate(Domain, &Socket->Domain);
    
    Status = DomainAllocateAddress(Socket);
    
    Status = CreateSocketPipe(&Socket->Receive);
    if (Status != OsSuccess) {
        free(Socket);
        return Status;
    }
    
    Status = CreateSocketPipe(&Socket->Send);
    if (Status != OsSuccess) {
        DestroySocketPipe(&Socket->Receive);
        free(Socket);
        return Status;
    }
    
    *SocketOut = Socket;
    return OsSuccess;
}

OsStatus_t
SocketShutdownImpl(
    _In_ Socket_t* Socket,
    _In_ int       Options)
{
    if (Options & SOCKET_SHUTDOWN_DESTROY) {
        DestroySocketPipe(&Socket->Receive);
        DestroySocketPipe(&Socket->Send);
        free(Socket);
        return OsSuccess;
    }
    else {
        if (Options & SOCKET_SHUTDOWN_SEND) {
            // Disable pipe
            // TODO
        }
        
        if (Options & SOCKET_SHUTDOWN_RECV) {
            // Disable pipe
            // TODO
        }
    }
    return OsNotSupported;
}

OsStatus_t
SocketListenImpl(
    _In_ Socket_t* Socket,
    _In_ int       ConnectionCount)
{
    return OsNotSupported;
}

OsStatus_t
SetSocketOptionImpl(
    _In_ Socket_t*        Socket,
    _In_ int              Protocol,
    _In_ unsigned int     Option,
    _In_ const void*      Data,
    _In_ socklen_t        DataLength)
{
    return OsNotSupported;
}
    
OsStatus_t
GetSocketOptionImpl(
    _In_ Socket_t*         Socket,
    _In_  int              Protocol,
    _In_  unsigned int     Option,
    _In_  void*            Data,
    _Out_ socklen_t*       DataLengthOut)
{
    return OsNotSupported;
}

OsStatus_t
GetSocketAddressImpl(
    _In_ Socket_t*        Socket,
    _In_ int              Source,
    _In_ struct sockaddr* Address)
{
    return OsNotSupported;
}

int GetSocketDomain(
    _In_ Socket_t* Socket)
{
    
}

int GetSocketType(
    _In_ Socket_t* Socket)
{
    
}

streambuffer_t*
GetSocketSendStream(
    _In_ Socket_t* Socket)
{
    
}

streambuffer_t*
GetSocketRecvStream(
    _In_ Socket_t* Socket)
{
    
}

OsStatus_t
SocketSetQueuedPacket(
    _In_ Socket_t*   Socket,
    _In_ const void* Payload,
    _In_ size_t      Length)
{
    Socket->QueuedPacket.Length = Length;
    Socket->QueuedPacket.Data   = malloc();
    if (Length) {
        memcpy(Socket->QueuedPacket.Data, Payload, Length);
    }
    return OsSuccess;
}

size_t
SocketGetQueuedPacket(
    _In_ Socket_t* Socket,
    _In_ void*     Buffer,
    _In_ size_t    MaxLength)
{
    if (Socket && Socket->QueuedPacket.Length) {
        memcpy(Buffer, Socket->QueuedPacket.Buffer, 
            MIN(MaxLength, Socket->QueuedPacket.Length));
        return Socket->QueuedPacket.Length;
    }
    return 0;
}
