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

/* Includes 
 * - System */
#include <os/virtualkeycodes.h>
#include <os/osdefs.h>
#include "../ps2.h"

/* PS2 Keyboard definitions, useful commands and
 * ps2 keyboard ids (device ids really) */
#define PS2_KEYBOARD_SETLEDS		0xED
#define PS2_KEYBOARD_SCANCODE		0xF0
#define PS2_KEYBOARD_TYPEMATIC		0xF3
#define PS2_KEYBOARD_SETDEFAULT		0xF6

#define PS2_KEYBOARD_ECHO			0xEE

/* Typematic definitions for setting the different
 * typematic-support bits */
#define PS2_REPEATS_PERSEC(Hz)		(0x1F - (Hz - 2))
#define PS2_DELAY_250MS				0
#define PS2_DELAY_500MS				0x20
#define PS2_DELAY_750MS				0x40
#define PS2_DELAY_1000MS			0x60

/* Buffer flags definitions
 * Used to define any special behaviour when reaidng
 * data from the irq */
#define PS2_KEY_EXTENDED			0x1
#define PS2_KEY_RELEASED			0x2

/* The PS2-keyboard driver structure
 * contains current settings for the connected
 * ps2 keyboard and its features */
typedef struct _PS2Keyboard {
	PS2Port_t			*Port;
	UUId_t				Irq;
	int					Translation;
	int					ScancodeSet;

	unsigned			Flags;
	unsigned			Buffer;
	uint8_t				TypematicRepeat;
	uint8_t				TypematicDelay;
} PS2Keyboard_t;

/* ScancodeSet2ToVKey
 * Converts a scancode 2 key to the standard-defined
 * virtual key-layout */
__CRT_EXTERN VKey ScancodeSet2ToVKey(uint8_t Scancode, unsigned *Buffer, unsigned *Flags);

#endif //!_DRIVER_PS2_KEYBOARD_H_
