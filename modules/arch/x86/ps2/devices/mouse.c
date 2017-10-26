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

/* Includes 
 * - System */
#include <os/driver/input.h>
#include <os/utils.h>
#include "mouse.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* PS2MouseInterrupt 
 * Handles the ps2-mouse interrupt and extracts the
 * data for processing - fast interrupt */
InterruptStatus_t PS2MouseInterrupt(void *InterruptData)
{
	/* Initialize the data pointer */
	PS2Mouse_t *Mouse = (PS2Mouse_t*)InterruptData;
	MInput_t Input;
	uint8_t Data = 0;
	int Flush = 0;

	/* Read the data */
	Data = PS2ReadData(1);

	/* Handle any out-standing commands first */
	if (PS2PortFinishCommand(Mouse->Port, Data) == OsSuccess) {
		return InterruptHandled;
	}

	/* Now, we keep track of the number of bytes that enter
	 * the system, as it only sends one byte at the time
	 * but we need multiple */
	switch (Mouse->Index) {
		case 0:
		case 1: {
			Mouse->Buffer[Mouse->Index] = Data;
			Mouse->Index++;
		} break;

		case 2: {
			/* Read the possibly last byte */
			Mouse->Buffer[Mouse->Index] = Data;
			Mouse->Index++;

			/* Check limits */
			if (Mouse->Mode == 0) {
				Mouse->Index = 0;
				Flush = 1;
			}
		} break;

		case 3: {
			/* Read last byte */
			Mouse->Buffer[Mouse->Index] = Data;
			Mouse->Index = 0;
			Flush = 1;
		} break;

		default: 
			break;
	}

	/* Flush the data? */
	if (Flush != 0) {
		uint8_t Buttons = 0;
		int RelX = 0;
		int RelY = 0;
		int RelZ = 0;

		/* Update relative x and y */
		RelX = (int)(Mouse->Buffer[1] - ((Mouse->Buffer[0] << 4) & 0x100));
		RelY = (int)(Mouse->Buffer[2] - ((Mouse->Buffer[0] << 3) & 0x100));

		/* Get status of L-R-M buttons */
		Buttons = Mouse->Buffer[0] & 0x7;

		/* Check extended data modes */
		if (Mouse->Mode == 1) {
			RelZ = (int)(char)Mouse->Buffer[3];
		}
		else if (Mouse->Mode == 2) {
			/* 4 bit signed value */
			RelZ = (int)(char)(Mouse->Buffer[3] & 0xF);
			if (Mouse->Buffer[3] & PS2_MOUSE_4BTN) {
				Buttons |= 0x8;
			}
			if (Mouse->Buffer[3] & PS2_MOUSE_5BTN) {
				Buttons |= 0x10;
			}
		}

		/* Store a state of buttons */
		Mouse->Buttons = Buttons;

		/* Create a new input event */
		Input.Type = InputMouse;
		Input.Flags = INPUT_KEYS_PACKED;
		Input.Key = (VKey)Buttons;

		/* Set stuff */
		Input.xRelative = RelX;
		Input.yRelative = RelY;
		Input.zRelative = RelZ;

		/* Register input */
		CreateInput(&Input);
	}

	/* Yay! */
	return InterruptHandled;
}

/* PS2SetSampling
 * Updates the sampling rate for the mouse driver */
OsStatus_t PS2SetSampling(PS2Mouse_t *Mouse, uint8_t Sampling)
{
	/* Retrieve the mouse id */
	if (PS2PortQueueCommand(Mouse->Port, PS2_MOUSE_SETSAMPLE, NULL) != OsSuccess
		|| PS2PortQueueCommand(Mouse->Port, Sampling, NULL) != OsSuccess) {
		return OsError;
	}

	/* Wuhuu! */
	return OsSuccess;
}

/* PS2EnableExtensions
 * Tries to enable the 4/5 mouse button, the mouse must
 * pass the EnableScroll wheel before calling this */
OsStatus_t PS2EnableExtensions(PS2Mouse_t *Mouse)
{
	/* Variables */
	uint8_t MouseId = 0;

	/* Perform the magic sequence */
	if (PS2SetSampling(Mouse, 200) != OsSuccess
		|| PS2SetSampling(Mouse, 200) != OsSuccess
		|| PS2SetSampling(Mouse, 80) != OsSuccess) {
		return OsError;
	}

	/* Retrieve the mouse id */
	if (PS2PortQueueCommand(Mouse->Port, PS2_MOUSE_GETID, &MouseId) != OsSuccess) {
		return OsError;
	}

	/* Sanitize the mouse-id */
	if (MouseId == PS2_MOUSE_ID_EXTENDED2) {
		return OsSuccess;
	}
	else {
		return OsError;
	}
}

