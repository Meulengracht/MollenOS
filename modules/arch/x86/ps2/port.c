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

/* Includes 
 * - System */
#include <os/mollenos.h>
#include "ps2.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Perform Interface Tests */
OsStatus_t PS2InterfaceTest(int Port)
{
	/* Variables */
	uint8_t Response = 0;

	/* Send command */
	if (Port == 0) {
		PS2SendCommand(PS2_INTERFACETEST_PORT1);
	}
	else {
		PS2SendCommand(PS2_INTERFACETEST_PORT2);
	}

	/* Wait for response */
	Response = PS2ReadData(0);

	/* Done */
	return (Response == PS2_INTERFACETEST_OK) ? OsNoError : OsError;
}

/* PS2ResetPort
 * Resets the given port and tests for a reset-ok */
OsStatus_t PS2ResetPort(int Index)
{
	/* Variables */
	uint8_t Response = 0;

	/* Select port 2 if needed */
	if (Index != 0) {
		PS2SendCommand(PS2_SELECT_PORT2);
	}
	
	/* Perform the test */
	PS2WriteData(PS2_RESET_PORT);
	Response = PS2ReadData(0);

	/* Check */
	if (Response == 0xAA
		|| Response == 0xFA) {
		/* Do a few extra dummy reads */
		Response = PS2ReadData(1);
		Response = PS2ReadData(1);
		return OsNoError;
	}
	else {
		return OsError;
	}
}

/* PS2IdentifyPort
 * Identifies the device currently connected to the
 * given port index, if fails it returns 0xFFFFFFFF */
DevInfo_t PS2IdentifyPort(int Index)
{
	/* Variables needed for identification */
	uint8_t Response = 0, ResponseExtra = 0;

	/* Select port 2 if needed */
	if (Index != 0) {
		PS2SendCommand(PS2_SELECT_PORT2);
	}

	/* Disable Scanning */
	if (PS2WriteData(PS2_DISABLE_SCANNING) != OsNoError) {
		return 0xFFFFFFFF;
	}

	/* Wait for response */
	Response = PS2ReadData(0);

	/* Sanitize the response */
	if (Response != 0xFA) {
		return 0xFFFFFFFF;
	}

	/* Select port 2 if needed */
	if (Index != 0) {
		PS2SendCommand(PS2_SELECT_PORT2);
	}

	/* Identify the port */
	if (PS2WriteData(PS2_IDENTIFY_PORT) != OsNoError) {
		return 0xFFFFFFFF;
	}

	/* Wait for response */
	Response = PS2ReadData(0);

	/* Sanity */
	if (Response != 0xFA) {
		return 0xFFFFFFFF;
	}

	/* Get response */
GetResponse:
	Response = PS2ReadData(0);
	if (Response == 0xFA) {
		goto GetResponse;
	}

	/* Get next byte if it exists */
	ResponseExtra = PS2ReadData(0);
	if (ResponseExtra == 0xFF) {
		ResponseExtra = 0;
	}

	/* Done */
	return (Response << 8) | ResponseExtra;
}

/* PS2InitializePort
 * Initializes the given port and tries
 * to identify the device on the port */
OsStatus_t PS2InitializePort(int Index, PS2Port_t *Port)
{
	/* Variables */
	uint8_t Temp = 0;

	/* Start out by doing an interface
	 * test on the given port */
	if (PS2InterfaceTest(Index) != OsNoError) {
		return OsError;
	}

	/* We want to enable the port now */
	if (Index == 0) {
		PS2SendCommand(PS2_ENABLE_PORT1);
	}
	else {
		PS2SendCommand(PS2_ENABLE_PORT2);
	}

	/* Get controller configuration */
	PS2SendCommand(PS2_GET_CONFIGURATION);
	Temp = PS2ReadData(0);

	/* Check if the port is enabled */
	if (Temp & (1 << (4 + Index))) {
		return OsError; /* It failed to enable.. */
	}

	/* Enable irqs for the port */
	Temp |= (1 << Index);

	/* Write back the configuration */
	PS2SendCommand(PS2_SET_CONFIGURATION);
	if (PS2WriteData(Temp) != OsNoError) {
		return OsError; /* Failed to update configuration */
	}

	/* Try to reset the port */
	if (PS2ResetPort(Index) != OsNoError) {
		return OsError;	/* Failed to reset */
	}

	/* Identify the device on the port */
	MollenOSSystemLog("PS2-Port: Identified device 0x%x on port %i",
		PS2IdentifyPort(Index), Index);
	_CRT_UNUSED(Port);

	/* Done! */
	return OsNoError;
}
