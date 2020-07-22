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

#include <errno.h>
#include <ddk/bytepool.h>
#include "gracht/link/vali.h"
#include <stdlib.h>
#include <string.h>

void gracht_vali_message_defer_response(struct vali_link_deferred_response* deferredResponse,
    struct gracht_recv_message* message)
{
    if (!deferredResponse || !message) {
        return;
    }
    
    memcpy(&deferredResponse->recv_message, message, sizeof(struct gracht_recv_message));
    memcpy(&deferredResponse->recv_storage, message->storage, sizeof(struct ipmsg) + VALI_MSG_DEFER_SIZE(message));
    
    // Fixup the pointers
    deferredResponse->recv_message.storage = &deferredResponse->recv_storage;
    deferredResponse->recv_message.params = NULL;
}

// message size is defined by protocol generator
int gracht_vali_message_create(gracht_client_t* client, int message_size, struct vali_link_message** messageOut)
{
    struct vali_link_message* message = malloc(sizeof(struct vali_link_message) + message_size);
    if (!message) {
        errno = (ENOMEM);
        return -1;
    }
    
    memset(message, 0, sizeof(struct vali_link_message) + message_size);
    *messageOut = message;
    return 0;
}
