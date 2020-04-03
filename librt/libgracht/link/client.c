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

static struct socket_link_manager {
    struct client_link_ops             ops;
    struct socket_client_configuration config;
    int                                iod;
};

static int socket_link_send_stream(struct socket_link_manager* linkManager,
    struct gracht_message* message)
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
    
    intmax_t bytes_written = sendmsg(linkManager->iod, &msg, MSG_WAITALL);
    return bytes_written != message->length;
}

static int socket_link_recv_stream(struct socket_link_manager* linkManager,
    struct gracht_recv_message* messageContext, unsigned int flags)
{
    struct gracht_message* message        = messageContext->storage;
    char*                  params_storage = NULL;
    
    TRACE("[gracht_connection_recv_stream] reading message header");
    bytes_read = recv(linkManager->iod, message, sizeof(struct gracht_message), flags);
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
        bytes_read     = recv(linkManager->iod, params_storage, message->length - sizeof(struct gracht_message), MSG_WAITALL);
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
    
    context->parameters = (void*)params_storage;
    context->protocol   = message->protocol;
    context->action     = message->action;
    return 0;
}

static int socket_link_send_packet(struct socket_link_manager* linkManager,
    struct gracht_message* messageBase, void* unusedContext)
{
    struct iovec  iov[1 + message->param_in];
    struct iovec  iov_out[1 + message->param_out];
    int           i;
    intmax_t      byte_count;
    struct msghdr msg = {
        NULL, 0, /* client only: already connected on socket */ 
        &iov[0], message->param_in,
        NULL, 0, 0
    };
    
    // Prepare the header
    iov[0].iov_base = message;
    iov[0].iov_len  = sizeof(struct gracht_message);
    
    // Prepare the parameters
    for (i = 1; i < message->param_in; i++) {
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
    
    byte_count = sendmsg(linkManager->iod, &msg, MSG_WAITALL);
    if (bytes_written != message->length) {
        _set_errno(EPIPE);
        return -1;
    }
    
    if (message->param_out) {
        size_t length = 0;
        for (i = 0; i < messageBase->param_out; i++) {
            iov_out[i].iov_base = messageBase->params[messageBase->param_in + i].data.buffer;
            iov_out[i].iov_len  = messageBase->params[messageBase->param_in + i].data.length;
            length += messageBase->params[messageBase->param_in + i].data.length;
        }
        
        msg.msg_iov = &iov_out[0];
        msg.msg_iovlen = message->param_out;
        byte_count = recvmsg(linkManager->iod, &msg, MSG_WAITALL);
        if (byte_count == 0) {
            _set_errno(EPIPE);
            return -1;
        }
    }
    
    return 0;
}

static int socket_link_recv_packet(struct socket_link_manager* linkManager, 
    struct gracht_recv_message* context)
{
    struct gracht_message* message        = (struct gracht_message*)((char*)context->storage + sizeof(struct sockaddr_lc));
    void*                  params_storage = NULL;
    
    struct iovec iov[1] = { 
        { .iov_base = message, .iov_len = GRACHT_MAX_MESSAGE_SIZE }
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
    intmax_t bytes_read = recvmsg(linkManager->iod, &msg, flags);
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

static int socket_link_connect(struct socket_link_manager* linkManager)
{
    int type = linkManager->config.type == gracht_link_stream_based ? SOCK_STREAM : SOCK_DGRAM;
    
    linkManager->iod = socket(AF_LOCAL, type, 0);
    if (linkManager->iod == -1) {
        return -1;
    }
    
    int status = connect(linkManager->iod, sstosa(&linkManager->config.server_address),
        linkManager->config.server_address_length);
    if (status) {
        close(linkManager->iod);
        return status;
    }
    return linkManager->iod;
}

static int socket_link_recv(struct socket_link_manager* linkManager,
    struct gracht_recv_message* messageContext, unsigned int flags)
{
    if (linkManager->config.type == gracht_link_stream_based) {
        return socket_link_recv_stream(linkManager, messageContext, flags);
    }
    else if (linkManager->config.type == gracht_link_packet_based) {
        return socket_link_recv_packet(linkManager, messageContext, flags);
    }
    
    _set_errno(ENOTSUPP);
    return -1;
}

static int socket_link_send(struct socket_link_manager* linkManager, struct gracht_message* message,
    void* messageContext)
{
    if (linkManager->config.type == gracht_link_stream_based) {
        return socket_link_send_stream(linkManager, message);
    }
    else if (linkManager->config.type == gracht_link_packet_based) {
        return socket_link_send_packet(linkManager, message, messageContext);
    }
    
    _set_errno(ENOTSUPP);
    return -1;
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

int gracht_link_socket_client_create(struct link_operations** linkOut, 
    struct socket_client_configuration* configuration)
{
    struct socket_link_manager* linkManager;
    
    linkManager = (struct socket_link_manager*)malloc(sizeof(struct socket_link_manager));
    if (!linkManager) {
        _set_errno(ENOMEM);
        return -1;
    }
    
    memset(linkManager, 0, sizeof(struct socket_link_manager));
    memcpy(&linkManager->config, configuration, sizeof(struct socket_client_configuration));

    linkManager->ops.connect = socket_link_connect;
    linkManager->ops.send    = socket_link_send;
    linkManager->ops.recv    = socket_link_recv;
    linkManager->ops.destroy = socket_link_destroy;
    
    *linkOut = &linkManager->ops;
    return 0;
}
