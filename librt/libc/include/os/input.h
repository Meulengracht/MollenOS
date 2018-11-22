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
 * MollenOS MCore - Input Support Definitions & Structures
 * - This header describes the base input-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __INPUT_INTERFACE_H__
#define __INPUT_INTERFACE_H__

#include <os/osdefs.h>
#include <os/keycodes.h>

// Type of supported input devices in the operating system.
typedef enum _DeviceInputType {
    DeviceInputKeyboard     = 0,
    DeviceInputKeypad,
    DeviceInputPointer,
    DeviceInputJoystick,
    DeviceInputGamePad
} DeviceInputType_t;

#define KEY_MODIFIER_LSHIFT     0x0001
#define KEY_MODIFIER_RSHIFT     0x0002
#define KEY_MODIFIER_LALT       0x0004
#define KEY_MODIFIER_RALT       0x0008
#define KEY_MODIFIER_LCTRL      0x0010
#define KEY_MODIFIER_RCTRL      0x0020

#define KEY_MODIFIER_SCROLLLOCK 0x0040
#define KEY_MODIFIER_NUMLOCK    0x0080
#define KEY_MODIFIER_CAPSLOCK   0x0100

#define KEY_MODIFIER_RELEASED   0x1000

/* SystemKey
 * Represents a key event coming from an input pipe. Contains state details
 * and different text representations. */
PACKED_TYPESTRUCT(SystemKey, {
    uint8_t     KeyAscii;
    uint8_t     KeyCode;
    uint16_t    Flags;
    uint32_t    KeyUnicode;
});

/* SystemInput
 * Represents an input event that is used for non-key events. This could be raw pointer
 * data like mouse-data or joystick-data. */
PACKED_TYPESTRUCT(SystemInput, {
    uint8_t     Type;
    uint8_t     Flags;
    int16_t     RelativeX;
    int16_t     RelativeY;
    int16_t     RelativeZ;
    uint32_t    Buttons;
});

_CODE_BEGIN
/* WriteSystemInput
 * Notifies the operating system of new input, this input is written to the system's
 * standard input, which is then sent to the window-manager if present. */
CRTDECL(OsStatus_t,
WriteSystemInput(
    _In_ SystemInput_t* Input));

/* WriteSystemKey
 * Notifies the operating system of new key-event, this key is written to the system's
 * standard input, which is then sent to the window-manager if present. */
CRTDECL(OsStatus_t,
WriteSystemKey(
    _In_ SystemKey_t*   Key));

/* ReadSystemKey
 * Reads a system key from the process's stdin handle. This returns
 * the raw system key with no processing performed on the key. */
CRTDECL(OsStatus_t,
ReadSystemKey(
    _In_ SystemKey_t*   Key));

/* TranslateSystemKey
 * Performs the translation on the keycode in the system key structure. This fills
 * in the <KeyUnicode> and <KeyAscii> members by translation of the active keymap. */
CRTDECL(OsStatus_t,
TranslateSystemKey(
    _In_ SystemKey_t*   Key));
_CODE_END

#endif //!__INPUT_INTERFACE_H__
