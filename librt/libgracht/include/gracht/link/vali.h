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

#ifndef __GRACHT_LINK_VALI_H__
#define __GRACHT_LINK_VALI_H__

#include "link.h"
#include "../client.h"
#include <ipcontext.h>
#include <os/osdefs.h>

struct sockaddr_storage;

struct vali_link_message {
    struct ipmsg_addr address;
    struct ipmsg_resp response;
    void*             response_pool;
    void*             response_buffer;
};

struct vali_link_deferred_response {
    struct gracht_recv_message recv_message;
    struct ipmsg               recv_storage;
};

#define VALI_MSG_INIT_HANDLE(handle) { { IPMSG_ADDRESS_HANDLE, { handle } }, { 0 }, NULL }

#ifdef __cplusplus
extern "C" {
#endif

int gracht_os_get_server_client_address(struct sockaddr_storage*, int*);
int gracht_os_get_server_packet_address(struct sockaddr_storage*, int*);
int gracht_os_thread_set_name(const char*);

// Link API
void gracht_vali_message_defer_response(struct vali_link_deferred_response*, struct gracht_recv_message*);
int  gracht_vali_message_create(gracht_client_t*, int message_size, struct vali_link_message**);
void gracht_vali_message_finish(struct vali_link_message*);

int  gracht_link_vali_server_create(struct server_link_ops**, struct ipmsg_addr*);
int  gracht_link_vali_client_create(struct client_link_ops**);

#ifdef __cplusplus
}
#endif
#endif // !__GRACHT_LINK_VALI_H__
