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

struct socket_link {
    struct link_ops         ops;
    struct sockaddr_storage address;
    int                     iod;
};

struct socket_link_manager {
    struct server_link_ops             ops;
    struct socket_server_configuration config;
    
    int client_socket;
    int dgram_socket;
};

static int socket_link_send(struct socket_link* linkContext,
    struct gracht_message* message, unsigned int flags)
{
    struct iovec  iov[1 + message->header.param_in];
    int           i;
    intmax_t      bytesWritten;
    struct msghdr msg = {
        .msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &iov[0],
        .msg_iovlen = 1 + message->header.param_in,
        .msg_control = NULL,
        .msg_controllen = 0,
        .msg_flags = 0
    };
    
    // Prepare the header
    iov[0].iov_base = message;
    iov[0].iov_len  = sizeof(struct gracht_message) + (message->header.param_in * sizeof(struct gracht_param));
    
    // Prepare the parameters
    for (i = 0; i < message->header.param_in; i++) {
        iov[1 + i].iov_len    = message->params[i].length;
        if (message->params[i].type == GRACHT_PARAM_VALUE) {
            iov[1 + i].iov_base = (void*)&message->params[i].data.value;
        }
        else if (message->params[i].type == GRACHT_PARAM_BUFFER) {
            iov[1 + i].iov_base = message->params[i].data.buffer;
        }
        else if (message->params[i].type == GRACHT_PARAM_SHM) {
            // NO SUPPORT
            assert(0);
        }
    }

    TRACE("[socket_link_send] sending message\n");
    bytesWritten = sendmsg(linkContext->iod, &msg, MSG_WAITALL);
    if (bytesWritten != message->header.length) {
        return -1;
    }

    return 0;
}

static int socket_link_recv(struct socket_link* linkContext,
    struct gracht_recv_message* context, unsigned int flags)
{
    struct gracht_message* message        = context->storage;
    char*                  params_storage = NULL;
    size_t                 bytes_read;
    
    TRACE("[gracht_connection_recv_stream] reading message header\n");
    bytes_read = recv(linkContext->iod, message, sizeof(struct gracht_message), flags);
    if (bytes_read != sizeof(struct gracht_message)) {
        if (bytes_read == 0) {
            errno = (ENODATA);
        }
        return -1;
    }
    
    if (message->header.param_in) {
        intmax_t bytesToRead = message->header.length - sizeof(struct gracht_message);

        TRACE("[gracht_connection_recv_stream] reading message payload");
        params_storage = (char*)context->storage + sizeof(struct gracht_message);
        bytes_read     = recv(linkContext->iod, params_storage, bytesToRead, MSG_WAITALL);
        if (bytes_read != bytesToRead) {
            // do not process incomplete requests
            // TODO error code / handling
            ERROR("[gracht_connection_recv_message] did not read full amount of bytes (%u, expected %u)",
                  (uint32_t)bytes_read, (uint32_t)(message->header.length - sizeof(struct gracht_message)));
            errno = (EPIPE);
            return -1;
        }
    }
    
    context->client      = linkContext->iod;
    context->params      = (void*)params_storage;
    context->param_count = message->header.param_in;
    context->protocol    = message->header.protocol;
    context->action      = message->header.action;
    return 0;
}

static int socket_link_close(struct socket_link* linkContext)
{
    int status;
    
    if (!linkContext) {
        errno = (EINVAL);
        return -1;
    }
    
    status = close(linkContext->iod);
    free(linkContext);
    return status;
}

static int socket_link_listen(struct socket_link_manager* linkManager, int mode)
{
    int status;
    
    if (mode == LINK_LISTEN_DGRAM) {
        // Create a new socket for listening to events. They are all
        // delivered to fixed sockets on the local system.
        linkManager->dgram_socket = socket(AF_LOCAL, SOCK_DGRAM, 0);
        if (linkManager->dgram_socket < 0) {
            return -1;
        }
        
        status = bind(linkManager->dgram_socket,
            (const struct sockaddr*)&linkManager->config.dgram_address,
            linkManager->config.dgram_address_length);
        if (status) {
            return -1;
        }
        
        return linkManager->dgram_socket;
    }
    else if (mode == LINK_LISTEN_SOCKET) {
        linkManager->client_socket = socket(AF_LOCAL, SOCK_STREAM, 0);
        if (linkManager->client_socket < 0) {
            return -1;
        }
        
        status = bind(linkManager->client_socket,
            (const struct sockaddr*)&linkManager->config.server_address,
            linkManager->config.server_address_length);
        if (status) {
            return -1;
        }
        
        // Enable listening for connections, with a maximum of 2 on backlog
        status = listen(linkManager->client_socket, 2);
        if (status) {
            return -1;
        }
        
        return linkManager->client_socket;
    }
    
    errno = (ENOTSUP);
    return -1;
}

static int socket_link_accept(struct socket_link_manager* linkManager, struct link_ops** linkOut)
{
    struct socket_link* link;
    socklen_t           address_length;
    TRACE("[socket_link_accept]\n");
    
    link = (struct socket_link*)malloc(sizeof(struct socket_link));
    if (!link) {
        ERROR("link_server: failed to allocate data for link\n");
        errno = (ENOMEM);
        return -1;
    }
    
    // TODO handle disconnects in accept in netmanager
    TRACE("link_server: accepting client on socket %i\n", linkManager->client_socket);
    link->iod = accept(linkManager->client_socket,
        (struct sockaddr*)&link->address, &address_length);
    if (link->iod < 0) {
        ERROR("link_server: failed to accept client\n");
        free(link);
        return -1;
    }
    
    link->ops.send  = (link_send_fn)socket_link_send;
    link->ops.recv  = (link_recv_fn)socket_link_recv;
    link->ops.close = (link_close_fn)socket_link_close;

    *linkOut = &link->ops;
    return link->iod;
}

