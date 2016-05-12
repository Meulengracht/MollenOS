/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
*/

#ifndef _MOLLENOS_IPC_H_
#define _MOLLENOS_IPC_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

/* Definitions */

/***********************
* IPC Comm Type
***********************/
typedef unsigned int IpcComm_t;

/* Enumerators */

/***********************
* Base IPC Message Type
***********************/
typedef enum _MEventType
{
	EventGeneric,
	EventInput

} MEventType_t;

/***********************
* Input IPC Message Type
***********************/
typedef enum _MInputSourceType
{
	PointerUnknown = 0,
	PointerMouse,
	PointerKeyboard,
	PointerKeypad,
	PointerJoystick,
	PointerGamePad,
	PointerOther

} MInputSourceType_t;

/* Structures */

/*********************** 
 * Base IPC Message 
 ***********************/
typedef struct _MEventMessageBase
{
	/* Message Type */
	MEventType_t Type;

	/* Message Length 
	 * Including this base header */
	size_t Length;

	/* Message Source 
	 * The sender of this message */
	IpcComm_t Sender;

} MEventMessageBase_t;

/***********************
* Input IPC Message
*  - Pointer Event
***********************/
typedef struct _MEventMessageInputPointer
{
	/* Header */
	MEventMessageBase_t Header;

	/* Input Type */
	MInputSourceType_t Type;

	/* Axis Data
	* Must be relative */
	int32_t xRelative;
	int32_t yRelative;
	int32_t zRelative;

	/* Rotation Data */

} MEventMessageInputPointer_t;


/***********************
* Input IPC Message
*  - Button Event
***********************/
typedef struct _MEventMessageInputButton
{
	/* Header */
	MEventMessageBase_t Header;

	/* Input Type */
	MInputSourceType_t Type;

	/* Button Data (Keycode) */
	uint32_t Data;

	/* Button State (Press / Release) */
	uint32_t State;

} MEventMessageInputButton_t; 

/* Event Types */
#define MCORE_INPUT_LEFT_MOUSEBUTTON	0x1
#define MCORE_INPUT_RIGHT_MOUSEBUTTON	0x2
#define MCORE_INPUT_MIDDLE_MOUSEBUTTON	0x4

#define MCORE_INPUT_BUTTON_RELEASED		0x0
#define MCORE_INPUT_BUTTON_CLICKED		0x1

/***********************
* Input IPC Message
***********************/
typedef union _MEventMessage
{
	/* Base Message
	 * Always use this to determine 
	 * which structure to access */
	MEventMessageBase_t Base;

	/* Input Events 
	 * Contain data as mouse position, 
	 * button states etc */
	MEventMessageInputPointer_t EventPointer;
	MEventMessageInputButton_t EventButton;

} MEventMessage_t;

#endif //!_MOLLENOS_IPC_H_