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
#ifndef _X86_PIT_
#define _X86_PIT_


/* Includes */
#include <Arch.h>
#include <crtdefs.h>
#include <stdint.h>

/* Definitions */
#define	X86_PIT_REGISTER_COUNTER0		0x40
#define X86_PIT_REGISTER_COMMAND		0x43

#define X86_PIT_COMMAND_SQUAREWAVEGEN	0x6
#define	X86_PIT_COMMAND_RL_DATA			0x30

#define	X86_PIT_COMMAND_COUNTER_0		0
#define	X86_PIT_COMMAND_COUNTER_1		0x40
#define	X86_PIT_COMMAND_COUNTER_2		0x80

#define X86_PIT_IRQ						0x0

/* Prototypes */
_CRT_EXTERN OsStatus_t PitInit(void);

/* Sleepy Functions */
_CRT_EXTERN uint64_t PitGetClocks(void);
_CRT_EXTERN void PitSleep(uint32_t MilliSeconds);
_CRT_EXTERN void PitStall(uint32_t MilliSeconds);

#endif