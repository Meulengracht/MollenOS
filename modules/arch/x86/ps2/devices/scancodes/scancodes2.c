/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 *
 * MollenOS X86 PS2 Controller (Keyboard) - ScancodeSet 2 Driver
 * http://wiki.osdev.org/PS2
 */

#include "../keyboard.h"
#include <os/keycodes.h>

static uint8_t g_scancodeSet2Table[132] = {
    VK_INVALID, VK_F9, VK_INVALID, VK_F5, VK_F3, VK_F1, VK_F2, VK_F12, VK_INVALID, VK_F10,
    VK_F8, VK_F6, VK_F4, VK_TAB, VK_BACKTICK, VK_INVALID, VK_INVALID, VK_LALT,
    VK_LSHIFT, VK_INVALID, VK_LCONTROL, VK_Q, VK_1, VK_INVALID, VK_INVALID,
    VK_INVALID, VK_Z, VK_S, VK_A, VK_W, VK_2, VK_INVALID, VK_INVALID, VK_C, VK_X,
    VK_D, VK_E, VK_4, VK_3, VK_INVALID, VK_INVALID, VK_SPACE, VK_V, VK_F, VK_T, VK_R, VK_5,
    VK_INVALID, VK_INVALID, VK_N, VK_B, VK_H, VK_G, VK_Y, VK_6, VK_INVALID, VK_INVALID,
    VK_INVALID, VK_M, VK_J, VK_U, VK_7, VK_8, VK_INVALID, VK_INVALID, VK_COMMA, VK_K, VK_I,
    VK_O, VK_0, VK_9, VK_INVALID, VK_INVALID, VK_DOT, VK_SLASH, VK_L, VK_SEMICOLON, VK_P, VK_HYPHEN, VK_INVALID,
    VK_INVALID, VK_INVALID, VK_APOSTROPHE, VK_INVALID, VK_LBRACKET, VK_EQUAL,  VK_INVALID, VK_INVALID,
    VK_CAPSLOCK, VK_RSHIFT, VK_ENTER, VK_RBRACKET, VK_INVALID, VK_BACKSLASH, VK_INVALID, VK_INVALID, VK_INVALID,
    VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_BACK, VK_INVALID,
    VK_INVALID, VK_1, VK_INVALID, VK_4, VK_7, VK_INVALID, VK_INVALID,
    VK_INVALID, VK_0, VK_DECIMAL, VK_2, VK_5, VK_6, VK_8,
    VK_ESCAPE, VK_NUMLOCK, VK_F11, VK_ADD, VK_3, VK_SUBTRACT, VK_MULTIPLY, VK_9,
    VK_SCROLL, VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_F7
};

static uint8_t g_scancodeSet2ExtendedTable[126] = {
    VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID,
    VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID,
    VK_INVALID, VK_INVALID, VK_BROWSER_SEARCH, VK_RALT, VK_INVALID, VK_INVALID, VK_RCONTROL,
    VK_MEDIA_PREV_TRACK, VK_INVALID, VK_INVALID, VK_BROWSER_FAVORITES, VK_INVALID, VK_INVALID,
    VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_LWIN, VK_BROWSER_REFRESH, VK_VOLUME_DOWN,
    VK_INVALID, VK_VOLUME_MUTE, VK_INVALID, VK_INVALID, VK_INVALID, VK_RWIN, VK_BROWSER_STOP, 
    VK_INVALID, VK_INVALID, VK_LAUNCH_APP2, VK_INVALID, VK_INVALID, VK_INVALID, VK_LAUNCH_APP1,
    VK_BROWSER_FORWARD, VK_INVALID, VK_VOLUME_UP, VK_INVALID, VK_MEDIA_PLAY_PAUSE, VK_INVALID,
    VK_INVALID, VK_POWER, VK_BROWSER_BACK, VK_INVALID, VK_BROWSER_HOME, VK_MEDIA_STOP, VK_INVALID,
    VK_INVALID, VK_INVALID, VK_SLEEP, VK_APPS, VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID,
    VK_INVALID, VK_INVALID, VK_INVALID, VK_LAUNCH_MAIL, VK_INVALID, VK_DIVIDE, VK_INVALID, VK_INVALID,
    VK_MEDIA_NEXT_TRACK, VK_INVALID, VK_INVALID, VK_LAUNCH_MEDIA_SELECT, VK_INVALID, VK_INVALID,
    VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_ENTER,
    VK_INVALID, VK_INVALID, VK_INVALID, VK_WAKE, VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID,
    VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_END, VK_INVALID, VK_LEFT, VK_HOME,
    VK_INVALID, VK_INVALID, VK_INVALID, VK_INSERT, VK_DELETE, VK_DOWN, VK_INVALID, VK_RIGHT, VK_UP,
    VK_INVALID, VK_INVALID, VK_INVALID, VK_INVALID, VK_PAGEDOWN, VK_INVALID, VK_INVALID, VK_PAGEUP
};

oserr_t
ScancodeSet2ToVKey(
    _In_ struct key_state* keyState,
    _In_ uint8_t           scancode)
{
    // Handle special cases
    if (scancode == PS2_CODE_EXTENDED) {
        keyState->modifiers |= KEY_MODIFIER_EXTENDED;
        return OsError;
    }
    else if (scancode == PS2_CODE_RELEASED) {
        keyState->modifiers |= VK_MODIFIER_RELEASED;
        return OsError;
    }

    // Get appropriate scancode
    if (keyState->modifiers & KEY_MODIFIER_EXTENDED) {
        keyState->keycode = g_scancodeSet2ExtendedTable[scancode];
    }
    else {
        keyState->keycode = g_scancodeSet2Table[scancode];
    }
    return OsOK;
}
