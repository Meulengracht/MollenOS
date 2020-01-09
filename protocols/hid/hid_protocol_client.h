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

#ifndef __HID_PROTOCOL_CLIENT_H__
#define __HID_PROTOCOL_CLIENT_H__

typedef struct wm_client wm_client_t;

int hid_key_event(wm_client_t* client, int source, uint8_t key_ascii, uint8_t key_code, uint16_t flags, uint32_t key_unicode);
int hid_pointer_event(wm_client_t* client, int source, uint16_t flags, int16_t rel_x, int16_t rel_y, int16_t rel_z, int16_t buttons_set);

#endif //!__HID_PROTOCOL_CLIENT_H__
