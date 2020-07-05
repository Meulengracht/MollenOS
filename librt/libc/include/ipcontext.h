/**
 * MollenOS
 *
 * Copyright 2020, Philip Meulengracht
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
 * Ip-Context Support Definitions & Structures
 * - This header describes the base ipc-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __IPCONTEXT_H__
#define	__IPCONTEXT_H__

#include <gracht/types.h>
#include <os/osdefs.h>
#include <threads.h>

#define IPMSG_ADDRESS_HANDLE 0
#define IPMSG_ADDRESS_PATH   1

struct ipmsg_addr {
    int type;
    union {
        UUId_t      handle;
        const char* path;
    } data;
};

#define IPMSG_ADDR_INIT_HANDLE(handle) { IPMSG_ADDRESS_HANDLE, { handle } }

struct ipmsg_header {
    UUId_t                 sender;
    struct ipmsg_addr*     address;
    struct gracht_message* base;
};

struct ipmsg {
    UUId_t                sender;
    struct gracht_message base;
};

#define IPMSG_DONTWAIT 0x1

_CODE_BEGIN
CRTDECL(int, ipcontext(unsigned int, struct ipmsg_addr*));
CRTDECL(int, putmsg(int, struct ipmsg_header*, int));
CRTDECL(int, getmsg(int, struct ipmsg*, unsigned int, int));
CRTDECL(int, resp(int, struct ipmsg*, struct ipmsg_header*));
_CODE_END

#endif //!__IPCONTEXT_H__
