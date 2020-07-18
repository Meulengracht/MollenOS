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
 * C Standard Library
 * - Keymap implementation. Supports key translation from native keycodes to
 *   the standard en-US keyboard. Supports Ascii for now
 */

#include <ctt_input_protocol.h>
#include <os/keycodes.h>

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

void
GetKeyFromSystemKeyEnUs(
    struct ctt_input_button_event* key)
{
    int shouldUpperCase = key->flags & (key_flag_lshift | key_flag_rshift);
    if (key->flags & key_flag_capslock) {
        if (shouldUpperCase != 0) {
            shouldUpperCase = 0;
        }
        else {
            shouldUpperCase = 1;
        }
    }

    // Handle modifiers, caps lock negates shift as seen above
    if (shouldUpperCase) {
        key->key_ascii = AsciiShiftKeyMap[key->key_code];
    }
    else {
        key->key_ascii = AsciiKeyMap[key->key_code];
    }

    // Copy if standard unicode, otherwise ignore for now
    if (key->key_ascii < 128) {
        key->key_unicode = key->key_ascii;
    }
}
