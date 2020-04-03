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
#include <gracht/link/vali.h>
#include <os/dmabuf.h>

static struct vali_link_manager {
    struct server_link_ops ops;
    int                    iod;
};

static int vali_link_listen(struct vali_link_manager* linkManager, int mode)
{
    if (mode == LINK_LISTEN_DGRAM) {
        return linkManager->iod;
    }
    
    _set_errno(ENOTSUP);
    return -1;
}

static int vali_link_accept(struct vali_link_manager* linkManager, struct struct link_ops** linkOut)
{
    _set_errno(ENOTSUP);
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
    
    context->client      = linkManager->iod;
    context->params      = &message->base.params[0];
    context->param_count = message->base.param_in;
    context->protocol    = message->base.protocol;
    context->action      = message->base.action;
    return 0;
}

static int vali_link_respond(struct vali_link_manager* linkManager,
    struct gracht_recv_message* messageContext, struct gracht_message* message)
{
    return resp(linkManager->iod, messageContext->storage, (struct ipmsg_base*)message);
}

static void vali_link_destroy(struct vali_link_manager* linkManager)
{
    if (!linkManager) {
        return;
    }
    
    close(linkManager->iod);
    free(linkManager);
}

int gracht_link_vali_create(struct link_operations** linkOut, struct ipmsg_addr* address)
{
    struct vali_link_manager* linkManager;
    
    linkManager = (struct vali_link_manager*)malloc(sizeof(struct vali_link_manager));
    if (!linkManager) {
        _set_errno(ENOMEM);
        return -1;
    }
    
    // create an ipc context
    linkManager->iod = ipcontext(0x4000, address); /* 16kB */
    
    // initialize link operations
    linkManager->ops.listen      = vali_link_listen;
    linkManager->ops.accept      = vali_link_accept;
    linkManager->ops.recv_packet = vali_link_recv_packet;
    linkManager->ops.respond     = vali_link_respond;
    linkManager->ops.destroy     = vali_link_destroy;
    
    *linkOut = &linkManager->ops;
    return 0;
}
