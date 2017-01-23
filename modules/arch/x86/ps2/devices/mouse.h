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
 * MollenOS X86 PS2 Controller (Mouse) Driver
 * http://wiki.osdev.org/PS2
 */

#ifndef _DRIVER_PS2_MOUSE_H_
#define _DRIVER_PS2_MOUSE_H_

/* Includes 
 * - System */
#include <os/osdefs.h>
#include "../ps2.h"

/* PS2 Mouse definitions, useful commands and
 * ps2 mouse ids (device ids really) */
#define PS2_MOUSE_SETRESOLUTION		0xE8
#define PS2_MOUSE_GETID				0xF2
#define PS2_MOUSE_SETSAMPLE			0xF3
#define PS2_MOUSE_SETDEFAULT		0xF6
#define PS2_MOUSE_ACK				0xFA

#define PS2_MOUSE_ID_DEFAULT		0
#define PS2_MOUSE_ID_EXTENDED		3
#define PS2_MOUSE_ID_EXTENDED2		4

/* Button definitions, used to determine which
 * buttons have their states changed */
#define	PS2_MOUSE_LBTN				0x1
#define	PS2_MOUSE_RBTN				0x2
#define	PS2_MOUSE_MBTN				0x4
#define PS2_MOUSE_4BTN				0x10
#define PS2_MOUSE_5BTN				0x20

/* The PS2-mouse driver structure
 * contains current settings for the connected
 * ps2 mouse and its features */
typedef struct _PS2Mouse {
	PS2Port_t			*Port;
	UUId_t				Irq;
	int					Sampling;
	int					Mode;
	
	uint8_t				Buffer[4];
	uint8_t				Index;
	uint8_t				Buttons;
	int					CurrentX;
	int					CurrentY;
	int					CurrentZ;
} PS2Mouse_t;

#endif //!_DRIVER_PS2_MOUSE_H_
