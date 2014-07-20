/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS Input Manager
*/

#ifndef _MCORE_INPUT_MANAGER_H_
#define _MCORE_INPUT_MANAGER_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

/* Structures */
typedef struct _im_pointer_data
{
	/* Pointer Type */
	uint32_t type;

	/* Axis Data 
	 * Must be relative */
	int32_t x_relative;
	int32_t y_relative;
	int32_t z_relative;

	/* Rotation Data */

} input_pointer_data_t;

/* Input Types */
#define MCORE_INPUT_TYPE_UNKNOWN	0x0
#define MCORE_INPUT_TYPE_MOUSE		0x1
#define MCORE_INPUT_TYPE_KEYBOARD	0x2
#define MCORE_INPUT_TYPE_KEYPAD		0x3
#define MCORE_INPUT_TYPE_JOYSTICK	0x4
#define MCORE_INPUT_TYPE_GAMEPAD	0x5
#define MCORE_INPUT_TYPE_OTHER		0x6

typedef struct _im_button_data
{
	/* Button Type */
	uint32_t type;

	/* Button Data (Keycode) */
	uint32_t data;

	/* Button State (Press / Release) */
	uint32_t state;

} input_button_data_t;

/* Prototypes */

/* Write data to pointer pipe */
_CRT_EXTERN void input_manager_send_pointer_data(input_pointer_data_t *data);

/* Write data to button pipe */
_CRT_EXTERN void input_manager_send_button_data(input_button_data_t *data);

#endif // !_MCORE_INPUT_MANAGER_H_
