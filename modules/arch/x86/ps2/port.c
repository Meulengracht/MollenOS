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

/* PS2InterfaceTest
 * Performs an interface test on the given port*/
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

/* PS2RegisterDevice
 * Shortcut function for registering a new device */
OsStatus_t PS2RegisterDevice(PS2Port_t *Port) 
{
	/* Keep some static storage */
	MCoreDevice_t Device;

	/* Initialize the device structure */
	Device.VendorId = 0xFFEF;
	Device.DeviceId = 0x0030;

	/* Invalidate generics */
	Device.Class = 0xFF0F;
	Device.Subclass = 0xFF0F;

	/* Initialize the irq */
	Device.IrqPin = INTERRUPT_NONE;
	Device.IrqAvailable[0] = INTERRUPT_NONE;
	Device.AcpiConform = 0;

	/* Depends on port index */
	if (Port->Index == 0) {
		Device.IrqLine = PS2_PORT1_IRQ;
	}
	else {
		Device.IrqLine = PS2_PORT2_IRQ;
	}

	/* Register device */
	Port->Contract.DeviceId = RegisterDevice(&Device, 0);

	/* Yay */
	return OsNoError;
}

/* PS2PortWrite 
 * Writes the given data-byte to the ps2-port */
OsStatus_t PS2PortWrite(PS2Port_t *Port, uint8_t Value)
{
	/* Always select port if neccessary */
	if (Port->Index != 0) {
		PS2SendCommand(PS2_SELECT_PORT2);
	}

	/* Write the data */
	return PS2WriteData(Value);
}

/* PS2PortQueueCommand 
 * Queues the given command up for the given port
 * if a response is needed for the previous commnad
 * Set command = PS2_RESPONSE_COMMAND and pointer to response buffer */
OsStatus_t PS2PortQueueCommand(PS2Port_t *Port, uint8_t Command, uint8_t *Response)
{
	/* Variables */
	PS2Command_t *pCommand = NULL;
	OsStatus_t Result = OsNoError;
	int Execute = 0;
	int i;

	/* Find a free command spot */
FindCommand:
	for (i = 0; i < PS2_MAXCOMMANDS; i++) {
		if (Port->Commands[i].InUse == 0) {
			pCommand = &Port->Commands[i];
			if (Port->CommandIndex == -1) {
				Port->CommandIndex = i;
				Execute = 1;
			}
			break;
		}
	}

	/* Sanitize the command */
	if (pCommand == NULL) {
		goto FindCommand;
	}

	/* Build the packet */
	pCommand->Executed = 0;
	pCommand->Step = 0;
	pCommand->Command = Command;
	pCommand->Response = Response;
	pCommand->InUse = 1;

	/* Start the command ? */
	if (Execute == 1) {
		Result = PS2PortWrite(Port, Command);
	}

	/* Wait for response?? */
	if (Response != NULL) {
		while (pCommand->Executed != 2);
	}

	/* Done! */
	return Result;
}

/* PS2PortFinishCommand 
 * Finalizes the current command and executes
 * the next command in queue (if any). */
