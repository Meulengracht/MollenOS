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
* Generic IPC Message Type
***********************/
typedef enum _MGenericMessageType
{
	GenericWindowCreate,
	GenericWindowDestroy,
	GenericWindowInvalidate,
	GenericWindowQuery

} MGenericMessageType_t;

/***********************
* Input IPC Message Type
***********************/
typedef enum _MInputSourceType
{
	InputUnknown = 0,
	InputMouse,
	InputKeyboard,
	InputKeypad,
	InputJoystick,
	InputGamePad,
	InputOther

} MInputSourceType_t;

/* Structures */

/* Define the standard os
* rectangle used for ui
* operations */
#ifndef MRECTANGLE_DEFINED
#define MRECTANGLE_DEFINED
typedef struct _mRectangle
{
	/* Origin */
	int x, y;

	/* Size */
	int h, w;

} Rect_t;
#endif

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
* Generic IPC Message
***********************/
typedef struct _MEventMessageGeneric
{
	/* Base */
	MEventMessageBase_t Header;

	/* Message Type */
	MGenericMessageType_t Type;

	/* Param 1 */
	size_t LoParam;

	/* Param 2 */
	size_t HiParam;

	/* Param Rect */
	Rect_t RcParam;

} MEventMessageGeneric_t;

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

	/* Axis Data
	 * Must be relative */
	int32_t xRelative;
	int32_t yRelative;
	int32_t zRelative;

	/* Rotation Data */

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

	/* Generic Message 
	 * Used for pretty much any
	 * message passed between processes */
	MEventMessageGeneric_t Generic;

	/* Events, these are driver messages
	 * that rely on static message space
	 * Contain data as mouse position, 
	 * button states etc */
	MEventMessageInputButton_t EventButton;

} MEventMessage_t;

#endif //!_MOLLENOS_IPC_H_