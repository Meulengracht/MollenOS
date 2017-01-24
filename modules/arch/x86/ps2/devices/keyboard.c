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
#include <os/mollenos.h>
#include "keyboard.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* PS2KeyboardInterrupt 
 * Handles the ps2-keyboard interrupt and extracts the
 * data for processing - fast interrupt */
InterruptStatus_t PS2KeyboardInterrupt(void *InterruptData)
{
	/* Initialize the keyboard pointer */
	PS2Keyboard_t *Kybd = (PS2Keyboard_t*)InterruptData;
	VKey Result = VK_INVALID;
	uint8_t Scancode = 0;

	/* Read the scancode */
	Scancode = PS2ReadData(1);

	/* Handle stuff depending on scancode
	 * and translation status */
	if (Kybd->ScancodeSet == 2) {
		Result = ScancodeSet2ToVKey(Scancode, &Kybd->Buffer, &Kybd->Flags);
	}

	/* Now, if it's not VK_INVALID 
	 * we would like to send a new input package */
	if (Result != VK_INVALID) {
		MollenOSSystemLog("KEY: 0x%x", Result);


		/* Reset buffer and flags */
		Kybd->Buffer = 0;
		Kybd->Flags = 0;
	}

	/* Yay! */
	return InterruptHandled;
}

/* PS2KeyboardWrite 
 * Writes the given command byte to the keyboard */
OsStatus_t PS2KeyboardWrite(int Index, uint8_t Command, uint8_t Data)
{
	/* Response */
	uint8_t Response = 0;

	/* Always select port if neccessary */
	if (Index != 0) {
		PS2SendCommand(PS2_SELECT_PORT2);
	}

	/* Write command */
	if (PS2WriteData(Command) != OsNoError) {
		return OsError;
	}

	/* Write data */
	if (Data != 0xFF) {
		PS2SendCommand(PS2_SELECT_PORT2);
		if (PS2WriteData(Command) != OsNoError) {
			return OsError;
		}
	}

	/* Get response */
	Response = PS2ReadData(0);

	/* Sanitize the byte */
	if (Response == PS2_KEYBOARD_ACK) {
		return OsNoError;
	}
	else if (Response == PS2_KEYBOARD_RESEND) {
		return PS2KeyboardWrite(Index, Command, Data);
	}
	else {
		return OsError;
	}
}

/* PS2KeyboardGetScancode
 * Retrieves the current scancode-set for the keyboard */
OsStatus_t PS2KeyboardGetScancode(int Index, int *ResultSet)
{
	/* Variables */
	uint8_t Result = 0;

	/* Write the command to get scancode set */
	if (PS2KeyboardWrite(Index, 
		PS2_KEYBOARD_SCANCODE, 0) != OsNoError) {
		return OsError;
	}

	/* Read the current scancode set */
	Result = PS2ReadData(0);
	*ResultSet = (int)Result;
	if (Result > 4) {
		return OsError;
	}

	/* Wuhu! */
	return OsNoError;
}

/* PS2KeyboardSetScancode
 * Updates the current scancode-set for the keyboard */
OsStatus_t PS2KeyboardSetScancode(int Index, uint8_t RequestSet, int *ResultSet)
{
	/* Write the command to get scancode set */
	if (PS2KeyboardWrite(Index,
			PS2_KEYBOARD_SCANCODE, RequestSet) != OsNoError) {
		return OsError;
	}
	else {
		return PS2KeyboardGetScancode(Index, ResultSet);
	}
}

/* PS2KeyboardSetTypematics
 * Updates the current typematics for the keyboard */
OsStatus_t PS2KeyboardSetTypematics(int Index,
	uint8_t TypematicRepeat, uint8_t Delay)
{
	/* Variables */
	uint8_t Format = 0;

	/* Build the data-packet */
	Format |= TypematicRepeat;
	Format |= Delay;

	/* Write the command to get scancode set */
	if (PS2KeyboardWrite(Index,
			PS2_KEYBOARD_TYPEMATIC, Format) != OsNoError) {
		return OsError;
	}
	else {
		return OsNoError;
	}
}

/* PS2KeyboardSetLEDs
 * Updates the LED statuses for the ps2 keyboard */