/* PS2EnableScroll 
 * Tries to enable the mouse scroll wheel by performing
 * the 'unlock' sequence of 200-100-80 sample */
OsStatus_t PS2EnableScroll(PS2Mouse_t *Mouse)
{
	/* Variables */
	uint8_t MouseId = 0;

	/* Perform the magic sequence */
	if (PS2SetSampling(Mouse, 200) != OsSuccess
		|| PS2SetSampling(Mouse, 100) != OsSuccess
		|| PS2SetSampling(Mouse, 80) != OsSuccess) {
		return OsError;
	}

	/* Retrieve the mouse id */
	if (PS2PortQueueCommand(Mouse->Port, PS2_MOUSE_GETID, &MouseId) != OsSuccess) {
		return OsError;
	}

	/* Sanitize the mouse-id */
	if (MouseId == PS2_MOUSE_ID_EXTENDED) {
		return OsSuccess;
	}
	else {
		return OsError;
	}
}

/* PS2MouseInitialize 
 * Initializes an instance of an ps2-mouse on
 * the given PS2-Controller port */
OsStatus_t PS2MouseInitialize(PS2Port_t *Port)
{
	/* Variables for initializing */
	PS2Mouse_t *Mouse = NULL;

	/* Allocate a new instance of the ps2 mouse */
	Mouse = (PS2Mouse_t*)malloc(sizeof(PS2Mouse_t));
	memset(Mouse, 0, sizeof(PS2Mouse_t));

	/* Initialize stuff */
	Mouse->Sampling = 100;
	Mouse->Port = Port;

	/* Start out by initializing the contract */
	InitializeContract(&Port->Contract, Port->Contract.DeviceId, 1,
		ContractInput, "PS2 Mouse Interface");

	/* Initialize the interrupt descriptor */
	Port->Interrupt.AcpiConform = 0;
	Port->Interrupt.Pin = INTERRUPT_NONE;
	Port->Interrupt.Vectors[0] = INTERRUPT_NONE;
	
	/* Select interrupt source */
	if (Port->Index == 0) {
		Port->Interrupt.Line = PS2_PORT1_IRQ;
	}
	else {
		Port->Interrupt.Line = PS2_PORT2_IRQ;
	}

	/* Setup fast-handler */
	Port->Interrupt.FastHandler = PS2MouseInterrupt;
	Port->Interrupt.Data = Mouse;

	/* Register our contract for this device */
	if (RegisterContract(&Port->Contract) != OsSuccess) {
		ERROR("PS2-Mouse: failed to install contract");
		return OsError;
	}

	/* Register the interrupt for this mouse */
	Mouse->Irq = RegisterInterruptSource(&Port->Interrupt, INTERRUPT_NOTSHARABLE);

	/* The mouse is in default state at this point
	 * since all ports suffer a reset - We want to test
	 * if the mouse is a 4-byte mouse */
	if (PS2EnableScroll(Mouse) == OsSuccess) {
		Mouse->Mode = 1;
		if (PS2EnableExtensions(Mouse) == OsSuccess) {
			Mouse->Mode = 2;
		}
	}

	/* Update sampling to 60, no need for faster updates */
	if (PS2SetSampling(Mouse, 60) == OsSuccess) {
		Mouse->Sampling = 60;
	}

	/* Last step is to enable scanning for our port */
	return PS2PortQueueCommand(Port, PS2_ENABLE_SCANNING, NULL);
}

/* PS2MouseCleanup 
 * Cleans up the ps2-mouse instance on the
 * given PS2-Controller port */
OsStatus_t PS2MouseCleanup(PS2Port_t *Port)
{
	/* Initialize the mouse pointer */
	PS2Mouse_t *Mouse = (PS2Mouse_t*)Port->Interrupt.Data;

	/* Disable scanning */
	PS2PortQueueCommand(Port, PS2_DISABLE_SCANNING, NULL);

	/* Uninstall interrupt */
	UnregisterInterruptSource(Mouse->Irq);

	/* Free the mouse structure */
	free(Mouse);

	/* Set port connected = 0 */
	Port->Signature = 0;

	/* Done! */
	return OsSuccess;
}
