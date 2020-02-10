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
 * WM Core Protocol
 *  - Core compositor protocol implementation used the window manager.
 */

#ifndef __WM_CORE_PROTOCOL_H__
#define __WM_CORE_PROTOCOL_H__

#include <stdint.h>

#define PROTOCOL_WM_CORE_ID             0x10
#define PROTOCOL_WM_CORE_FUNCTION_COUNT 4

#define PROTOCOL_WM_CORE_SYNC_ID           0x0
#define PROTOCOL_WM_CORE_GET_STATE_ID      0x1

#define PROTOCOL_WM_CORE_EVENT_SYNC_ID           0x3
#define PROTOCOL_WM_CORE_EVENT_ERROR_ID          0x4
#define PROTOCOL_WM_CORE_EVENT_OBJECT_DELETED_ID 0x5

struct wm_core_sync_event {
    uint32_t serial;
};

struct wm_core_state_element_event {
    uint32_t serial;
    int      type;
};

#endif //!__WM_CORE_PROTOCOL_H__
