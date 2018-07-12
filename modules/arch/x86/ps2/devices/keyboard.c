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
//#define __TRACE

#include <os/input.h>
#include <os/utils.h>
#include "keyboard.h"
#include <string.h>
#include <stdlib.h>

/* PS2KeyboardInterrupt 
 * Handles the ps2-keyboard interrupt and extracts the data for processing - fast interrupt */
InterruptStatus_t
PS2KeyboardInterrupt(
    _In_ void*		InterruptData)
{
	PS2Keyboard_t *Kybd = (PS2Keyboard_t*)InterruptData;
	VKey Result 		= VK_INVALID;
	uint8_t Scancode 	= 0;
	MInput_t Input;

	// Get the scancode, and potentielly handle it as command data
	Scancode = PS2ReadData(1);
	if (PS2PortFinishCommand(Kybd->Port, Scancode) == OsSuccess) {
		return InterruptHandled;
	}

	// Perform scancode-translation
	if (Kybd->ScancodeSet == 1) {
		TRACE("PS2-Keyboard: Scancode set 1");
	}
	else if (Kybd->ScancodeSet == 2) {
		Result = ScancodeSet2ToVKey(Scancode, &Kybd->Buffer, &Kybd->Flags);
	}

	/* Now, if it's not VK_INVALID 
	 * we would like to send a new input package */
	if (Result != VK_INVALID) {
		TRACE("KEY: 0x%x", Result);

		Input.Type = InputKeyboard;
		Input.Scancode = Kybd->Buffer;
		Input.Key = Result;

		/* Determine flags */
		if (Kybd->Flags & PS2_KEY_RELEASED) {
			Input.Flags = INPUT_BUTTON_RELEASED;
		}
		else {
			Input.Flags = INPUT_BUTTON_CLICKED;
		}

		/* Create input */
		CreateInput(&Input);

		/* Reset buffer and flags */
		Kybd->Buffer = 0;
		Kybd->Flags = 0;
	}
	return InterruptHandled;
}

/* PS2KeyboardGetScancode
 * Retrieves the current scancode-set for the keyboard */
OsStatus_t
PS2KeyboardGetScancode(
	_In_  PS2Keyboard_t*	Kybd,
	_Out_ int*				ResultSet)
{
	uint8_t Response = 0;
	if (PS2PortQueueCommand(Kybd->Port, PS2_KEYBOARD_SCANCODE, &Response) != OsSuccess) {
		return OsError;
	}

	// Bit 6 is set in case its translation
	*ResultSet = (int)(Response & 0xF);
	if (Response >= 0xF0) {
		return OsError;
	}
	else {
		return OsSuccess;
	}
}

/* PS2KeyboardSetScancode
 * Updates the current scancode-set for the keyboard */
OsStatus_t PS2KeyboardSetScancode(
	_In_  PS2Keyboard_t* 	Kybd,
	_In_  uint8_t 			RequestSet,
	_Out_ int*				ResultSet)
{
	if (PS2PortQueueCommand(Kybd->Port, PS2_KEYBOARD_SCANCODE, NULL) != OsSuccess
		|| PS2PortQueueCommand(Kybd->Port, RequestSet, NULL) != OsSuccess) {
		return OsError;
	}
	else {
		*ResultSet = (int)RequestSet;
		return OsSuccess;
	}
}

/* PS2KeyboardSetTypematics
 * Updates the current typematics for the keyboard */
OsStatus_t
PS2KeyboardSetTypematics(
	_In_ PS2Keyboard_t*	Kybd,
	_In_ uint8_t 		TypematicRepeat,
	_In_ uint8_t 		Delay)
{
	uint8_t Format = 0;

	// Build the data-packet
	Format |= TypematicRepeat;
	Format |= Delay;

	// Write the command to get scancode set
	if (PS2PortQueueCommand(Kybd->Port, PS2_KEYBOARD_TYPEMATIC, NULL) != OsSuccess
		|| PS2PortQueueCommand(Kybd->Port, Format, NULL) != OsSuccess) {
		return OsError;
	}
	else {
		return OsSuccess;
	}
}

/* PS2KeyboardSetLEDs
 * Updates the LED statuses for the ps2 keyboard */
