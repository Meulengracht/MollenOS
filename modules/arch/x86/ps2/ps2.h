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
#include <os/driver/contracts/base.h>
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

/* Status definitons from reading the status
 * register in the PS2-Controller */
#define PS2_STATUS_OUTPUT_FULL		0x1
#define PS2_STATUS_INPUT_FULL		0x2

/* Command Stuff */
#define PS2_GET_CONFIGURATION		0x20
#define PS2_SET_CONFIGURATION		0x60
#define PS2_INTERFACETEST_PORT1		0xAB
#define PS2_INTERFACETEST_PORT2		0xA9
#define PS2_SELFTEST				0xAA
#define PS2_SELECT_PORT2			0xD4
#define PS2_RESET_PORT				0xFF
#define PS2_ENABLE_SCANNING			0xF4
#define PS2_DISABLE_SCANNING		0xF5
#define PS2_IDENTIFY_PORT			0xF2

#define PS2_SELFTEST_OK				0x55
#define PS2_INTERFACETEST_OK		0x00

#define PS2_DISABLE_PORT1			0xAD
#define PS2_ENABLE_PORT1			0xAE

#define PS2_DISABLE_PORT2			0xA7
#define PS2_ENABLE_PORT2			0xA8

/* Configuration definitions used by the above
 * commands to read/write the configuration of the PS 2 */
#define PS2_CONFIG_PORT1_IRQ		0x01
#define PS2_CONFIG_PORT2_IRQ		0x02
#define PS2_CONFIG_POST				0x04
#define PS2_CONFIG_PORT1_DISABLED	0x10
#define PS2_CONFIG_PORT2_DISABLED	0x20
#define PS2_CONFIG_TRANSLATION		0x40		/* First PS/2 port translation (1 = enabled, 0 = disabled) */

#define PS2_CMD_SET_SCANCODE		0xF0

/* The IRQ lines the PS2 Controller uses, it's 
 * an ISA line so it's fixed */
#define PS2_PORT1_IRQ				0x01
#define PS2_PORT2_IRQ				0x0C

/* The PS2 Controller Port driver structure
 * contains information about port status and
 * the current device */
typedef struct _PS2Port {
	MContract_t				Contract;
	MCoreInterrupt_t		Interrupt;
	int						Enabled;
	int						Connected;
	DevInfo_t				Signature;
} PS2Port_t;

/* The PS2 Controller driver structure
 * contains all driver information and chip
 * current status information */
typedef struct _PS2Controller {
	MContract_t				Controller;
	DeviceIoSpace_t			DataSpace;
	DeviceIoSpace_t			CommandSpace;
	PS2Port_t				Ports[PS2_MAXPORTS];
} PS2Controller_t;

/* PS2InitializePort
 * Initializes the given port and tries 
 * to identify the device on the port */
__CRT_EXTERN OsStatus_t PS2InitializePort(int Index, PS2Port_t *Port);

/* PS2ReadData
 * Reads a byte from the PS2 controller data port */
__CRT_EXTERN uint8_t PS2ReadData(int Dummy);

/* PS2WriteData
 * Writes a data byte to the PS2 controller data port */
__CRT_EXTERN OsStatus_t PS2WriteData(uint8_t Value);

/* PS2SendCommand
 * Writes the given command to the ps2-controller */
__CRT_EXTERN void PS2SendCommand(uint8_t Command);

/* PS2MouseInitialize 
 * Initializes an instance of an ps2-mouse on
 * the given PS2-Controller port */
__CRT_EXTERN OsStatus_t PS2MouseInitialize(int Index, PS2Port_t *Port);

/* PS2MouseCleanup 
 * Cleans up the ps2-mouse instance on the
 * given PS2-Controller port */
__CRT_EXTERN OsStatus_t PS2MouseCleanup(int Index, PS2Port_t *Port);

/* PS2KeyboardInitialize 
 * Initializes an instance of an ps2-keyboard on
 * the given PS2-Controller port */
__CRT_EXTERN OsStatus_t PS2KeyboardInitialize(int Index, PS2Port_t *Port, int Translation);

/* PS2KeyboardCleanup 
 * Cleans up the ps2-keyboard instance on the
 * given PS2-Controller port */
__CRT_EXTERN OsStatus_t PS2KeyboardCleanup(int Index, PS2Port_t *Port);

#endif //!_DRIVER_PS2_CONTROLLER_H_
