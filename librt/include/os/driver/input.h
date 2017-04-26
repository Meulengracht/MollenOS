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
 * MollenOS MCore - Input Support Definitions & Structures
 * - This header describes the base input-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _INPUT_INTERFACE_H_
#define _INPUT_INTERFACE_H_

/* Includes
 * - System */
#include <os/driver/window.h>
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
typedef struct _MInput {
	MInputType_t		Type;
	unsigned			Scancode;
	VKey				Key;
	unsigned			Flags;		/* Flags (Bit-field, see under structure) */
	ssize_t				xRelative;
	ssize_t				yRelative;
	ssize_t				zRelative;
} MInput_t;

/* Flags - Event Types */
#define INPUT_BUTTON_RELEASED		0x0
#define INPUT_BUTTON_CLICKED		0x1
#define INPUT_KEYS_PACKED			0x2

/* CreateInput 
 * Creates a new input event with the given
 * type and flags. The event is either handled
 * by the window manager or proxied to the active
 * window */
#ifdef __WINDOWMANAGER_EXPORT
__WNDAPI OsStatus_t CreateInput(MInput_t *Params);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
CreateInput(MInput_t *Input)
{
	// Variables
	MRemoteCall_t Request;
	
	// Initialize rpc request
	RPCInitialize(&Request, __WINDOWMANAGER_INTERFACE_VERSION, 
		PIPE_RPCOUT, __WINDOWMANAGER_NEWINPUT);
	
	// Setup rpc arguments
	RPCSetArgument(&Request, 0, (__CONST void*)Input, 
		sizeof(MInput_t));
	
	// Fire off asynchronous event
	return RPCEvent(&Request, __WINDOWMANAGER_TARGET);
}
#endif

#endif //!_MOLLENOS_INPUT_H_