OsStatus_t PS2PortFinishCommand(PS2Port_t *Port, uint8_t Result)
{
	/* Variables */
	int Start = Port->CommandIndex + 1;
	int i;

	/* Sanitize ACKs */
	if (Result == PS2_ACK_COMMAND
		&& Port->CommandIndex != -1) {
		Port->Commands[Port->CommandIndex].Executed = 1;

		/* Wait for response? */
		if (Port->Commands[Port->CommandIndex].Response == NULL) {
			Port->Commands[Port->CommandIndex].InUse = 0;

			/* Find next (continue) */
			for (i = Start; i < PS2_MAXCOMMANDS; i++) {
				if (Port->Commands[i].InUse == 1) {
					Port->CommandIndex = i;
					PS2PortWrite(Port, Port->Commands[Port->CommandIndex].Command);
					break;
				}
				else {
					Port->CommandIndex = PS2_NO_COMMAND;
				}
			}

			/* Find next (start) */
			if (i == PS2_MAXCOMMANDS) {
				for (i = 0; i < Start; i++) {
					if (Port->Commands[i].InUse == 1) {
						Port->CommandIndex = i;
						PS2PortWrite(Port, Port->Commands[Port->CommandIndex].Command);
						break;
					}
					else {
						Port->CommandIndex = PS2_NO_COMMAND;
					}
				}
			}
		}

		/* Done - it was handled */
		return OsNoError;
	}
	else if (Result == PS2_RESEND_COMMAND
		&& Port->CommandIndex != -1) {
		Port->Commands[Port->CommandIndex].Step++;
		if (Port->Commands[Port->CommandIndex].Step != 3) {
			PS2PortWrite(Port, Port->Commands[Port->CommandIndex].Command);
		}
		else {
			Port->Commands[Port->CommandIndex].Executed = 2;
			Port->Commands[Port->CommandIndex].InUse = 0;

			/* Find next (continue) */
			for (i = Start; i < PS2_MAXCOMMANDS; i++) {
				if (Port->Commands[i].InUse == 1) {
					Port->CommandIndex = i;
					PS2PortWrite(Port, Port->Commands[Port->CommandIndex].Command);
					break;
				}
				else {
					Port->CommandIndex = PS2_NO_COMMAND;
				}
			}

			/* Find next (start) */
			if (i == PS2_MAXCOMMANDS) {
				for (i = 0; i < Start; i++) {
					if (Port->Commands[i].InUse == 1) {
						Port->CommandIndex = i;
						PS2PortWrite(Port, Port->Commands[Port->CommandIndex].Command);
						break;
					}
					else {
						Port->CommandIndex = PS2_NO_COMMAND;
					}
				}
			}
		}

		/* Done - it was handled */
		return OsNoError;
	}
	else if (Port->CommandIndex != -1) {
		Port->Commands[Port->CommandIndex].Executed = 2;
		Port->Commands[Port->CommandIndex].InUse = 0;

		/* Sanity */
		if (Port->Commands[Port->CommandIndex].Response != NULL) {
			*(Port->Commands[Port->CommandIndex].Response) = Result;
		}

		/* Find next (continue) */
		for (i = Start; i < PS2_MAXCOMMANDS; i++) {
			if (Port->Commands[i].InUse == 1) {
				Port->CommandIndex = i;
				PS2PortWrite(Port, Port->Commands[Port->CommandIndex].Command);
				break;
			}
			else {
				Port->CommandIndex = PS2_NO_COMMAND;
			}
		}

		/* Find next (start) */
		if (i == PS2_MAXCOMMANDS) {
			for (i = 0; i < Start; i++) {
				if (Port->Commands[i].InUse == 1) {
					Port->CommandIndex = i;
					PS2PortWrite(Port, Port->Commands[Port->CommandIndex].Command);
					break;
				}
				else {
					Port->CommandIndex = PS2_NO_COMMAND;
				}
			}
		}

		/* Done - it was handled */
		return OsNoError;
	}
	else {
		return OsError;
	}
}

/* PS2PortInitialize
 * Initializes the given port and tries
 * to identify the device on the port */
OsStatus_t PS2PortInitialize(PS2Port_t *Port)
{
	/* Variables */
	uint8_t Temp = 0;

	/* Initialize some variables for the port */
	Port->CommandIndex = PS2_NO_COMMAND;

	/* Start out by doing an interface
	 * test on the given port */
	if (PS2InterfaceTest(Port->Index) != OsNoError) {
		MollenOSSystemLog("PS2-Port (%i): Failed interface test", Port->Index);
		return OsError;
	}

	/* We want to enable the port now */
	if (Port->Index == 0) {
		PS2SendCommand(PS2_ENABLE_PORT1);
	}
	else {
		PS2SendCommand(PS2_ENABLE_PORT2);
	}

	/* Get controller configuration */
	PS2SendCommand(PS2_GET_CONFIGURATION);
	Temp = PS2ReadData(0);

	/* Check if the port is enabled */
	if (Temp & (1 << (4 + Port->Index))) {
		return OsError; /* It failed to enable.. */
	}

	/* Enable irqs for the port */
	Temp |= (1 << Port->Index);

	/* Write back the configuration */
	PS2SendCommand(PS2_SET_CONFIGURATION);
	if (PS2WriteData(Temp) != OsNoError) {
		MollenOSSystemLog("PS2-Port (%i): Failed to update configuration", Port->Index);
		return OsError; /* Failed to update configuration */
	}

	/* Try to reset the port */
	if (PS2ResetPort(Port->Index) != OsNoError) {
		MollenOSSystemLog("PS2-Port (%i): Failed port reset", Port->Index);
		return OsError;	/* Failed to reset */
	}

	/* Identify the device on the port */
	Port->Signature = PS2IdentifyPort(Port->Index);
	
	/* If valid -> Connected */
	if (Port->Signature != 0xFFFFFFFF) {
		Port->Connected = 1;
	}

	/* Done! */
	return PS2RegisterDevice(Port);
}
