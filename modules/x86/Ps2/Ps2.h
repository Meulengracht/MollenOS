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
* MollenOS X86-32 PS/2 Controller Driver
*/

#ifndef _X86_PS2_H_
#define _X86_PS2_H_

/* Includes */
#include <stddef.h>

/* Definitions */
#define X86_PS2_IO_BASE		0x60
#define X86_PS2_DATA		0x00
#define X86_PS2_STATUS		0x04
#define X86_PS2_COMMAND		0x04

/* Status Stuff */
#define X86_PS2_STATUS_OUTPUT_FULL	0x1
#define X86_PS2_STATUS_INPUT_FULL	0x2

/* Command Stuff */
#define X86_PS2_CMD_GET_CONFIG		0x20
#define X86_PS2_CMD_SET_CONFIG		0x60

#define X86_PS2_CMD_SET_SCANCODE	0xF0

#define X86_PS2_CMD_DISABLE_PORT1	0xAD
#define X86_PS2_CMD_ENABLE_PORT1	0xAE
#define X86_PS2_CMD_IF_TEST_PORT1	0xAB

#define X86_PS2_CMD_DISABLE_PORT2	0xA7
#define X86_PS2_CMD_ENABLE_PORT2	0xA8
#define X86_PS2_CMD_IF_TEST_PORT2	0xA9

#define X86_PS2_CMD_IF_TEST_OK		0x00

#define X86_PS2_CMD_SELFTEST		0xAA
#define X86_PS2_CMD_SELFTEST_OK		0x55

#define X86_PS2_CMD_SELECT_PORT2	0xD4
#define X86_PS2_CMD_RESET_DEVICE	0xFF

#define X86_PS2_PORT1_INTERRUPT		0x01
#define X86_PS2_PORT2_INTERRUPT		0x0C

/* Prototypes */
__CRT_EXTERN void Ps2Init(void);

__CRT_EXTERN void Ps2SendCommand(uint8_t Value);
__CRT_EXTERN int Ps2WriteData(uint8_t Value);
__CRT_EXTERN uint8_t Ps2ReadData(int Dummy);

#endif