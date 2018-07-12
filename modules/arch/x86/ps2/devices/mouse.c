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

#include <os/input.h>
#include <os/utils.h>
#include <string.h>
#include <stdlib.h>
#include "mouse.h"

/* PS2MouseInterrupt 
 * Handles the ps2-mouse interrupt and extracts the data for processing - fast interrupt */
InterruptStatus_t
PS2MouseInterrupt(
	_In_ void*		InterruptData)
{
	PS2Mouse_t *Mouse 	= (PS2Mouse_t*)InterruptData;
	uint8_t Data 		= 0;
	int Flush 			= 0;
	MInput_t Input;

	// Get the scancode, and potentielly handle it as command data
	Data = PS2ReadData(1);
	if (PS2PortFinishCommand(Mouse->Port, Data) == OsSuccess) {
		return InterruptHandled;
	}

	// Now, we keep track of the number of bytes that enter
	// the system, as it only sends one byte at the time
	// but we need multiple
	switch (Mouse->Index) {
		case 0:
		case 1: {
			Mouse->Buffer[Mouse->Index] = Data;
			Mouse->Index++;
		} break;

		case 2: {
			Mouse->Buffer[Mouse->Index] = Data;
			Mouse->Index++;

			// Are we done?
			if (Mouse->Mode == 0) {
				Mouse->Index = 0;
				Flush = 1;
			}
		} break;

		case 3: {
			Mouse->Buffer[Mouse->Index] = Data;
			Mouse->Index 	= 0;
			Flush 			= 1;
		} break;

		default: 
			break;
	}

	// If flush is set, then write it
	if (Flush != 0) {
		uint8_t Buttons = 0;
		int RelX 		= 0;
		int RelY 		= 0;
		int RelZ 		= 0;

		// Update relative x and y
		RelX = (int)(Mouse->Buffer[1] - ((Mouse->Buffer[0] << 4) & 0x100));
		RelY = (int)(Mouse->Buffer[2] - ((Mouse->Buffer[0] << 3) & 0x100));

		// Get status of L-R-M buttons
		Buttons = Mouse->Buffer[0] & 0x7;

		// Check extended data modes
		if (Mouse->Mode == 1) {
			RelZ = (int)(char)Mouse->Buffer[3];
		}
		else if (Mouse->Mode == 2) {
			// 4 bit signed value
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
	return InterruptHandled;
}

/* PS2SetSampling
 * Updates the sampling rate for the mouse driver */
OsStatus_t
PS2SetSampling(
	_In_ PS2Mouse_t*	Mouse,
	_In_ uint8_t 		Sampling)
{
	if (PS2PortQueueCommand(Mouse->Port, PS2_MOUSE_SETSAMPLE, NULL) != OsSuccess
		|| PS2PortQueueCommand(Mouse->Port, Sampling, NULL) != OsSuccess) {
		return OsError;
	}
	return OsSuccess;
}

/* PS2EnableExtensions
 * Tries to enable the 4/5 mouse button, the mouse must
 * pass the EnableScroll wheel before calling this */
OsStatus_t
PS2EnableExtensions(
	_In_ PS2Mouse_t*	Mouse)
{
	uint8_t MouseId = 0;

	if (PS2SetSampling(Mouse, 200) != OsSuccess
		|| PS2SetSampling(Mouse, 200) != OsSuccess
		|| PS2SetSampling(Mouse, 80) != OsSuccess) {
		return OsError;
	}
	if (PS2PortQueueCommand(Mouse->Port, PS2_MOUSE_GETID, &MouseId) != OsSuccess) {
		return OsError;
	}

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
OsStatus_t
PS2EnableScroll(
	_In_ PS2Mouse_t*	Mouse)
{
	uint8_t MouseId = 0;

	if (PS2SetSampling(Mouse, 200) != OsSuccess
		|| PS2SetSampling(Mouse, 100) != OsSuccess
		|| PS2SetSampling(Mouse, 80) != OsSuccess) {
		return OsError;
	}

	if (PS2PortQueueCommand(Mouse->Port, PS2_MOUSE_GETID, &MouseId) != OsSuccess) {
		return OsError;
	}

	if (MouseId == PS2_MOUSE_ID_EXTENDED) {
		return OsSuccess;
	}
	else {
		return OsError;
	}
}

/* PS2MouseInitialize 
 * Initializes an instance of an ps2-mouse on the given PS2-Controller port */
OsStatus_t
PS2MouseInitialize(
	_In_ PS2Port_t*		Port)
{
	PS2Mouse_t *Mouse = NULL;

	Mouse = (PS2Mouse_t*)malloc(sizeof(PS2Mouse_t));
	memset(Mouse, 0, sizeof(PS2Mouse_t));

	Mouse->Sampling = 100;
	Mouse->Port 	= Port;

	// Start out by initializing the contract
	InitializeContract(&Port->Contract, Port->Contract.DeviceId, 1,
		ContractInput, "PS2 Mouse Interface");

	// Initialize the interrupt descriptor
	Port->Interrupt.AcpiConform = 0;
	Port->Interrupt.Pin 		= INTERRUPT_NONE;
	Port->Interrupt.Vectors[0] 	= INTERRUPT_NONE;
	if (Port->Index == 0) {
		Port->Interrupt.Line = PS2_PORT1_IRQ;
	}
	else {
		Port->Interrupt.Line = PS2_PORT2_IRQ;
	}
	Port->Interrupt.FastHandler = PS2MouseInterrupt;
	Port->Interrupt.Data 		= Mouse;

	// Register our contract for this device
	if (RegisterContract(&Port->Contract) != OsSuccess) {
		ERROR("PS2-Mouse: failed to install contract");
		return OsError;
	}
	Mouse->Irq = RegisterInterruptSource(&Port->Interrupt, INTERRUPT_NOTSHARABLE);

	// The mouse is in default state at this point
	// since all ports suffer a reset - We want to test
	// if the mouse is a 4-byte mouse
	if (PS2EnableScroll(Mouse) == OsSuccess) {
		Mouse->Mode = 1;
		if (PS2EnableExtensions(Mouse) == OsSuccess) {
			Mouse->Mode = 2;
		}
	}

	// Update sampling to 60, no need for faster updates
	if (PS2SetSampling(Mouse, 60) == OsSuccess) {
		Mouse->Sampling = 60;
	}
	return PS2PortQueueCommand(Port, PS2_ENABLE_SCANNING, NULL);
}

/* PS2MouseCleanup 
 * Cleans up the ps2-mouse instance on the given PS2-Controller port */
OsStatus_t
PS2MouseCleanup(
	_In_ PS2Port_t*		Port)
{
	PS2Mouse_t *Mouse = (PS2Mouse_t*)Port->Interrupt.Data;

	// Try to disable the device before cleaning up
	PS2PortQueueCommand(Port, PS2_DISABLE_SCANNING, NULL);
	UnregisterInterruptSource(Mouse->Irq);

	// Cleanup
	free(Mouse);
	Port->Signature = 0;
	return OsSuccess;
}
