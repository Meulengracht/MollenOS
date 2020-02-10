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
 * WM Protocol HID
 *  - HID protocol implementation used by input devices and the window manager.
 */

#ifndef __HID_PROTOCOL_H__
#define __HID_PROTOCOL_H__

#include <stdint.h>

#define PROTOCOL_HID_ID             0x01
#define PROTOCOL_HID_FUNCTION_COUNT 1

#define PROTOCOL_HID_KEY_EVENT_ID     0x0
#define PROTOCOL_HID_POINTER_EVENT_ID 0x1

#define PROTOCOL_HID_KEY_EVENT_FLAGS_LSHIFT    0x0001
#define PROTOCOL_HID_KEY_EVENT_FLAGS_RSHIFT    0x0002
#define PROTOCOL_HID_KEY_EVENT_FLAGS_LALT      0x0004
#define PROTOCOL_HID_KEY_EVENT_FLAGS_RALT      0x0008
#define PROTOCOL_HID_KEY_EVENT_FLAGS_LCTRL     0x0010
#define PROTOCOL_HID_KEY_EVENT_FLAGS_RCTRL     0x0020
#define PROTOCOL_HID_KEY_EVENT_FLAGS_SCROLLOCK 0x0040
#define PROTOCOL_HID_KEY_EVENT_FLAGS_NUMLOCK   0x0080
#define PROTOCOL_HID_KEY_EVENT_FLAGS_CAPSLOCK  0x0100
#define PROTOCOL_HID_KEY_EVENT_FLAGS_RELEASED  0x1000

struct hid_key_event_arg {
    int      source;
    uint8_t  key_ascii;
    uint8_t  key_code;
    uint16_t flags;
    uint32_t key_unicode;
};

struct hid_pointer_event_arg {
    int      source;
    uint16_t flags;
    int16_t  rel_x;
    int16_t  rel_y;
    int16_t  rel_z;
    uint32_t buttons_set;
};

#endif //!__HID_PROTOCOL_H__
