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

#include "hid_protocol.h"
#include <libwm_client.h>
#include <string.h>

int hid_key_event(wm_client_t* client, int source, uint8_t key_ascii, uint8_t key_code, uint16_t flags, uint32_t key_unicode)
{
    struct hid_key_event_arg args;
    int                      wm_status;
    
    args.source = source;
    args.key_ascii = key_ascii;
    args.key_code = key_code;
    args.flags = flags;
    args.key_unicode = key_unicode;
    
    wm_status = wm_client_invoke(client,
        PROTOCOL_HID_ID, PROTOCOL_HID_KEY_EVENT_ID,  // config
        &args, sizeof(struct hid_key_event_arg),     // args
        NULL, 0);                                    // return
    return wm_status;
}

int hid_pointer_event(wm_client_t* client, int source, uint16_t flags, int16_t rel_x, int16_t rel_y, int16_t rel_z, int16_t buttons_set)
{
    struct hid_pointer_event_arg args;
    int                          wm_status;
    
    args.source = source;
    args.flags = flags;
    args.rel_x = rel_x;
    args.rel_y = rel_y;
    args.rel_z = rel_z;
    args.buttons_set = buttons_set;
    
    wm_status = wm_client_invoke(client,
        PROTOCOL_HID_ID, PROTOCOL_HID_POINTER_EVENT_ID,  // config
        &args, sizeof(struct hid_pointer_event_arg),     // args
        NULL, 0);                                        // return
    return wm_status;
}