static int socket_link_recv_packet(struct socket_link_manager* linkManager, 
    struct gracht_recv_message* context, unsigned int flags)
{
    struct gracht_message* message        = (struct gracht_message*)(
        (char*)context->storage + linkManager->config.dgram_address_length);
    void*                  params_storage = NULL;

    struct iovec iov[1] = { {
            .iov_base = message,
            .iov_len  = GRACHT_MAX_MESSAGE_SIZE - linkManager->config.dgram_address_length
        }
    };
    
    struct msghdr msg = {
        .msg_name       = context->storage,
        .msg_namelen    = linkManager->config.dgram_address_length,
        .msg_iov        = &iov[0],
        .msg_iovlen     = 1,
        .msg_control    = NULL,
        .msg_controllen = 0,
        .msg_flags      = 0
    };
    
    // Packets are atomic, either the full packet is there, or none is. So avoid
    // the use of MSG_WAITALL here.
    intmax_t bytes_read = recvmsg(linkManager->dgram_socket, &msg, flags);
    if (bytes_read <= 0) {
        if (bytes_read == 0) {
            errno = (ENODATA);
        }
        return -1;
    }

    TRACE("[gracht_connection_recv_stream] read [%u/%u] addr bytes, %p\n",
            msg.msg_namelen, linkManager->config.dgram_address_length,
            msg.msg_name);
    TRACE("[gracht_connection_recv_stream] read %lu bytes, %u\n", bytes_read, msg.msg_flags);
    TRACE("[gracht_connection_recv_stream] parameter offset %lu\n", (uintptr_t)&message->params[0] - (uintptr_t)message);
    if (message->header.param_in) {
        params_storage = &message->params[0];
    }
    
    context->client      = linkManager->dgram_socket;
    context->params      = (void*)params_storage;
    context->param_count = message->header.param_in;
    context->protocol    = message->header.protocol;
    context->action      = message->header.action;
    return 0;
}

static int socket_link_respond(struct socket_link_manager* linkManager,
    struct gracht_recv_message* messageContext, struct gracht_message* message)
{
    struct iovec  iov[1 + message->header.param_in];
    int           i;
    intmax_t      bytesWritten;
    struct msghdr msg = {
        .msg_name = messageContext->storage,
        .msg_namelen = linkManager->config.dgram_address_length,
        .msg_iov = &iov[0],
        .msg_iovlen = 1 + message->header.param_in,
        .msg_control = NULL,
        .msg_controllen = 0,
        .msg_flags = 0
    };
    
    // Prepare the header
    iov[0].iov_base = message;
    iov[0].iov_len  = sizeof(struct gracht_message) + (message->header.param_in * sizeof(struct gracht_param));
    
    // Prepare the parameters
    for (i = 0; i < message->header.param_in; i++) {
        iov[1 + i].iov_len    = message->params[i].length;
        if (message->params[i].type == GRACHT_PARAM_VALUE) {
            iov[1 + i].iov_base = (void*)&message->params[i].data.value;
        }
        else if (message->params[i].type == GRACHT_PARAM_BUFFER) {
            iov[1 + i].iov_base = message->params[i].data.buffer;
        }
        else if (message->params[i].type == GRACHT_PARAM_SHM) {
            // NO SUPPORT
            assert(0);
        }
    }
    
    bytesWritten = sendmsg(linkManager->dgram_socket, &msg, MSG_WAITALL);
    if (bytesWritten != message->header.length) {
        ERROR("link_server: failed to respond [%li/%i]\n", bytesWritten, message->header.length);
        if (bytesWritten == -1) {
            ERROR("link_server: errno %i\n", errno);
        }
        return -1;
    }

    return 0;
}

static void socket_link_destroy(struct socket_link_manager* linkManager)
{
    if (!linkManager) {
        return;
    }
    
    if (linkManager->dgram_socket > 0) {
        close(linkManager->dgram_socket);
    }
    
    if (linkManager->client_socket > 0) {
        close(linkManager->client_socket);
    }
    
    free(linkManager);
}

int gracht_link_socket_server_create(struct server_link_ops** linkOut, 
    struct socket_server_configuration* configuration)
{
    struct socket_link_manager* linkManager;
    
    linkManager = (struct socket_link_manager*)malloc(sizeof(struct socket_link_manager));
    if (!linkManager) {
        errno = (ENOMEM);
        return -1;
    }
    
    memset(linkManager, 0, sizeof(struct socket_link_manager));
    memcpy(&linkManager->config, configuration, sizeof(struct socket_server_configuration));
    
    linkManager->ops.listen      = (server_link_listen_fn)socket_link_listen;
    linkManager->ops.accept      = (server_link_accept_fn)socket_link_accept;
    linkManager->ops.recv_packet = (server_link_recv_packet_fn)socket_link_recv_packet;
    linkManager->ops.respond     = (server_link_respond_fn)socket_link_respond;
    linkManager->ops.destroy     = (server_link_destroy_fn)socket_link_destroy;
    
    *linkOut = &linkManager->ops;
    return 0;
}
