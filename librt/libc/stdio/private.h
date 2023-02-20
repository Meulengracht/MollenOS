/**
 * Copyright 2023, Philip Meulengracht
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

#ifndef __STDIO_PRIVATE_H__
#define __STDIO_PRIVATE_H__

#define MEMORYSTREAM_SIGNATURE 0x80000001

/**
 * @brief Convert from file mode ASCII string to O_* flags.
 * @param mode An ASCII string containing valid file mode string
 * @param flagsOut O_* style flags
 * @return -1 if the string contained illegal characters, otherwise 0.
 */
extern int __fmode_to_flags(const char* mode, int* flagsOut);

#endif //!__STDIO_PRIVATE_H__
