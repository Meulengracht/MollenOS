/**
 * Copyright 2023, Philip Meulengracht
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
 * Net Service Definitions & Structures
 * - This header describes the base structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __OS_SERVICE_NET_H__
#define __OS_SERVICE_NET_H__

#include <ds/streambuffer.h>
#include <os/types/net.h>
#include <os/types/handle.h>

_CODE_BEGIN

/**
 * @brief Creates a socket.
 * @param domain Address family for the socket.
 * @param type Socket type.
 * @param protocol Protocol to use, or zero for the default.
 * @param handleOut Receives the socket handle.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSSocketOpen(
        _In_  int         domain,
        _In_  int         type,
        _In_  int         protocol,
        _Out_ OSHandle_t* handleOut));

/**
 * @brief Creates a connected pair of sockets.
 * @param sock0 Receives the first socket handle.
 * @param sock1 Receives the second socket handle.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSSocketPair(
        _In_  OSHandle_t* sock0,
        _In_  OSHandle_t* sock1));

/**
 * @brief Accepts an incoming connection on a listening socket.
 * @param handle Listening socket handle.
 * @param address Receives the peer address.
 * @param addressLength In/out size of address, in bytes.
 * @param handleOut Receives the accepted socket handle.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSSocketAccept(
        _In_  OSHandle_t*      handle,
        _In_  struct sockaddr* address,
        _In_  socklen_t*       addressLength,
        _Out_ OSHandle_t*      handleOut));

/**
 * @brief Binds a socket to a local address.
 * @param handle Socket handle to bind.
 * @param address Local address.
 * @param addressLength Length of address, in bytes.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSSocketBind(
        _In_ OSHandle_t*            handle,
        _In_ const struct sockaddr* address,
        _In_ socklen_t              addressLength));

/**
 * @brief Connects a socket to a remote address.
 * @param handle Socket handle to connect.
 * @param address Remote address.
 * @param addressLength Length of address, in bytes.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSSocketConnect(
        _In_ OSHandle_t*            handle,
        _In_ const struct sockaddr* address,
        _In_ socklen_t              addressLength));

/**
 * @brief Marks a socket as listening for incoming connections.
 * @param handle Socket handle to listen on.
 * @param queueSize Maximum pending connection queue size.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSSocketListen(
        _In_ OSHandle_t* handle,
        _In_ int         queueSize));

/**
 * @brief Retrieves a socket address.
 * @param handle Socket handle to query.
 * @param type Address type to retrieve.
 * @param address Receives the socket address.
 * @param addressMaxSize Capacity of address in bytes.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSSocketAddress(
        _In_ OSHandle_t*      handle,
        _In_ int              type,
        _In_ struct sockaddr* address,
        _In_ socklen_t        addressMaxSize));

/**
 * @brief Sets a socket option.
 * @param handle Socket handle to modify.
 * @param protocol Protocol that owns the option.
 * @param option Option to set.
 * @param data Option value.
 * @param length Size of data in bytes.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSSocketSetOption(
        _In_ OSHandle_t* handle,
        _In_ int         protocol,
        _In_ int         option,
        _In_ const void* data,
        _In_ socklen_t   length));

/**
 * @brief Retrieves a socket option.
 * @param handle Socket handle to query.
 * @param protocol Protocol that owns the option.
 * @param option Option to retrieve.
 * @param data Receives the option value.
 * @param length In/out size of data in bytes.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSSocketOption(
        _In_    OSHandle_t* handle,
        _In_    int         protocol,
        _In_    int         option,
        _In_    void*       data,
        _InOut_ socklen_t*  length));

/**
 * @brief Creates a stream pipe connected to a socket.
 * @param handle Socket handle to use.
 * @param pipeOut Receives the stream buffer pipe.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSSocketSendPipe(
        _In_  OSHandle_t*      handle,
        _Out_ streambuffer_t** pipeOut));

/**
 * @brief Sends a message through a socket.
 * @param handle Socket handle to send through.
 * @param message Message to send.
 * @param flags Send behavior flags.
 * @param bytesSentOut Receives the number of bytes sent.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSSocketSend(
        _In_  OSHandle_t*          handle,
        _In_  const struct msghdr* message,
        _In_  int                  flags,
        _Out_ size_t*              bytesSentOut));

/**
 * @brief Creates a stream pipe connected to a socket for receiving data.
 * @param handle Socket handle to use.
 * @param pipeOut Receives the stream buffer pipe.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSSocketRecvPipe(
        _In_  OSHandle_t*      handle,
        _Out_ streambuffer_t** pipeOut));

/**
 * @brief Receives a message from a socket.
 * @param handle Socket handle to receive from.
 * @param message Receives the message.
 * @param flags Receive behavior flags.
 * @param bytesRecievedOut Receives the number of bytes received.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSSocketRecv(
        _In_  OSHandle_t*    handle,
        _In_  struct msghdr* message,
        _In_  int            flags,
        _Out_ size_t*        bytesRecievedOut));

_CODE_END
#endif //!__OS_SERVICE_NET_H__
