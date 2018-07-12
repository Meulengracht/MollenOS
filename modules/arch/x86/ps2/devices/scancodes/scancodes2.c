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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS X86 PS2 Controller (Keyboard) - ScancodeSet 2 Driver
 * http://wiki.osdev.org/PS2
 */

#include "../keyboard.h"
#include "scancodes2.h"

/* ScancodeSet2ToVKey
 * Converts a scancode 2 key to the standard-defined
 * virtual key-layout */
VKey
ScancodeSet2ToVKey(
    _In_  uint8_t     Scancode,
    _Out_ unsigned* Buffer,
    _Out_ unsigned* Flags)
{
    ScancodeSet2 Ss2Key = 0;
    VKey ConvKey         = VK_INVALID;

    // Handle special cases
    if (Scancode == EXTENDED) {
        *Buffer = 0xE000;
        *Flags |= PS2_KEY_EXTENDED;
        return ConvKey;
    }
    else if (Scancode == RELEASED) {
        *Flags |= PS2_KEY_RELEASED;
        return ConvKey;
    }
    else {
        *Buffer |= Scancode;
        Ss2Key = (ScancodeSet2)*Buffer;
    }

    switch (Ss2Key)
    {
        // Digits
        case D0:
            ConvKey = 0x30;
            break;
        case D1:
            ConvKey = 0x31;
            break;
        case D2:
            ConvKey = 0x32;
            break;
        case D3:
            ConvKey = 0x33;
            break;
        case D4:
            ConvKey = 0x34;
            break;
        case D5:
            ConvKey = 0x35;
            break;
        case D6:
            ConvKey = 0x36;
            break;
        case D7:
            ConvKey = 0x37;
            break;
        case D8:
            ConvKey = 0x38;
            break;
        case D9:
            ConvKey = 0x39;
            break;

        // A-Z
        case A:
            ConvKey = 0x41;
            break;
        case B:
            ConvKey = 0x42;
            break;
        case C:
            ConvKey = 0x43;
            break;
        case D:
            ConvKey = 0x44;
            break;
        case E:
            ConvKey = 0x45;
            break;
        case F:
            ConvKey = 0x46;
            break;
        case G:
            ConvKey = 0x47;
            break;
        case H:
            ConvKey = 0x48;
            break;
        case I:
            ConvKey = 0x49;
            break;
        case J:
            ConvKey = 0x4A;
            break;
        case K:
            ConvKey = 0x4B;
            break;
        case L:
            ConvKey = 0x4C;
            break;
        case M:
            ConvKey = 0x4D;
            break;
        case N:
            ConvKey = 0x4E;
            break;
        case O:
            ConvKey = 0x4F;
            break;
        case P:
            ConvKey = 0x50;
            break;
        case Q:
            ConvKey = 0x51;
            break;
        case R:
            ConvKey = 0x52;
            break;
        case S:
            ConvKey = 0x53;
            break;
        case T:
            ConvKey = 0x54;
            break;
        case U:
            ConvKey = 0x55;
            break;
        case V:
            ConvKey = 0x56;
            break;
        case W:
            ConvKey = 0x57;
            break;
        case X:
            ConvKey = 0x58;
            break;
        case Y:
            ConvKey = 0x59;
            break;
        case Z:
            ConvKey = 0x5A;
            break;

        // Function Keys
        case F1:
            ConvKey = VK_F1;
            break;
        case F2:
            ConvKey = VK_F2;
            break;
        case F3:
            ConvKey = VK_F3;
            break;
        case F4:
            ConvKey = VK_F4;
            break;
        case F5:
            ConvKey = VK_F5;
            break;
        case F6:
            ConvKey = VK_F6;
            break;
        case F7:
            ConvKey = VK_F7;
            break;
        case F8:
            ConvKey = VK_F8;
            break;
        case F9:
            ConvKey = VK_F9;
            break;
        case F10:
            ConvKey = VK_F10;
            break;
        case F11:
            ConvKey = VK_F11;
            break;
        case F12:
            ConvKey = VK_F12;
            break;

        // KeyPad
        case KEYPAD0:
            ConvKey = VK_NUMPAD0;
            break;
        case KEYPAD1:
            ConvKey = VK_NUMPAD1;
            break;
        case KEYPAD2:
            ConvKey = VK_NUMPAD2;
            break;
        case KEYPAD3:
            ConvKey = VK_NUMPAD3;
            break;
        case KEYPAD4:
            ConvKey = VK_NUMPAD4;
            break;
        case KEYPAD5:
            ConvKey = VK_NUMPAD5;
            break;
        case KEYPAD6:
            ConvKey = VK_NUMPAD6;
            break;
        case KEYPAD7:
            ConvKey = VK_NUMPAD7;
            break;
        case KEYPAD8:
            ConvKey = VK_NUMPAD8;
            break;
        case KEYPAD9:
            ConvKey = VK_NUMPAD9;
            break;
        case KEYPADMINUS:
            ConvKey = VK_SUBTRACT;
            break;
        case KEYPADPLUS:
            ConvKey = VK_ADD;
            break;
        case KEYPADMUL:
            ConvKey = VK_MULTIPLY;
            break;
        case KEYPADBACKSLASH:
            ConvKey = VK_DIVIDE;
            break;
        case KEYPADDOT:
            ConvKey = VK_DECIMAL;
            break;

        // Control Keys
        case LCONTROL:
            ConvKey = VK_LCONTROL;
            break;
        case RCONTROL:
            ConvKey = VK_RCONTROL;
            break;
        case LALT:
            ConvKey = VK_LALT;
            break;
        case RALT:
            ConvKey = VK_RALT;
            break;
        case LSHIFT:
            ConvKey = VK_LSHIFT;
            break;
        case RSHIFT:
            ConvKey = VK_RSHIFT;
            break;

        case BTICK:
            ConvKey = '`';
            break;
        case TAB:
            ConvKey = VK_TAB;
            break;
        case SPACE:
            ConvKey = VK_SPACE;
            break;
        case COMMA:
            ConvKey = VK_OEM_COMMA;
            break;
        case DOT:
            ConvKey = '.';
            break;
        case SLASH:
            ConvKey = '/';
            break;
        case SEMICOL:
            ConvKey = ';';
            break;
        case HYPHEN:
            ConvKey = '-';
            break;
        case TICK:
            ConvKey = '\'';
            break;
        case LBRACKET:
            ConvKey = '[';
            break;
        case RBRACKET:
            ConvKey = ']';
            break;
        case EQUAL:
            ConvKey = '=';
            break;
        case CAPSLOCK:
            ConvKey = VK_CAPSLOCK;
            break;
        case ENTER:
            ConvKey = VK_ENTER;
            break;
        case BACKSLASH:
            ConvKey = '\\';
            break;
        case BACKSPACE:
            ConvKey = VK_BACK;
            break;
        case ESCAPE:
            ConvKey = VK_ESCAPE;
            break;
        case NUMLOCK:
            ConvKey = VK_NUMLOCK;
            break;
        case SCROLLLOCK:
            ConvKey = VK_SCROLL;
            break;
        case INSERT:
            ConvKey = VK_INSERT;
            break;
        case END:
            ConvKey = VK_END;
            break;
        case PAGEDOWN:
            ConvKey = VK_PAGEDOWN;
            break;
        case PAGEUP:
            ConvKey = VK_PAGEUP;
            break;
        case DELETE:
            ConvKey = VK_DELETE;
            break;
        case HOME:
            ConvKey = VK_HOME;
            break;

        // Extended Codes
        case WwwSearch:
            ConvKey = VK_BROWSER_SEARCH;
            break;
        case WwwBack:
            ConvKey = VK_BROWSER_BACK;
            break;
        case WwwFav:
            ConvKey = VK_BROWSER_FAVORITES;
            break;
        case WwwForward:
            ConvKey = VK_BROWSER_FORWARD;
            break;
        case WwwHome:
            ConvKey = VK_BROWSER_HOME;
            break;
        case WwwRefresh:
            ConvKey = VK_BROWSER_REFRESH;
            break;
        case WwwStop:
            ConvKey = VK_BROWSER_STOP;
            break;

        case PrevTrack:
            ConvKey = VK_MEDIA_PREV_TRACK;
            break;
        case NextTrack:
            ConvKey = VK_MEDIA_NEXT_TRACK;
            break;
        case Stop:
            ConvKey = VK_MEDIA_STOP;
            break;
        case PlayPause:
            ConvKey = VK_MEDIA_PLAY_PAUSE;
            break;

        case VolUp:
            ConvKey = VK_VOLUME_UP;
            break;
        case VolDown:
            ConvKey = VK_VOLUME_DOWN;
            break;
        case Mute:
            ConvKey = VK_VOLUME_MUTE;
            break;
        
        case Email:
            ConvKey = VK_LAUNCH_MAIL;
            break;
        case MediaSelect:
            ConvKey = VK_LAUNCH_MEDIA_SELECT;
            break;
        case Apps:
            ConvKey = VK_LAUNCH_APP1;
            break;
        case Calculator:
            ConvKey = VK_LAUNCH_APP2;
            break;
        case MyPc:
            ConvKey = VK_APPS;
            break;

        // Arrow Keys
        case LEFTARROW:
            ConvKey = VK_LEFT;
            break;
        case RIGHTARROW:
            ConvKey = VK_RIGHT;
            break;
        case UPARROW:
            ConvKey = VK_UP;
            break;
        case DOWNARROW:
            ConvKey = VK_DOWN;
            break;

        case LGUI:
            ConvKey = VK_LWIN;
            break;
        case RGUI:
            ConvKey = VK_RWIN;
            break;

        default:
            break;
    }
    return ConvKey;
}