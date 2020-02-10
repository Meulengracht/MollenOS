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
 * WM Screen Protocol
 *  - Screen compositor protocol implementation used the window manager.
 */

#ifndef __WM_SCREEN_PROTOCOL_H__
#define __WM_SCREEN_PROTOCOL_H__

#include <stdint.h>

#define PROTOCOL_WM_SCREEN_ID             0x11
#define PROTOCOL_WM_SCREEN_FUNCTION_COUNT 2

#define PROTOCOL_WM_SCREEN_GET_FORMATS_ID    0x0
#define PROTOCOL_WM_SCREEN_CREATE_SURFACE_ID 0x1

struct wm_screen_format_event {
    uint32_t serial;
    int      format;
};

#endif //!__WM_SCREEN_PROTOCOL_H__
