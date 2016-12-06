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
* MollenOS X86-32 PS/2 Keyboard Driver
*/

#ifndef _X86_PS2_KEYBOARD_H_
#define _X86_PS2_KEYBOARD_H_

/* Includes */
#include <DeviceManager.h>
#include <stddef.h>

/* Definitions */
#define X86_PS2_KBD_FLAG_RELEASED	0x1
#define X86_PS2_KBD_FLAG_EXTENDED	0x2

/* Structures */
#pragma pack(push, 1)
typedef struct _Ps2KeyboardDevice
{
	/* Id */
	DevId_t Id;

	/* Port */
	int32_t Port; 

	/* Scancode Set */
	int32_t ScancodeSet;

	/* Buffer */
	uint8_t Flags;
	int32_t Buffer;

} Ps2KeyboardDevice_t;
#pragma pack(pop)

/* Prototypes */
__CRT_EXTERN void Ps2KeyboardInit(int Port, int Translation);

#endif