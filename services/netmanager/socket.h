/* MollenOS
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
 
#ifndef __NETMANAGER_SOCKET_H__
#define __NETMANAGER_SOCKET_H__

#include <os/osdefs.h>
#include <ddk/buffer.h>
#include <ddk/ringbuffer.h>

#define SOCKET_DEFAULT_BUFFER_SIZE (16 * 4096)

typedef enum {
    SocketCreated,
    SocketOpen,
    SocketConnected
} SocketState_t;

typedef struct {
    buffer_t*     Buffer;
    ringbuffer_t* Pipe;
} SocketPipe_t;

typedef struct {
    UUId_t        Handle;
    int           Domain;
    int           Options;
    Flags_t       Flags;
    SocketState_t State;
    
    
    SocketPipe_t  Receive;
    SocketPipe_t  Send;
} Socket_t;

/* SocketCreate
 * Creates and initializes a new socket of default options. The socket
 * will not be assigned any address, nor any resources before a bind or
 * connect operation is performed. */
OsStatus_t
SocketCreate(
    _In_  int     Domain,
    _In_  int     Options,
    _Out_ UUId_t* Handle);

/* SocketShutdown
 * Shutsdown or closes certain aspects (or all) of a socket. This will also
 * close down any active connections, and notify of disconnect. */
OsStatus_t
SocketShutdown(
    _In_ UUId_t Socket,
    _In_ int    Options);

/* SocketBind
 * Binds a socket to an address and allow others to 'look it up' for 
 * communication. This must be performed before certain other operations. */


/* SocketConnect
 * Connect the socket to an address. If any socket is listening on the address
 * the socket will be passed on to the listening party. */

/* SocketWaitForConnection
 * Waits for a new connection on the socket. The socket will return the client
 * socket that can be used by both parties to communicate. */


#endif //!__NETMANAGER_SOCKET_H__
