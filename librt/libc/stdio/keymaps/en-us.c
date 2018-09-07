/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * MollenOS - C Standard Library
 * - Keymap implementation. Supports key translation from native keycodes to
 *   the standard en-US keyboard. Supports Ascii for now
 */

#include <os/input.h>

// Keymap when modifier SHIFT is present
static uint8_t AsciiShiftKeyMap[VK_KEYCOUNT] = {
    0, ')', '!', '\"', '#', '$', '%', '^', '&', '*', '(', '*',
    '+', '-', '-', ',', '/', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 
    'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '`', '~', 0, '\t', '>', 
    '<', ':', 0, '\n', 0, 0, 0, 0, 0, '_', 0, ' ', '?', '|', 
    '@', '+', '{', '}', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0
};

// Keymap when no modifier is present
static uint8_t AsciiKeyMap[VK_KEYCOUNT] = {
    0, '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '*',
    '+', '-', '-', ',', '/', 'a', 'b', 'c', 'd', 'e', 'f', 'g',
    'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 
    't', 'u', 'v', 'w', 'x', 'y', 'z', '`', '`', 0, '\t', '.', 
    ',', ';', 0, '\n', 0, 0, 0, 0, 0, '-', 0, ' ', '/', '\\', 
    '\'', '=', '[', ']', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0
};

/* GetKeyFromSystemKeyEnUs
 * Converts system key to both a unicode and ascii character. */
OsStatus_t
GetKeyFromSystemKeyEnUs(
    _In_ SystemKey_t* Key)
{
    int ShouldUpperCase = Key->Flags & (KEY_MODIFIER_LSHIFT | KEY_MODIFIER_RSHIFT);
    if (Key->Flags & KEY_MODIFIER_CAPSLOCK) {
        if (ShouldUpperCase != 0) {
            ShouldUpperCase = 0;
        }
        else {
            ShouldUpperCase = 1;
        }
    }

    // Handle modifiers, caps lock negates shift as seen above
    if (ShouldUpperCase) {
        Key->KeyAscii = AsciiShiftKeyMap[Key->KeyCode];
    }
    else {
        Key->KeyAscii = AsciiKeyMap[Key->KeyCode];
    }

    // Copy if standard unicode, otherwise ignore for now
    if (Key->KeyAscii < 128) {
        Key->KeyUnicode = Key->KeyAscii;
    }
    return OsSuccess;
}