OsStatus_t PS2KeyboardSetLEDs(int Index, 
	int Scroll, int Number, int Caps)
{
	/* Variables */
	uint8_t Format = 0;

	/* Build the data-packet */
	Format |= ((uint8_t)(Scroll & 0x1) << 0);
	Format |= ((uint8_t)(Number & 0x1) << 1);
	Format |= ((uint8_t)(Caps & 0x1) << 2);

	/* Write the command to get scancode set */
	if (PS2KeyboardWrite(Index,
			PS2_KEYBOARD_SETLEDS, Format) != OsNoError) {
		return OsError;
	}
	else {
		return OsNoError;
	}
}

/* PS2KeyboardInitialize 
 * Initializes an instance of an ps2-keyboard on
 * the given PS2-Controller port */
OsStatus_t PS2KeyboardInitialize(int Index, PS2Port_t *Port, int Translation)
{
	/* Variables for initializing */
	PS2Keyboard_t *Kybd = NULL;

	/* Allocate a new instance of the ps2 mouse */
	Kybd = (PS2Keyboard_t*)malloc(sizeof(PS2Keyboard_t));
	memset(Kybd, 0, sizeof(PS2Keyboard_t));

	/* Initialize stuff */
	Kybd->Port = Port;

	/* Initialize keyboard defaults */
	Kybd->Translation = Translation;
	Kybd->TypematicRepeat = PS2_REPEATS_PERSEC(10);
	Kybd->TypematicDelay = PS2_DELAY_750MS;
	Kybd->ScancodeSet = 2;

	/* Start out by initializing the contract */
	InitializeContract(&Port->Contract, Port->Contract.DeviceId, 1,
		ContractInput, "PS2 Keyboard Interface");

	/* Initialize the interrupt descriptor */
	Port->Interrupt.AcpiConform = 0;
	Port->Interrupt.Pin = INTERRUPT_NONE;
	Port->Interrupt.Direct[0] = INTERRUPT_NONE;
	
	/* Select interrupt source */
	if (Index == 0) {
		Port->Interrupt.Line = PS2_PORT1_IRQ;
	}
	else {
		Port->Interrupt.Line = PS2_PORT2_IRQ;
	}

	/* Setup fast-handler */
	Port->Interrupt.FastHandler = PS2KeyboardInterrupt;
	Port->Interrupt.Data = Kybd;

	/* Register our contract for this device */
	if (RegisterContract(&Port->Contract) != OsNoError) {
		MollenOSSystemLog("PS2-Keyboard: failed to install contract");
		return OsError;
	}

	/* Register the interrupt for this mouse */
	Kybd->Irq = RegisterInterruptSource(&Port->Interrupt,
		INTERRUPT_NOTSHARABLE | INTERRUPT_FAST);

	/* Reset keyboard LEDs status */
	if (PS2KeyboardSetLEDs(Index, 0, 0, 0) != OsNoError) {
		MollenOSSystemLog("PS2-Keyboard: failed to reset LEDs");
	}

	/* Update typematics to preffered settings */
	if (PS2KeyboardSetTypematics(Index, 
			Kybd->TypematicRepeat, Kybd->TypematicDelay) != OsNoError) {
		MollenOSSystemLog("PS2-Keyboard: failed to set typematic settings");
	}
	
	/* Select our preffered scancode set */
	if (PS2KeyboardSetScancode(Index, 2, &Kybd->ScancodeSet) != OsNoError) {
		MollenOSSystemLog("PS2-Keyboard: failed to select scancodeset 2");
	}

	/* Enable scanning (Keyboard is now active) */
	return PS2KeyboardWrite(Index, PS2_ENABLE_SCANNING, 0xFF);
}

/* PS2KeyboardCleanup 
 * Cleans up the ps2-keyboard instance on the
 * given PS2-Controller port */
OsStatus_t PS2KeyboardCleanup(int Index, PS2Port_t *Port)
{
	/* Initialize the keyboard pointer */
	PS2Keyboard_t *Kybd = (PS2Keyboard_t*)Port->Interrupt.Data;

	/* Disable scanning */
	PS2KeyboardWrite(Index, PS2_DISABLE_SCANNING, 0xFF);

	/* Uninstall interrupt */
	UnregisterInterruptSource(Kybd->Irq);

	/* Free the mouse structure */
	free(Kybd);

	/* Set port connected = 0 */
	Port->Connected = 0;

	/* Done! */
	return OsNoError;
}
