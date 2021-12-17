/**
 * Copyright 2021, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __VBOOT_DEF_H__
#define __VBOOT_DEF_H__

#include <stdint.h>
#include <stddef.h>

// Use this to pack structures and avoid any issues with padding
// from compilers
#if (defined (__clang__))
#define VBOOT_PACKED(name, body) struct __attribute__((packed)) name body
#elif (defined (__GNUC__))
#define VBOOT_PACKED(name, body) struct name body __attribute__((packed))
#elif (defined (__arm__))
#define VBOOT_PACKED(name, body) __packed struct name body
#elif (defined (_MSC_VER))
#define VBOOT_PACKED(name, body) __pragma(pack(push, 1)) struct name body __pragma(pack(pop))
#else
#error Please define packed struct for the used compiler
#endif

#endif //!__VBOOT_DEF_H__
