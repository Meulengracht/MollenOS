/**
 * Copyright 2022, Philip Meulengracht
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
 * Platform specific bits
 * - Contains macros and includes for specific platforms
 */

#ifndef __PLATFORM_H__
#define __PLATFORM_H__

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

#ifdef _MSC_VER
#include <dirent_win32.h>
#define PACKED_TYPESTRUCT(name, body) __pragma(pack(push, 1)) typedef struct _##name body name##_t __pragma(pack(pop))
#else
#include <dirent.h>
#define PACKED_TYPESTRUCT(name, body) typedef struct __attribute__((packed)) _##name body name##_t
#endif
#include <sys/stat.h>

#endif //!__PLATFORM_H__
