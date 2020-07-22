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
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include "gracht/link/vali.h"
#include "gracht/debug.h"
#include <io.h>
#include <os/mollenos.h>
#include <stdlib.h>

#define GRACHT_MESSAGE_THRESHOLD 128

struct vali_link_manager {
    struct client_link_ops ops;
    int                    iod;
};

static int vali_link_connect(struct vali_link_manager* linkManager)
{
    if (!linkManager) {
        errno = EINVAL;
        return -1;
    }
    return linkManager->iod;
}

static int vali_link_send_message(struct vali_link_manager* linkManager,
                                  struct gracht_message* messageBase, struct vali_link_message* messageContext)
{
    struct ipmsg_header  message;
    struct ipmsg_header* messagePointer = &message;
    OsStatus_t           status;
    int                  i;

    message.address  = &messageContext->address;
    message.base     = messageBase;
    message.sender   = GetNativeHandle(linkManager->iod);

    if (messageBase->header.length > GRACHT_MAX_MESSAGE_SIZE) {
        for (i = 0; messageBase->header.length > GRACHT_MAX_MESSAGE_SIZE && i < messageBase->header.param_in; i++) {
            if (messageBase->params[i].length > GRACHT_MESSAGE_THRESHOLD) {
                messageBase->params[i].type = GRACHT_PARAM_SHM;
                messageBase->header.length -= messageBase->params[i].length;
            }
        }
    }

    status = Syscall_IpcContextSend(&messagePointer, 1, 0);
    if (status != OsSuccess) {
        OsStatusToErrno(status);
        return GRACHT_MESSAGE_ERROR;
    }
    return GRACHT_MESSAGE_INPROGRESS;
}

static int vali_link_recv(struct vali_link_manager* linkManager, void* messageBuffer,
                          unsigned int flags, struct gracht_message** messageOut)
{
    struct ipmsg* message = (struct ipmsg*)messageBuffer;
    int           status;
    unsigned int  convertedFlags = 0;

    if (!(flags & GRACHT_WAIT_BLOCK)) {
        convertedFlags |= IPMSG_DONTWAIT;
    }

    status = getmsg(linkManager->iod, message, GRACHT_MAX_MESSAGE_SIZE, convertedFlags);
    if (status) {
        return status;
    }

    *messageOut = &message->base;
    return 0;
}

static void vali_link_destroy(struct vali_link_manager* linkManager)
{
    if (!linkManager) {
        return;
    }

    if (linkManager->iod > 0) {
        close(linkManager->iod);
    }
    free(linkManager);
}

int gracht_link_vali_client_create(struct client_link_ops** linkOut)
{
    struct vali_link_manager* linkManager;

    linkManager = (struct vali_link_manager*)malloc(sizeof(struct vali_link_manager));
    if (!linkManager) {
        ERROR("[gracht] [client-link] [vali] failed to allocate memory");
        errno = (ENOMEM);
        return -1;
    }

    // create an ipc context, 4kb should be more than enough
    linkManager->iod = ipcontext(0x1000, NULL);
    if (linkManager->iod < 0) {
        return -1;
    }

    linkManager->ops.connect     = (client_link_connect_fn)vali_link_connect;
    linkManager->ops.recv        = (client_link_recv_fn)vali_link_recv;
    linkManager->ops.send        = (client_link_send_fn)vali_link_send_message;
    linkManager->ops.destroy     = (client_link_destroy_fn)vali_link_destroy;

    *linkOut = &linkManager->ops;
    return 0;
}
