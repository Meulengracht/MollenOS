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
 * MollenOS X86 PS2 Controller (Keyboard) Driver
 * http://wiki.osdev.org/PS2
 */

#ifndef _DRIVER_PS2_KEYBOARD_H_
#define _DRIVER_PS2_KEYBOARD_H_

#include <os/osdefs.h>
#include "../hid_events_protocol_client.h"

// PS2 keyboard specific commands
#define PS2_KEYBOARD_SETLEDS                0xED
#define PS2_KEYBOARD_SCANCODE               0xF0
#define PS2_KEYBOARD_TYPEMATIC              0xF3
#define PS2_KEYBOARD_SETDEFAULT             0xF6

#define PS2_CODE_EXTENDED                   0xE0
#define PS2_CODE_RELEASED                   0xF0
#define PS2_KEYBOARD_ECHO                   0xEE

#define PS2_REPEATS_PERSEC(Hz)              (0x1F - (Hz - 2))
#define PS2_DELAY_250MS                     0
#define PS2_DELAY_500MS                     0x20
#define PS2_DELAY_750MS                     0x40
#define PS2_DELAY_1000MS                    0x60
#define KEY_MODIFIER_EXTENDED               0x8000

#define PS2_KEYBOARD_DATA_XLATION(Port)     (Port)->DeviceData[0]
#define PS2_KEYBOARD_DATA_SCANCODESET(Port) (Port)->DeviceData[1]
#define PS2_KEYBOARD_DATA_REPEAT(Port)      (Port)->DeviceData[2]
#define PS2_KEYBOARD_DATA_DELAY(Port)       (Port)->DeviceData[3]
#define PS2_KEYBOARD_DATA_STATE_LO(Port)    (Port)->DeviceData[4]
#define PS2_KEYBOARD_DATA_STATE_HI(Port)    (Port)->DeviceData[5]

/* ScancodeSet2ToVKey
 * Converts a scancode 2 key to the standard-defined virtual key-layout */
__EXTERN OsStatus_t 
ScancodeSet2ToVKey(
    _In_ struct hid_events_key_event_args* KeyState,
    _In_ uint8_t                           Scancode);

#endif //!_DRIVER_PS2_KEYBOARD_H_
