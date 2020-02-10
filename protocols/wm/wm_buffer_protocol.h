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
 * WM Buffer Protocol
 *  - Buffer memory protocol implementation used the window manager.
 */

#ifndef __WM_BUFFER_PROTOCOL_H__
#define __WM_BUFFER_PROTOCOL_H__

#include <stdint.h>

#define PROTOCOL_WM_BUFFER_ID             0x14
#define PROTOCOL_WM_BUFFER_FUNCTION_COUNT 2

#define PROTOCOL_WM_BUFFER_DESTROY_ID 0x0

#define PROTOCOL_WM_BUFFER_RELEASE_ID 0x1

#endif //!__WM_BUFFER_PROTOCOL_H__
