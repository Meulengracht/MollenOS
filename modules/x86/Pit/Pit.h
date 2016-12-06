/* MollenOS
*
* Copyright 2011 - 2015, Philip Meulengracht
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
* MollenOS x86-32 Pit Header
*/
#ifndef __MODULE_PIT__
#define __MODULE_PIT__


/* Includes */
#include <crtdefs.h>
#include <stdint.h>

/* Definitions */
#define X86_PIT_IO_BASE					0x40
#define	X86_PIT_REGISTER_COUNTER0		0x00
#define X86_PIT_REGISTER_COMMAND		0x03

#define X86_PIT_COMMAND_SQUAREWAVEGEN	0x6
#define	X86_PIT_COMMAND_RL_DATA			0x30

#define	X86_PIT_COMMAND_COUNTER_0		0
#define	X86_PIT_COMMAND_COUNTER_1		0x40
#define	X86_PIT_COMMAND_COUNTER_2		0x80

#define X86_PIT_IRQ						0x0

/* Sleepy Functions */
__CRT_EXTERN uint64_t PitGetClocks(void *Data);
__CRT_EXTERN void PitSleep(void *Data, uint32_t MilliSeconds);
__CRT_EXTERN void PitStall(void *Data, uint32_t MilliSeconds);

#endif