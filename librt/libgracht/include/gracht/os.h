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
 * Gracht OS Type Definitions & Structures
 * - This header describes the base os-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_OS_H__
#define __GRACHT_OS_H__

#include "types.h"

// Prototypes
struct sockaddr_storage;

// OS API
// Generally os-specific utility functions that are needed during execution
// of the libwm operations
int gracht_os_get_server_client_address(struct sockaddr_storage*, int*);
int gracht_os_get_server_packet_address(struct sockaddr_storage*, int*);
int gracht_os_thread_set_name(const char*);

#endif // !__GRACHT_OS_H__
