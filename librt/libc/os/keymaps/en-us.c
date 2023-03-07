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
 */

#include <os/keycodes.h>

// Keymap when modifier SHIFT is present
static uint8_t g_asciiShiftKeyMap[VK_KEYCOUNT] = {
    0, ')', '!', '\"', '#', '$', '%', '^', '&', '*', '(', '*',
    '+', '-', '-', ',', '/', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 
    'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '`', '~', 0, '\t', '>', 
    '<', ':', 0, '\n', 0, 0, '_', 0, ' ', '?', '|', '@', '+',
    '{', '}', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Keymap when no modifier is present
static uint8_t g_asciiKeyMap[VK_KEYCOUNT] = {
    0, '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '*',
    '+', '-', '-', ',', '/', 'a', 'b', 'c', 'd', 'e', 'f', 'g',
    'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 
    't', 'u', 'v', 'w', 'x', 'y', 'z', '`', '`', 0, '\t', '.', 
    ',', ';', 0, '\n', 0, 0, '-', 0, ' ', '/', '\\', '\'', '=',
    '[', ']', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

char
GetASCIIFromKeyCodeEnUs(
    _In_ uint8_t  keyCode,
    _In_ uint16_t keyModifiers)
{
    int  shouldUpperCase = keyModifiers & (VK_MODIFIER_LSHIFT | VK_MODIFIER_RSHIFT);
    char character;

    if (keyModifiers & VK_MODIFIER_CAPSLOCK) {
        if (shouldUpperCase != 0) {
            shouldUpperCase = 0;
        }
        else {
            shouldUpperCase = 1;
        }
    }

    // Handle modifiers, caps lock negates shift as seen above
    if (shouldUpperCase) {
        character = (char)g_asciiShiftKeyMap[keyCode];
    }
    else {
        character = (char)g_asciiKeyMap[keyCode];
    }
    return character;
}
