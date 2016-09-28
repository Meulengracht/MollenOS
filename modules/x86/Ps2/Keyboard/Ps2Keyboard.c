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
* MollenOS X86-32 PS/2 Keyboard Driver
*/

/* Includes */
#include <Module.h>
#include <Heap.h>
#include <DeviceManager.h>
#include <InputManager.h>
#include <Log.h>

#include "../Ps2.h"
#include "Ps2Keyboard.h"

/* CLib */
#include <stdio.h>
#include <string.h>

/* Scansets */
#include <ScancodeSet2.h>

/* Globals */
const char *GlbPs2KeyboardDriverName = "MollenOS PS2-Keyboard Driver";

/* Irq Handler */
int Ps2KeyboadIrqHandler(void *Args)
{
	/* Get datastructure */
	MCoreDevice_t *mDev = (MCoreDevice_t*)Args;
	Ps2KeyboardDevice_t *Ps2Dev = (Ps2KeyboardDevice_t*)mDev->Driver.Data;
	
	/* Vars */
	MEventMessageInput_t bEvent;
	uint8_t Scancode = 0;

	/* Set Header */
	bEvent.Header.Type = EventInput;
	bEvent.Header.Length = sizeof(MEventMessageInput_t);

	/* Get scancode */
	Scancode = Ps2ReadData(1);

	/* Special Cases */
	if (Scancode == EXTENDED)
	{
		Ps2Dev->Flags |= X86_PS2_KBD_FLAG_EXTENDED;
		Ps2Dev->Buffer = 0xE000;
	}
	else if (Scancode == RELEASED)
		Ps2Dev->Flags |= X86_PS2_KBD_FLAG_RELEASED;
	else if (Scancode < (uint8_t)EXTENDED
			&& Scancode > 0x00)
	{
		/* Oh god actual scancode */
		Ps2Dev->Buffer |= Scancode;
		bEvent.Type = InputKeyboard;
		bEvent.Scancode = (unsigned)Ps2Dev->Buffer;
		bEvent.Flags = 
			((Ps2Dev->Flags & X86_PS2_KBD_FLAG_RELEASED) == 1) ? 
							MCORE_INPUT_BUTTON_RELEASED : MCORE_INPUT_BUTTON_CLICKED;
		
		/* Convert scancode to a shared keycode format */
		if (Ps2Dev->ScancodeSet == 2)
			bEvent.Key = ScancodeSet2ToMCore((ScancodeSet2)Ps2Dev->Buffer);

		/* Reset */
		Ps2Dev->Flags = 0;
		Ps2Dev->Buffer = 0;

		/* Send */
		EmCreateEvent(&bEvent.Header);
	}

	/* Done! */
	return X86_IRQ_HANDLED;
}

/* Setup */
void Ps2KeyboardInit(int Port, int Translation)
{
	uint8_t Response = 0;

	/* Allocate Data Structure */
	MCoreDevice_t *Device = (MCoreDevice_t*)kmalloc(sizeof(MCoreDevice_t));
	Ps2KeyboardDevice_t *Ps2Dev = (Ps2KeyboardDevice_t*)kmalloc(sizeof(Ps2KeyboardDevice_t));
	
	/* Setup ps2 driver data */
	memset(Ps2Dev, 0, sizeof(Ps2KeyboardDevice_t));
	Ps2Dev->Port = Port;
	Ps2Dev->Flags = 0;
	Ps2Dev->Buffer = 0;

	/* Setup device data */
	memset(Device, 0, sizeof(MCoreDevice_t));

	/* Setup information */
	Device->VendorId = 0x8086;
	Device->DeviceId = 0x0;
	Device->Class = DEVICEMANAGER_LEGACY_CLASS;
	Device->Subclass = 0x00000010;

	/* Irq information */
	if (Port == 1)
		Device->IrqLine = X86_PS2_PORT1_INTERRUPT;
	else
		Device->IrqLine = X86_PS2_PORT2_INTERRUPT;

	Device->IrqPin = -1;
	Device->IrqAvailable[0] = -1;
	Device->IrqHandler = Ps2KeyboadIrqHandler;

	/* Type */
	Device->Type = DeviceInput;
	Device->Data = NULL;

	/* Initial */
	Device->Driver.Name = (char*)GlbPs2KeyboardDriverName;
	Device->Driver.Version = 1;
	Device->Driver.Data = Ps2Dev;
	Device->Driver.Status = DriverActive;

	/* Register us for an irq */
	if (DmRequestResource(Device, ResourceIrq)) {
		LogFatal("PS2K", "Failed to allocate irq for use, bailing out!");

		/* Cleanup */
		kfree(Ps2Dev);
		kfree(Device);

		/* Done */
		return;
	}

	/* Create device in upper layer */
	Ps2Dev->Id = DmCreateDevice("Ps2-Keyboard", Device);

	/* Set scancode set to 2 */
	if (Port == 2)
		Ps2SendCommand(X86_PS2_CMD_SELECT_PORT2);
	
	/* Select command */
	Ps2WriteData(X86_PS2_CMD_SET_SCANCODE);
	Response = Ps2ReadData(0);

	if (Port == 2)
		Ps2SendCommand(X86_PS2_CMD_SELECT_PORT2);

	/* Send value */
	Ps2WriteData(0x2);
	Response = Ps2ReadData(0);

	if (Response == 0xFA)
		Ps2Dev->ScancodeSet = 2;

	/* Either disable or enable */
	if (Port == 2)
		Ps2SendCommand(X86_PS2_CMD_SELECT_PORT2);
	Ps2WriteData(Translation ? 0x65 : 0x25);

	/* Ack */
	Response = Ps2ReadData(0);

	/* Enable scan, 0xF4 */
	if (Port == 2)
		Ps2SendCommand(X86_PS2_CMD_SELECT_PORT2);
	Ps2WriteData(0xF4);

	/* Ack */
	Response = Ps2ReadData(0);
}