OsStatus_t
PS2KeyboardSetLEDs(
	_In_ PS2Keyboard_t*	Kybd,
	_In_ int 			Scroll,
	_In_ int 			Number,
	_In_ int 			Caps)
{
	uint8_t Format = 0;

	// Build the data-packet
	Format |= ((uint8_t)(Scroll & 0x1) << 0);
	Format |= ((uint8_t)(Number & 0x1) << 1);
	Format |= ((uint8_t)(Caps & 0x1) << 2);

	// Write the command to get scancode set
	if (PS2PortQueueCommand(Kybd->Port, PS2_KEYBOARD_SETLEDS, NULL) != OsSuccess
		|| PS2PortQueueCommand(Kybd->Port, Format, NULL) != OsSuccess) {
		return OsError;
	}
	else {
		return OsSuccess;
	}
}

/* PS2KeyboardInitialize 
 * Initializes an instance of an ps2-keyboard on the given PS2-Controller port */
OsStatus_t
PS2KeyboardInitialize(
	_In_ PS2Port_t*	Port,
	_In_ int 		Translation)
{
	PS2Keyboard_t *Kybd = NULL; 

	Kybd = (PS2Keyboard_t*)malloc(sizeof(PS2Keyboard_t));
	memset(Kybd, 0, sizeof(PS2Keyboard_t));

	// Initialize keyboard defaults
	Kybd->Port 				= Port;
	Kybd->Translation 		= Translation;
	Kybd->TypematicRepeat 	= PS2_REPEATS_PERSEC(16);
	Kybd->TypematicDelay 	= PS2_DELAY_500MS;
	Kybd->ScancodeSet 		= 2;

	// Start out by initializing the contract
	InitializeContract(&Port->Contract, Port->Contract.DeviceId, 1,
		ContractInput, "PS2 Keyboard Interface");

	// Initialize the interrupt descriptor
	Port->Interrupt.AcpiConform = 0;
	Port->Interrupt.Pin 		= INTERRUPT_NONE;
	Port->Interrupt.Vectors[0] 	= INTERRUPT_NONE;
	if (Port->Index == 0) {
		Port->Interrupt.Line 	= PS2_PORT1_IRQ;
	}
	else {
		Port->Interrupt.Line 	= PS2_PORT2_IRQ;
	}
	Port->Interrupt.FastHandler = PS2KeyboardInterrupt;
	Port->Interrupt.Data 		= Kybd;

	// Register our contract for this device
	if (RegisterContract(&Port->Contract) != OsSuccess) {
		ERROR("PS2-Keyboard: failed to install contract");
		return OsError;
	}
	Kybd->Irq = RegisterInterruptSource(&Port->Interrupt, INTERRUPT_NOTSHARABLE);

	// Reset keyboard LEDs status
	if (PS2KeyboardSetLEDs(Kybd, 0, 0, 0) != OsSuccess) {
		ERROR("PS2-Keyboard: failed to reset LEDs");
	}

	// Update typematics to preffered settings
	if (PS2KeyboardSetTypematics(Kybd,
			Kybd->TypematicRepeat, Kybd->TypematicDelay) != OsSuccess) {
		WARNING("PS2-Keyboard: failed to set typematic settings");
	}
	
	// Select our preffered scancode set
	if (PS2KeyboardSetScancode(Kybd, 2, &Kybd->ScancodeSet) != OsSuccess) {
		WARNING("PS2-Keyboard: failed to select scancodeset 2 (%i)",
			Kybd->ScancodeSet);
	}
	return PS2PortQueueCommand(Port, PS2_ENABLE_SCANNING, NULL);
}

/* PS2KeyboardCleanup 
 * Cleans up the ps2-keyboard instance on the given PS2-Controller port */
OsStatus_t
PS2KeyboardCleanup(
	_In_ PS2Port_t*	Port)
{
	PS2Keyboard_t *Kybd = (PS2Keyboard_t*)Port->Interrupt.Data;
	
	// Send the disable command before removing device
	PS2PortQueueCommand(Port, PS2_DISABLE_SCANNING, NULL);
	UnregisterInterruptSource(Kybd->Irq);

	// Cleanup resources
	free(Kybd);
	Port->Signature = 0;
	return OsSuccess;
}
