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

#define IPMSG_NOTIFY_NONE       0
#define IPMSG_NOTIFY_HANDLE_SET 1 // Completion handle
#define IPMSG_NOTIFY_SIGNAL     2 // SIGIPC
#define IPMSG_NOTIFY_THREAD     3 // Thread callback

// Pack structures transmitted to make debugging wire format easier
GRACHT_STRUCT(ipmsg_resp, {
    UUId_t   dma_handle; // ::handle
    uint32_t dma_offset; // remove
    int      notify_method; // remove?
    void*    notify_context; // remove?
    union {
        UUId_t    handle;
        uintptr_t callback;
    } notify_data; // remove?
});

#define IPMSG_RESP_INIT_DEFAULT { UUID_INVALID, 0, IPMSG_NOTIFY_NONE, NULL, { thrd_current() } }

struct ipmsg_desc {
    struct ipmsg_addr*     address;
    struct gracht_message* base;
    struct ipmsg_resp*     response;
};

struct ipmsg {
    struct ipmsg_resp     response;
    struct gracht_message base;
};

#define IPMSG_DONTWAIT 0x1

_CODE_BEGIN
CRTDECL(int, ipcontext(unsigned int, struct ipmsg_addr*));
CRTDECL(int, putmsg(int, struct ipmsg_desc*, int));
CRTDECL(int, getmsg(int, struct ipmsg*, unsigned int, int));
CRTDECL(int, resp(int, struct ipmsg*, struct gracht_message*));
_CODE_END

#endif //!__IPCONTEXT_H__
