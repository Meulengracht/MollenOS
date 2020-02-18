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

#define GRACHT_MAX_MESSAGE_SIZE 255

typedef void* gracht_handle_t;

typedef struct gracht_message {
    uint32_t length     : 8;  // length is this message including arguments
    uint32_t ret_length : 8;  // length of reply object
    uint32_t crc        : 16; // crc of argument data
    uint32_t protocol   : 8;
    uint32_t action     : 8;
    uint32_t padding    : 16;
} gracht_message_t;

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
