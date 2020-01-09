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

#define WM_HEADER_MAGIC              0xDEAE5A9A
#define WM_MAX_PROTOCOLS             8
#define WM_MAX_ARGUMENTS             5
#define WM_MAX_MESSAGE_SIZE          4096
#define WM_MESSAGE_GET_LENGTH(Field) ((size_t)Field + 1)

typedef void* wm_handle_t;

typedef struct wm_message {
    unsigned int magic;
    uint32_t     length     : 12; // length is this + 1, up to max 4095
    uint32_t     ret_length : 12; // length of reply object, N + 1
    uint32_t     has_arg    : 1;  // is there any arguments?
    uint32_t     has_ret    : 1;  // is there any return?
    uint32_t     unused     : 6;  // reserved
    uint16_t     crc;             // crc of following data
    uint8_t      protocol;
    uint8_t      action;
} wm_message_t;

typedef struct wm_protocol_function {
    uint8_t id;
    void*   address;
} wm_protocol_function_t;

typedef struct wm_protocol {
    uint8_t                 id;
    uint8_t                 num_functions;
    wm_protocol_function_t* functions;
} wm_protocol_t;

#endif // !__LIBWM_TYPES_H__
