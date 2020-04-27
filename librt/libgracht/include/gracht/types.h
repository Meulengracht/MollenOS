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
 * Gracht Type Definitions & Structures
 * - This header describes the base wm-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_TYPES_H__
#define __GRACHT_TYPES_H__

#include <stdint.h>
#include <stddef.h>

typedef void* gracht_handle_t;

#define MESSAGE_FLAG_ASYNC    0x00000001

#define GRACHT_MAX_MESSAGE_SIZE 512

#define GRACHT_PARAM_VALUE  0
#define GRACHT_PARAM_BUFFER 1
#define GRACHT_PARAM_SHM    2

struct gracht_param {
    int type;
    union {
        size_t value;
        void*  buffer;
    } data;
    size_t length;
};

struct gracht_message_header {
    uint32_t length    : 16;
    uint32_t param_in  : 4;
    uint32_t param_out : 4;
    uint32_t flags     : 8;
    uint32_t protocol  : 8;
    uint32_t action    : 8;
    uint32_t reserved  : 16;
};

struct gracht_message {
    struct gracht_message_header header;
    struct gracht_param          params[];
};

struct gracht_recv_message {
    void*   storage;
    int     client;
    void*   params;
    int     param_count;
    uint8_t protocol;
    uint8_t action;
};

typedef struct gracht_object_header {
    int                          id;
    struct gracht_object_header* link;
} gracht_object_header_t;

typedef struct gracht_protocol_function {
    uint8_t id;
    void*   address;
} gracht_protocol_function_t;

typedef struct gracht_protocol {
    gracht_object_header_t      header;
    uint8_t                     id;
    uint8_t                     num_functions;
    gracht_protocol_function_t* functions;
} gracht_protocol_t;

#define GRACHT_PROTOCOL_INIT(id, num_functions, functions) { { id, NULL }, id, num_functions, functions }

#endif // !__GRACHT_TYPES_H__
