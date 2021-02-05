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
 * Gracht Socket Link Type Definitions & Structures
 * - This header describes the base link-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <assert.h>
#include <errno.h>
#include "../include/gracht/link/socket.h"
#include "../include/gracht/debug.h"
#include <stdlib.h>
#include <string.h>

struct socket_link_manager {
    struct client_link_ops             ops;
    struct socket_client_configuration config;
    int                                iod;
};

static int socket_link_send_stream(struct socket_link_manager* linkManager,
    struct gracht_message* message)
{
    struct iovec  iov[1 + message->header.param_in];
    int           i;
    intmax_t      byteCount;
    struct msghdr msg = {
        .msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &iov[0],
        .msg_iovlen = 1,
        .msg_control = NULL,
        .msg_controllen = 0,
        .msg_flags = 0
    };
    
    // Prepare the header
    iov[0].iov_base = message;
    iov[0].iov_len  = sizeof(struct gracht_message) + (
        (message->header.param_in + message->header.param_out) * sizeof(struct gracht_param));
    
    // Prepare the parameters
    for (i = 0; i < message->header.param_in; i++) {
        if (message->params[i].type == GRACHT_PARAM_BUFFER) {
            iov[msg.msg_iovlen].iov_len  = message->params[i].length;
            iov[msg.msg_iovlen].iov_base = message->params[i].data.buffer;
            msg.msg_iovlen++;
        }
        else if (message->params[i].type == GRACHT_PARAM_SHM) {
            // NO SUPPORT
            assert(0);
        }
    }

    byteCount = sendmsg(linkManager->iod, &msg, 0);
    if (byteCount != message->header.length) {
        ERROR("link_client: failed to send message, bytes sent: %li, expected: %u (%i)\n",
              byteCount, message->header.length, errno);
        errno = (EPIPE);
        return GRACHT_MESSAGE_ERROR;
    }

    return GRACHT_MESSAGE_INPROGRESS;
}

static int socket_link_recv_stream(struct socket_link_manager* linkManager,
    void* messageBuffer, unsigned int flags, struct gracht_message** messageOut)
{
    struct gracht_message* message = messageBuffer;
    char*                  params_storage;
    size_t                 bytes_read;
    
    TRACE("[gracht_connection_recv_stream] reading message header\n");
    bytes_read = recv(linkManager->iod, message, sizeof(struct gracht_message), flags);
    if (bytes_read != sizeof(struct gracht_message)) {
        if (bytes_read == 0) {
            errno = (ENODATA);
        }
        else {
            errno = (EPIPE);
        }
        return -1;
    }
    
    if (message->header.param_in) {
        TRACE("[gracht_connection_recv_stream] reading message payload\n");
        
        params_storage = (char*)messageBuffer + sizeof(struct gracht_message);
        bytes_read     = recv(linkManager->iod, params_storage, message->header.length - sizeof(struct gracht_message), MSG_WAITALL);
        if (bytes_read != message->header.length - sizeof(struct gracht_message)) {
            // do not process incomplete requests
            ERROR("[gracht_connection_recv_message] did not read full amount of bytes (%u, expected %u)",
                  (uint32_t)bytes_read, (uint32_t)(message->header.length - sizeof(struct gracht_message)));
            errno = (EPIPE);
            return -1; 
        }
    }
    
    *messageOut = message;
    return 0;
}

static int socket_link_send_packet(struct socket_link_manager* linkManager, struct gracht_message* message)
{
    struct iovec  iov[1 + message->header.param_in];
    int           i;
    intmax_t      byteCount;
    struct msghdr msg = {
        .msg_name = NULL,
        .msg_namelen = 0, /* client only: already connected on socket */
        .msg_iov = &iov[0],
        .msg_iovlen = 1,
        .msg_control = NULL,
        .msg_controllen = 0,
        .msg_flags = 0
    };

    TRACE("link_client: send message (%u, in %i, out %i)\n",
          message->header.length, message->header.param_in, message->header.param_out);

    // Prepare the header
    iov[0].iov_base = message;
    iov[0].iov_len  = sizeof(struct gracht_message) + (
        (message->header.param_in + message->header.param_out) * sizeof(struct gracht_param));
    
    // Prepare the parameters
    for (i = 0; i < message->header.param_in; i++) {
        if (message->params[i].type == GRACHT_PARAM_BUFFER) {
            iov[msg.msg_iovlen].iov_len  = message->params[i].length;
            iov[msg.msg_iovlen].iov_base = message->params[i].data.buffer;
            msg.msg_iovlen++;
        }
        else if (message->params[i].type == GRACHT_PARAM_SHM) {
            // NO SUPPORT
            assert(0);
        }
    }
    
    byteCount = sendmsg(linkManager->iod, &msg, 0);
    if (byteCount != message->header.length) {
        ERROR("link_client: failed to send message, bytes sent: %u, expected: %u\n",
              (uint32_t)byteCount, message->header.length);
        errno = (EPIPE);
        return GRACHT_MESSAGE_ERROR;
    }

    return GRACHT_MESSAGE_INPROGRESS;
}

