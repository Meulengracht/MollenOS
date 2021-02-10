/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Key Definitions & Structures
 * - This header describes the key-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */
#define __TRACE

#include <os/keycodes.h>

extern char GetASCIIFromKeyCodeEnUs(uint8_t, uint16_t);

char
TranslateKeyCode(
        _In_  uint8_t  keyCode,
        _In_  uint16_t keyModifiers)
{
    if (keyCode != VK_INVALID) {
        return GetASCIIFromKeyCodeEnUs(keyCode, keyModifiers);
    }
    return '\0';
}
