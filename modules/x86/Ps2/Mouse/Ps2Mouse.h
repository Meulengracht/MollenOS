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
* MollenOS X86-32 PS/2 Mouse Driver
*/

#ifndef _X86_PS2_MOUSE_H_
#define _X86_PS2_MOUSE_H_

/* Includes */
#include <DeviceManager.h>
#include <stddef.h>

/* Definitions */
#define X86_PS2_MOUSE_LBTN	0x1
#define X86_PS2_MOUSE_RBTN	0x2
#define X86_PS2_MOUSE_MBTN	0x4

/* Structures */
#pragma pack(push, 1)
typedef struct _Ps2MouseDevice
{
	/* Id */
	DevId_t Id;

	/* Port */
	int Port;

	/* State */
	uint8_t Buffer[3];
	uint8_t Index;

	/* Current Pos */
	int32_t MouseX;
	int32_t MouseY;

	/* Current Btns */
	uint8_t MouseButtons;

} Ps2MouseDevice_t;
#pragma pack(pop)

/* Prototypes */
__CRT_EXTERN void Ps2MouseInit(int Port);

#endif