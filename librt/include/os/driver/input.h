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
 * MollenOS InterProcess Comm Interface
 * - Input Events
 */

#ifndef _MOLLENOS_INPUT_H_
#define _MOLLENOS_INPUT_H_

/* Includes
 * - C-Library */
#include <os/virtualkeycodes.h>
#include <os/osdefs.h>

/* The different types of input
 * that can be sent input messages
 * from, check the Type field in the
 * structure of an input message */
typedef enum _MInputType {
	InputUnknown = 0,
	InputMouse,
	InputKeyboard,
	InputKeypad,
	InputJoystick,
	InputGamePad,
	InputOther
} MInputType_t;

/* Window input structure, contains
 * information about the recieved input
 * event with button data etc */
typedef struct _MEventInput {
	MInputType_t		Type;
	unsigned			Scancode;
	VKey				Key;
	unsigned			Flags;	/* Flags (Bit-field, see under structure) */
	ssize_t				xRelative;
	ssize_t				yRelative;
	ssize_t				zRelative;
} MEventInput_t;

/* Flags - Event Types */
#define INPUT_BUTTON_RELEASED		0x0
#define INPUT_BUTTON_CLICKED		0x1
#define INPUT_MULTIPLEKEYS			0x2

#endif //!_MOLLENOS_INPUT_H_
