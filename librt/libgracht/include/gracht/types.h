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

#if (defined (__clang__))
#define GRACHT_STRUCT(name, body) struct __attribute__((packed)) name body 
#elif (defined (__GNUC__))
#define GRACHT_STRUCT(name, body) struct name body __attribute__((packed))
#elif (defined (__arm__))
#define GRACHT_STRUCT(name, body) __packed struct name body
#elif (defined (_MSC_VER))
#define GRACHT_STRUCT(name, body) __pragma(pack(push, 1)) struct name body __pragma(pack(pop))
#else
#error "Please define packed struct for the used compiler"
#endif

#define MESSAGE_FLAG_TYPE(flags) (flags & 0x3)
#define MESSAGE_FLAG_SYNC     0x00000000
#define MESSAGE_FLAG_ASYNC    0x00000001
#define MESSAGE_FLAG_EVENT    0x00000002
#define MESSAGE_FLAG_RESPONSE 0x00000003

#define GRACHT_MAX_MESSAGE_SIZE 512

#define GRACHT_PARAM_VALUE  0
#define GRACHT_PARAM_BUFFER 1
#define GRACHT_PARAM_SHM    2

#define GRACHT_AWAIT_ANY 0
#define GRACHT_AWAIT_ALL 1

#define GRACHT_MESSAGE_ERROR      -1
#define GRACHT_MESSAGE_CREATED    0
#define GRACHT_MESSAGE_INPROGRESS 1
#define GRACHT_MESSAGE_COMPLETED  2

#define GRACHT_WAIT_BLOCK 0x1

typedef struct gracht_object_header {
    int                          id;
    struct gracht_object_header* link;
} gracht_object_header_t;

// Pack structures transmitted to make debugging wire format easier
GRACHT_STRUCT(gracht_param, {
    uint32_t length : 30;
    uint32_t type   : 2;
    union {
        size_t value;
        void*  buffer;
    } data;
});

// Pack structures transmitted to make debugging wire format easier
GRACHT_STRUCT(gracht_message_header, {
    uint32_t id;
    uint32_t length;
    uint32_t param_in  : 4;
    uint32_t param_out : 4;
    uint32_t flags     : 8;
    uint32_t protocol  : 8;
    uint32_t action    : 8;
});

struct gracht_message {
    struct gracht_message_header header;
    struct gracht_param          params[];
};

struct gracht_recv_message {
    void* storage;
    void* params;
    
    int      client;
    uint32_t message_id;
    uint8_t  param_in;
    uint8_t  param_count;
    uint8_t  protocol;
    uint8_t  action;
};

struct gracht_message_context {
    uint32_t message_id;
    void*    descriptor;
};

typedef struct gracht_protocol_function {
    uint8_t id;
    void*   address;
} gracht_protocol_function_t;

typedef struct gracht_protocol {
    gracht_object_header_t      header;
    uint8_t                     id;
    char*                       name;
    uint8_t                     num_functions;
    gracht_protocol_function_t* functions;
} gracht_protocol_t;

#define GRACHT_PROTOCOL_INIT(id, name, num_functions, functions) { { id, NULL }, id, name, num_functions, functions }

#endif // !__GRACHT_TYPES_H__
