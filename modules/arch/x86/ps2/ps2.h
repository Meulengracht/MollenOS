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
 * MollenOS X86 PS2 Controller (Controller) Driver
 * http://wiki.osdev.org/PS2
 */

#ifndef _DRIVER_PS2_CONTROLLER_H_
#define _DRIVER_PS2_CONTROLLER_H_

/* Includes 
 * - System */
#include <os/osdefs.h>
#include <os/driver/io.h>
#include <os/driver/interrupt.h>

/* Io-space for accessing the PS2
 * Spans over 2 bytes from 0x60 & 0x64 */
#define PS2_IO_DATA_BASE			0x60
#define PS2_IO_STATUS_BASE			0x64
#define PS2_IO_LENGTH				0x01

#define PS2_REGISTER_DATA			0x00
#define PS2_REGISTER_STATUS			0x00
#define PS2_REGISTER_COMMAND		0x00

/* Some standard definitons for the PS2 controller 
 * like port count etc */
#define PS2_MAXPORTS				2

/* Status Stuff */
#define PS2_STATUS_OUTPUT_FULL	0x1
#define PS2_STATUS_INPUT_FULL	0x2

/* Command Stuff */
#define PS2_CMD_GET_CONFIG		0x20
#define PS2_CMD_SET_CONFIG		0x60

#define PS2_CMD_SET_SCANCODE	0xF0

#define PS2_CMD_DISABLE_PORT1	0xAD
#define PS2_CMD_ENABLE_PORT1	0xAE
#define PS2_CMD_IF_TEST_PORT1	0xAB

#define PS2_CMD_DISABLE_PORT2	0xA7
#define PS2_CMD_ENABLE_PORT2	0xA8
#define PS2_CMD_IF_TEST_PORT2	0xA9

#define PS2_CMD_IF_TEST_OK		0x00

#define PS2_CMD_SELFTEST		0xAA
#define PS2_CMD_SELFTEST_OK		0x55

#define PS2_CMD_SELECT_PORT2	0xD4
#define PS2_CMD_RESET_DEVICE	0xFF

/* The IRQ lines the PS2 Controller uses, it's 
 * an ISA line so it's fixed */
#define PS2_PORT1_IRQ				0x01
#define PS2_PORT2_IRQ				0x0C

/* The PS2 Controller Port driver structure
 * contains information about port status and
 * the current device */
typedef struct _PS2Port {
	MCoreDevice_t			Device;
	MCoreInterrupt_t		Interrupt;
	int						Enabled;
	int						Connected;
} PS2Port_t;

/* The PS2 Controller driver structure
 * contains all driver information and chip
 * current status information */
typedef struct _PS2Controller {
	MContract_t			Controller;
	DeviceIoSpace_t		DataSpace;
	DeviceIoSpace_t		CommandSpace;
	PS2Port_t			Ports[PS2_MAXPORTS];
} PS2Controller_t;

#endif //!_DRIVER_PS2_CONTROLLER_H_
