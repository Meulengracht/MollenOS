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

#include <ds/streambuffer.h>
#include <errno.h>
#include <gracht/link/socket.h>

static struct socket_link {
    struct link_ops         ops;
    struct sockaddr_storage address;
    int                     iod;
};

static struct socket_link_manager {
    struct link_manager_ops          ops;
    struct socket_link_configuration config;
    
    int client_socket;
    int dgram_socket;
};

static int socket_link_send(struct socket_link* linkContext,
    struct gracht_message* message, unsigned int flags)
{
    struct iovec  iov[1 + message->param_count];
    int           i;
    struct msghdr msg = {
        NULL, 0, &iov[0], message->param_count, NULL, 0, 0
    };
    
    // Prepare the header
    iov[0].iov_base = message;
    iov[0].iov_len  = sizeof(struct gracht_message);
    
    // Prepare the parameters
    for (i = 1; i < message->param_count; i++) {
        iov[i].iov_len = message->params[i].length;
        if (message->params[i].type == PARAM_VALUE) {
            iov[i].iov_base = (void*)&message->params[i].value;
        }
        else if (message->params[i].type == PARAM_BUFFER) {
            iov[i].iov_base = message->params[i].buffer;
        }
        else if (message->params[i].type == PARAM_SHM) {
            // NO SUPPORT
            assert(0);
        }
    }
    
    intmax_t bytes_written = sendmsg(linkContext->iod, &msg, MSG_WAITALL);
    return bytes_written != message->length;
}

static int socket_link_recv(struct socket_link* linkContext,
    struct gracht_recv_message* messageContext, unsigned int flags)
{
    struct gracht_message* message        = messageContext->storage;
    char*                  params_storage = NULL;
    
    TRACE("[gracht_connection_recv_stream] reading message header");
    bytes_read = recv(linkContext->iod, message, sizeof(struct gracht_message), flags);
    if (bytes_read != sizeof(struct gracht_message)) {
        if (bytes_read == 0) {
            _set_errno(ENODATA);
        }
        else {
            _set_errno(EPIPE);
        }
        return -1;
    }
    
    if (message->Base.param_count) {
        TRACE("[gracht_connection_recv_stream] reading message payload");
        params_storage = (char*)storage + sizeof(struct gracht_message);
        bytes_read     = recv(linkContext->iod, params_storage, message->length - sizeof(struct gracht_message), MSG_WAITALL);
        if (bytes_read != message->length - sizeof(struct gracht_message)) {
            // do not process incomplete requests
            // TODO error code / handling
            ERROR("[gracht_connection_recv_message] did not read full amount of bytes (%" 
                PRIuIN ", expected %" PRIuIN ")",
                bytes_read, message->length - sizeof(struct gracht_message));
            _set_errno(EPIPE);
            return -1; 
        }
    }
    
    context->params      = params_storage;
    context->param_count = message->param_in;
    context->protocol    = message->protocol;
    context->action      = message->action;
    return 0;
}

static int socket_link_close(struct socket_link* linkContext)
{
    int status;
    
    if (!linkContext) {
        _set_errno(EINVAL);
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
        
        status = bind(linkManager->dgram_socket, sstosa(&linkManager->config.dgram_address),
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
        
        status = bind(linkManager->client_socket, sstosa(&linkManager->config.server_address),
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
    
    _set_errno(ENOTSUPP);
    return -1;
}

static int socket_link_accept(struct socket_link_manager* linkManager, struct link_ops** linkOut)
{
    struct link_ops* link;
    socklen_t        address_length;
    int              client_socket;
    int              status;
    
    link = (struct link_ops*)malloc(sizeof(struct link_ops));
    if (!link) {
        _set_errno(ENOMEM);
        return -1;
    }
    
    // TODO handle disconnects in accept in netmanager
    link->iod = accept(linkManager->client_socket, sstosa(&link->address), &address_length);
    if (link->iod < 0) {
        free(link);
        return -1;
    }
    
    link->ops.send  = socket_link_send;
    link->ops.recv  = socket_link_recv;
    link->ops.close = socket_link_close;
    
    *linkOut = link;
    return 0;
}

static int socket_link_recv_packet(struct socket_link_manager* linkManager, 
    struct gracht_recv_message* context, unsigned int flags)
{
    struct gracht_message* message        = (struct gracht_message*)((char*)context->storage + sizeof(struct sockaddr_lc));
    void*                  params_storage = NULL;
    
    struct iovec iov[1] = { 
        { .iov_base = context->storage, .iov_len = GRACHT_MAX_MESSAGE_SIZE }
    };
    
    struct msghdr msg = {
        .msg_name       = context->storage,
        .msg_namelen    = sizeof(struct sockaddr_lc),
        .msg_iov        = &iov[0],
        .msg_iovlen     = 1,
        .msg_control    = NULL,
        .msg_controllen = 0,
        .msg_flags      = 0
    };
    
    // Packets are atomic, either the full packet is there, or none is. So avoid
    // the use of MSG_WAITALL here.
    intmax_t bytes_read = recvmsg(linkManager->dgram_socket, &msg, flags);
    if (bytes_read < sizeof(struct gracht_message)) {
        if (bytes_read == 0) {
            _set_errno(ENODATA);
        }
        else {
            _set_errno(EPIPE);
        }
        return -1;
    }
    
    if (message->Base.param_count) {
        TRACE("[gracht_connection_recv_stream] reading message payload");
        params_storage = (char*)message + sizeof(struct gracht_message);
    }
    
    context->parameters  = (void*)params_storage;
    context->param_count = message->param_in;
    context->protocol    = message->protocol;
    context->action      = message->action;
    return 0;
}

static int socket_link_respond(struct socket_link_manager* linkManager,
    struct gracht_recv_message* messageContext, struct gracht_message* message)
{
    TRACE("[gracht_connection_send_reply] %i, %" PRIuIN, linkManager->dgram_socket, length);
    intmax_t bytes_written = sendto(linkManager->dgram_socket, buffer, length, MSG_WAITALL,
        (const struct sockaddr*)sstosa(&messageContext->address), messageContext->address.__ss_len);
    if (bytes_written <= 0) {
        ERROR("[gracht_connection_send_reply] send returned -1, error %i", errno);
    }
    TRACE("[gracht_connection_send_reply] bytes sent = %i", bytes_written);
    return (bytes_written != length); // return 0 on ok
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
        _set_errno(ENOMEM);
        return -1;
    }
    
    memset(linkManager, 0, sizeof(struct socket_link_manager));
    memcpy(&linkManager->config, configuration, sizeof(struct socket_server_configuration));
    
    linkManager->ops.listen      = socket_link_listen;
    linkManager->ops.accept      = socket_link_accept;
    linkManager->ops.recv_packet = socket_link_recv_packet;
    linkManager->ops.respond     = socket_link_respond;
    linkManager->ops.destroy     = socket_link_destroy;
    
    *linkOut = &linkManager->ops;
    return 0;
}
