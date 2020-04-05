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

#include <os/osdefs.h>

#define IPMSG_ADDRESS_HANDLE 0
#define IPMSG_ADDRESS_PATH   1

struct ipmsg_addr {
    int type;
    union {
        UUId_t      handle;
        const char* path;
    } data;
};

#define IPMSG_NOTIFY_NONE       0
#define IPMSG_NOTIFY_HANDLE_SET 1 // Completion handle
#define IPMSG_NOTIFY_SIGNAL     2 // SIGIPC
#define IPMSG_NOTIFY_THREAD     3 // Thread callback

struct ipmsg_resp {
    UUId_t   dma_handle;
    uint16_t dma_offset;
    int      notify_method;
    void*    notify_context;
    union {
        UUId_t    handle;
        uintptr_t callback;
        uintptr_t syncobject;
    } notify_data;
};

#define IPMSG_PARAM_VALUE  0
#define IPMSG_PARAM_BUFFER 1
#define IPMSG_PARAM_SHM    2

struct ipmsg_param {
    int type;
    union {
        size_t value;
        void*  buffer;
    } data;
    size_t length;
};

struct ipmsg_base {
    uint32_t length    : 16;
    uint32_t param_in  : 4;
    uint32_t param_out : 4;
    uint32_t flags     : 8;
    uint32_t protocol  : 8;
    uint32_t action    : 8;
    uint32_t reserved  : 16;
    
    struct ipmsg_param params[];
};

struct ipmsg_desc {
    struct ipmsg_addr* address;
    struct ipmsg_base* base;
    struct ipmsg_resp* response;
};

struct ipmsg {
    struct ipmsg_resp response;
    struct ipmsg_base base;
};

#define IPMSG_DONTWAIT 0x1

_CODE_BEGIN
CRTDECL(int, ipcontext(unsigned int, struct ipmsg_addr*));
CRTDECL(int, putmsg(int, struct ipmsg_desc*, int));
CRTDECL(int, getmsg(int, struct ipmsg*, unsigned int, int));
CRTDECL(int, resp(int, struct ipmsg*, struct ipmsg_base*));
_CODE_END

#endif //!__IPCONTEXT_H__
