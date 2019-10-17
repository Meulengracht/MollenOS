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
 * Local Address Support Definitions & Structures
 * - This header describes the base lcaddr-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */
 
#ifndef __INET_LOCAL_H__
#define __INET_LOCAL_H__

#include <inet/socket.h>

// global paths
#define LCADDR_INPUT "/lc/input"
#define LCADDR_WM    "/lc/wm"

struct sockaddr_lc {
	uint8_t     slc_len;
	sa_family_t slc_family;   // AF_LOCAL
    char        slc_addr[32]; // see defines above
};

static inline struct sockaddr_lc*
sstolc(struct sockaddr_storage *ss)
{
    return ((struct sockaddr_lc *)(ss));
}

#endif //!__INET_LOCAL_H__
