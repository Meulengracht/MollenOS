/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Virtual KeyCodes Support Definitions & Structures
 * - This header describes the base virtual keycodes-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _KEYCODES_INTERFACE_H_
#define _KEYCODES_INTERFACE_H_

typedef enum _KeyCode {
    VK_INVALID              = 0x00,

    // 0-9
    VK_0                    = 0x01,
    VK_1                    = 0x02,
    VK_2                    = 0x03,
    VK_3                    = 0x04,
    VK_4                    = 0x05,
    VK_5                    = 0x06,
    VK_6                    = 0x07,
    VK_7                    = 0x08,
    VK_8                    = 0x09,
    VK_9                    = 0x0A,
    VK_MULTIPLY             = 0x0B,
    VK_ADD                  = 0x0C,
    VK_SEPARATOR            = 0x0D,
    VK_SUBTRACT             = 0x0E,
    VK_DECIMAL              = 0x0F,
    VK_DIVIDE               = 0x10,

    // A-Z
    VK_A                    = 0x11,
    VK_B                    = 0x12,
    VK_C                    = 0x13,
    VK_D                    = 0x14,
    VK_E                    = 0x15,
    VK_F                    = 0x16,
    VK_G                    = 0x17,
    VK_H                    = 0x18,
    VK_I                    = 0x19,
    VK_J                    = 0x1A,
    VK_K                    = 0x1B,
    VK_L                    = 0x1C,
    VK_M                    = 0x1D,
    VK_N                    = 0x1E,
    VK_O                    = 0x1F,
    VK_P                    = 0x20,
    VK_Q                    = 0x21,
    VK_R                    = 0x22,
    VK_S                    = 0x23,
    VK_T                    = 0x24,
    VK_U                    = 0x25,
    VK_V                    = 0x26,
    VK_W                    = 0x27,
    VK_X                    = 0x28,
    VK_Y                    = 0x29,
    VK_Z                    = 0x2A,

    VK_TICK                 = 0x2B,
    VK_BACKTICK             = 0x2C,
    VK_BACK                 = 0x2D,
    VK_TAB                  = 0x2E,
    VK_DOT                  = 0x2F,
    VK_COMMA                = 0x30,
    VK_SEMICOLON            = 0x31,
    VK_CLEAR                = 0x32,
    VK_ENTER                = 0x33,
    VK_SHIFT                = 0x34,
    VK_CONTROL              = 0x35,
    VK_ALT                  = 0x36,
    VK_PAUSE                = 0x37,
    VK_CAPSLOCK             = 0x38,
    VK_HYPHEN               = 0x39,
    VK_ESCAPE               = 0x3A,
    VK_SPACE                = 0x3B,
    VK_SLASH                = 0x3C,
    VK_BACKSLASH            = 0x3D,
    VK_APOSTROPHE           = 0x3E,
    VK_EQUAL                = 0x3F,
    VK_LBRACKET             = 0x40,
    VK_RBRACKET             = 0x41,
    VK_PAGEUP               = 0x42,
    VK_PAGEDOWN             = 0x43,
    VK_END                  = 0x44,
    VK_HOME                 = 0x45,
    VK_LEFT                 = 0x46,
    VK_UP                   = 0x47,
    VK_RIGHT                = 0x48,
    VK_DOWN                 = 0x49,
    VK_SELECT               = 0x4A,
    VK_PRINT                = 0x4B,
    VK_EXECUTE              = 0x4C,
    VK_SNAPSHOT             = 0x4D,
    VK_INSERT               = 0x4E,
    VK_DELETE               = 0x4F,
    VK_HELP                 = 0x50,

    VK_NUMLOCK              = 0x51,
    VK_SCROLL               = 0x52,

    VK_LSHIFT               = 0x53,
    VK_RSHIFT               = 0x54,
    VK_LCONTROL             = 0x55,
    VK_RCONTROL             = 0x56,
    VK_LALT                 = 0x57,
    VK_RALT                 = 0x58,

    VK_LWIN                 = 0x59,
    VK_RWIN                 = 0x5A,
    VK_APPS                 = 0x5B,

    VK_F1                   = 0x5C,
    VK_F2                   = 0x5D,
    VK_F3                   = 0x5E,
    VK_F4                   = 0x5F,
    VK_F5                   = 0x60,
    VK_F6                   = 0x61,
    VK_F7                   = 0x62,
    VK_F8                   = 0x63,
    VK_F9                   = 0x64,
    VK_F10                  = 0x65,
    VK_F11                  = 0x66,
    VK_F12                  = 0x67,
    VK_F13                  = 0x68,
    VK_F14                  = 0x69,
    VK_F15                  = 0x70,
    VK_F16                  = 0x71,
    VK_F17                  = 0x72,
    VK_F18                  = 0x73,
    VK_F19                  = 0x74,
    VK_F20                  = 0x75,
    VK_F21                  = 0x76,
    VK_F22                  = 0x77,
    VK_F23                  = 0x78,
    VK_F24                  = 0x79,

    VK_BROWSER_BACK         = 0x7A,
    VK_BROWSER_FORWARD      = 0x7B,
    VK_BROWSER_REFRESH      = 0x7C,
    VK_BROWSER_STOP         = 0x7D,
    VK_BROWSER_SEARCH       = 0x7E,
    VK_BROWSER_FAVORITES    = 0x7F,
    VK_BROWSER_HOME         = 0x80,
    VK_VOLUME_MUTE          = 0x81,
    VK_VOLUME_DOWN          = 0x82,
    VK_VOLUME_UP            = 0x83,
    VK_MEDIA_NEXT_TRACK     = 0x84,
    VK_MEDIA_PREV_TRACK     = 0x85,
    VK_MEDIA_STOP           = 0x86,
    VK_MEDIA_PLAY_PAUSE     = 0x87,
    VK_LAUNCH_MAIL          = 0x88,
    VK_LAUNCH_MEDIA_SELECT  = 0x89,
    VK_LAUNCH_APP1          = 0x8A,
    VK_LAUNCH_APP2          = 0x8B,
    VK_POWER                = 0x8C,
    VK_WAKE                 = 0x8D,
    VK_SLEEP                = 0x8E,
    VK_PLAY                 = 0x8F,
    VK_ZOOM                 = 0x90,

    VK_KEYCOUNT             = 0x91
} KeyCode_t;

#endif //!_KEYCODES_INTERFACE_H_