static int socket_link_recv_packet(struct socket_link_manager* linkManager, 
    void* messageBuffer, unsigned int flags, struct gracht_message** messageOut)
{
    struct gracht_message* message = (struct gracht_message*)((char*)messageBuffer + linkManager->config.address_length);
    
    struct iovec iov[1] = { 
        { .iov_base = message, .iov_len = GRACHT_MAX_MESSAGE_SIZE }
    };
    
    struct msghdr msg = {
        .msg_name       = messageBuffer,
        .msg_namelen    = linkManager->config.address_length,
        .msg_iov        = &iov[0],
        .msg_iovlen     = 1,
        .msg_control    = NULL,
        .msg_controllen = 0,
        .msg_flags      = 0
    };
    
    // Packets are atomic, either the full packet is there, or none is. So avoid
    // the use of MSG_WAITALL here.
    TRACE("[gracht_connection_recv_stream] reading full message");
    intmax_t bytes_read = recvmsg(linkManager->iod, &msg, flags);
    if (bytes_read < sizeof(struct gracht_message)) {
        if (bytes_read == 0) {
            errno = (ENODATA);
        }
        else {
            errno = (EPIPE);
        }
        return -1;
    }
    
    *messageOut = message;
    return 0;
}

static int socket_link_connect(struct socket_link_manager* linkManager)
{
    int type = linkManager->config.type == gracht_link_stream_based ? SOCK_STREAM : SOCK_DGRAM;
    
    linkManager->iod = socket(AF_LOCAL, type, 0);
    if (linkManager->iod < 0) {
        ERROR("client_link: failed to create socket\n");
        return -1;
    }
    
    int status = connect(linkManager->iod, 
        (const struct sockaddr*)&linkManager->config.address,
        linkManager->config.address_length);
    if (status) {
        ERROR("client_link: failed to connect to socket\n");
        close(linkManager->iod);
        return status;
    }
    return linkManager->iod;
}

static int socket_link_recv(struct socket_link_manager* linkManager,
    void* messageBuffer, unsigned int flags, struct gracht_message** messageOut)
{
    unsigned int convertedFlags = MSG_WAITALL;
    
    if (!(flags & GRACHT_WAIT_BLOCK)) {
        convertedFlags |= MSG_DONTWAIT;
    }
    
    if (linkManager->config.type == gracht_link_stream_based) {
        return socket_link_recv_stream(linkManager, messageBuffer, convertedFlags, messageOut);
    }
    else if (linkManager->config.type == gracht_link_packet_based) {
        return socket_link_recv_packet(linkManager, messageBuffer, convertedFlags, messageOut);
    }
    
    errno = (ENOTSUP);
    return -1;
}

static int socket_link_send(struct socket_link_manager* linkManager,
    struct gracht_message* message, void* messageContext)
{
    // perform length check before sending
    if (message->header.length > GRACHT_MAX_MESSAGE_SIZE) {
        errno = (E2BIG);
        return GRACHT_MESSAGE_ERROR;
    }
    
    if (linkManager->config.type == gracht_link_stream_based) {
        return socket_link_send_stream(linkManager, message);
    }
    else if (linkManager->config.type == gracht_link_packet_based) {
        return socket_link_send_packet(linkManager, message);
    }
    else
    {
        errno = (ENOTSUP);
        return GRACHT_MESSAGE_ERROR;
    }
}

static void socket_link_destroy(struct socket_link_manager* linkManager)
{
    if (!linkManager) {
        return;
    }
    
    if (linkManager->iod > 0) {
        close(linkManager->iod);
    }
    
    free(linkManager);
}

int gracht_link_socket_client_create(struct client_link_ops** linkOut, 
    struct socket_client_configuration* configuration)
{
    struct socket_link_manager* linkManager;
    
    linkManager = (struct socket_link_manager*)malloc(sizeof(struct socket_link_manager));
    if (!linkManager) {
        errno = (ENOMEM);
        return -1;
    }
    
    memset(linkManager, 0, sizeof(struct socket_link_manager));
    memcpy(&linkManager->config, configuration, sizeof(struct socket_client_configuration));

    linkManager->ops.connect     = (client_link_connect_fn)socket_link_connect;
    linkManager->ops.recv        = (client_link_recv_fn)socket_link_recv;
    linkManager->ops.send        = (client_link_send_fn)socket_link_send;
    linkManager->ops.destroy     = (client_link_destroy_fn)socket_link_destroy;
    
    *linkOut = &linkManager->ops;
    return 0;
}
