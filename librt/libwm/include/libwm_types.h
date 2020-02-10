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
 * Wm Type Definitions & Structures
 * - This header describes the base wm-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __LIBWM_TYPES_H__
#define __LIBWM_TYPES_H__

#include <stdint.h>
#include <stddef.h>

#define WM_MAX_MESSAGE_SIZE 255

typedef void* wm_handle_t;

typedef struct wm_message {
    uint32_t serial_no;
    uint32_t length     : 8;  // length is this message including arguments
    uint32_t ret_length : 8;  // length of reply object
    uint32_t crc        : 16; // crc of argument data
    uint8_t  protocol;
    uint8_t  action;
    uint16_t padding;
} wm_message_t;

typedef struct wm_object_header {
    int                      id;
    struct wm_object_header* link;
} wm_object_header_t;

typedef struct wm_protocol_function {
    uint8_t id;
    void*   address;
} wm_protocol_function_t;

typedef struct wm_protocol {
    wm_object_header_t      header;
    uint8_t                 id;
    uint8_t                 num_functions;
    wm_protocol_function_t* functions;
} wm_protocol_t;

#define WM_PROTOCOL_INIT(id, num_functions, functions) { { id, NULL }, id, num_functions, functions }

#endif // !__LIBWM_TYPES_H__
