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
 * Gracht Vali Link Type Definitions & Structures
 * - This header describes the base link-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

// Link operations not supported in a packet-based environment
// - Events
// - Stream

#include <errno.h>
#include "gracht/link/vali.h"
#include <internal/_utils.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>

struct vali_link_client {
    struct gracht_server_client base;
    struct ipmsg_addr           address;
};

struct vali_link_manager {
    struct server_link_ops ops;
    int                    iod;
};

static int vali_link_send_client(struct vali_link_client* client,
    struct gracht_message* message, unsigned int flags)
{
    struct ipmsg_header ipmsg = {
            .sender  = GetNativeHandle(client->base.iod),
            .address = &client->address,
            .base    = message
    };
    return putmsg(client->base.iod, &ipmsg, 0);
}

static int vali_link_recv_client(struct gracht_server_client* client,
    struct gracht_recv_message* context, unsigned int flags)
{
    errno = (ENOTSUP);
    return -1;
}

static int vali_link_create_client(struct vali_link_manager* linkManager, struct gracht_recv_message* message,
    struct vali_link_client** clientOut)
{
    struct vali_link_client* client;
    UUId_t                   clientHandle;
    
    if (!linkManager || !message || !clientOut) {
        errno = (EINVAL);
        return -1;
    }

    clientHandle = ((struct ipmsg*)message->storage)->sender;
    client       = (struct vali_link_client*)malloc(sizeof(struct vali_link_client));
    if (!client) {
        errno = (ENOMEM);
        return -1;
    }

    memset(client, 0, sizeof(struct vali_link_client));
    client->base.header.id = message->client;
    client->base.iod = linkManager->iod;

    client->address.type = IPMSG_ADDRESS_HANDLE;
    client->address.data.handle = clientHandle;

    *clientOut = client;
    return 0;
}

static int vali_link_destroy_client(struct vali_link_client* client)
{
    int status;
    
    if (!client) {
        errno = (EINVAL);
        return -1;
    }
    
    status = close(client->base.iod);
    free(client);
    return status;
}

static int vali_link_listen(struct vali_link_manager* linkManager, int mode)
{
    if (mode == LINK_LISTEN_DGRAM) {
        return linkManager->iod;
    }
    
    errno = (ENOTSUP);
    return -1;
}

static int vali_link_accept(struct vali_link_manager* linkManager, struct gracht_server_client** clientOut)
{
    errno = (ENOTSUP);
    return -1;
}

static int vali_link_recv_packet(struct vali_link_manager* linkManager, struct gracht_recv_message* context)
{
    struct ipmsg* message = (struct ipmsg*)context->storage;
    int           status;
    
    status = getmsg(linkManager->iod, message, GRACHT_MAX_MESSAGE_SIZE, IPMSG_DONTWAIT);
    if (status) {
        return status;
    }

    context->message_id  = message->base.header.id;
    context->client      = (int)message->sender;
    context->params      = &message->base.params[0];
    
    context->param_in    = message->base.header.param_in;
    context->param_count = message->base.header.param_in + message->base.header.param_out;
    context->protocol    = message->base.header.protocol;
    context->action      = message->base.header.action;
    return 0;
}

static int vali_link_respond(struct vali_link_manager* linkManager,
    struct gracht_recv_message* messageContext, struct gracht_message* message)
{
    struct ipmsg* recvmsg = (struct ipmsg*)messageContext->storage;
    struct ipmsg_addr ipaddr = {
            .type = IPMSG_ADDRESS_HANDLE,
            .data.handle = recvmsg->sender
    };
    struct ipmsg_header ipmsg = {
            .sender  = GetNativeHandle(linkManager->iod),
            .address = &ipaddr,
            .base    = message
    };
    return resp(linkManager->iod, messageContext->storage, &ipmsg);
}

static void vali_link_destroy(struct vali_link_manager* linkManager)
{
    if (!linkManager) {
        return;
    }
    
    close(linkManager->iod);
    free(linkManager);
}

int gracht_link_vali_server_create(struct server_link_ops** linkOut, struct ipmsg_addr* address)
{
    struct vali_link_manager* linkManager;
    
    linkManager = (struct vali_link_manager*)malloc(sizeof(struct vali_link_manager));
    if (!linkManager) {
        errno = (ENOMEM);
        return -1;
    }
    
    // create an ipc context
    linkManager->iod = ipcontext(0x4000, address); /* 16kB */
    
    // initialize link operations    
    linkManager->ops.create_client  = (server_create_client_fn)vali_link_create_client;
    linkManager->ops.destroy_client = (server_destroy_client_fn)vali_link_destroy_client;

    linkManager->ops.recv_client = (server_recv_client_fn)vali_link_recv_client;
    linkManager->ops.send_client = (server_send_client_fn)vali_link_send_client;

    linkManager->ops.listen      = (server_link_listen_fn)vali_link_listen;
    linkManager->ops.accept      = (server_link_accept_fn)vali_link_accept;
    linkManager->ops.recv_packet = (server_link_recv_packet_fn)vali_link_recv_packet;
    linkManager->ops.respond     = (server_link_respond_fn)vali_link_respond;
    linkManager->ops.destroy     = (server_link_destroy_fn)vali_link_destroy;
    
    *linkOut = &linkManager->ops;
    return 0;
}
