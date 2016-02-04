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
* MollenOS X86-32 PS/2 Mouse Driver
*/

/* Includes */
#include <Module.h>
#include <Heap.h>
#include <DeviceManager.h>
#include <InputManager.h>
#include <Log.h>

#include "../Ps2.h"
#include "Ps2Mouse.h"

/* CLib */
#include <string.h>

/* Globals */
const char *GlbPs2MouseDriverName = "MollenOS PS2-Mouse Driver";

/* Ps Mouse Irq */
int Ps2MouseIrqHandler(void *Args)
{
	/* We get 3 bytes describing the state
	 * BUT we can only get one at the time..... -.- */
	MCorePointerEvent_t eData;
	MCoreButtonEvent_t bData;

	/* Get datastructure */
	MCoreDevice_t *mDev = (MCoreDevice_t*)Args;
	Ps2MouseDevice_t *Ps2Dev = (Ps2MouseDevice_t*)mDev->Driver.Data;

	/* Lets see which byte */
	switch (Ps2Dev->Index)
	{
		case 0:
		{
			/* Read & Increase */
			Ps2Dev->Buffer[0] = Ps2ReadData(1);
			Ps2Dev->Index++;
		} break;
		case 1:
		{
			/* Read & Increase */
			Ps2Dev->Buffer[1] = Ps2ReadData(1);
			Ps2Dev->Index++;
		} break;
		case 2:
		{
			/* Reset */
			Ps2Dev->Buffer[2] = Ps2ReadData(1);
			Ps2Dev->Index = 0;

			/* Update */
			Ps2Dev->MouseX += (int32_t)(Ps2Dev->Buffer[1] - ((Ps2Dev->Buffer[0] << 4) & 0x100));
			Ps2Dev->MouseY += (int32_t)(Ps2Dev->Buffer[2] - ((Ps2Dev->Buffer[0] << 4) & 0x100));

			/* Redirect Mouse Pointer Data */
			eData.Type = MCORE_INPUT_TYPE_MOUSE;
			eData.xRelative = (int32_t)(Ps2Dev->Buffer[1] - ((Ps2Dev->Buffer[0] << 4) & 0x100));
			eData.yRelative = (int32_t)(Ps2Dev->Buffer[2] - ((Ps2Dev->Buffer[0] << 4) & 0x100));
			eData.zRelative = 0;

			/* Set header */
			eData.Header.Type = EventInput;
			eData.Header.Length = sizeof(MCorePointerEvent_t);

			/* Send! */
			EmCreateEvent((MCoreProcessEvent_t*)&eData);

			/* Check buttons */
			if (Ps2Dev->Buffer[0] != Ps2Dev->MouseButtons)
			{
				/* Setup */
				bData.Type = MCORE_INPUT_TYPE_MOUSE;
				bData.Data = Ps2Dev->Buffer[0];
				bData.State = 0;

				/* Set headers */
				bData.Header.Type = EventInput;
				bData.Header.Length = sizeof(MCoreButtonEvent_t);

				/* Send */
				EmCreateEvent((MCoreProcessEvent_t*)&bData);
			}

			/* Update */
			Ps2Dev->MouseButtons = Ps2Dev->Buffer[0];

		} break;
	}

	/* Yay */
	return X86_IRQ_HANDLED;
}

/* Setup */
void Ps2MouseInit(int Port)
{
	/* Vars */
	uint8_t Response = 0;

	/* Allocate Data Structure */
	MCoreDevice_t *Device = (MCoreDevice_t*)kmalloc(sizeof(MCoreDevice_t));
	Ps2MouseDevice_t *Ps2Dev = (Ps2MouseDevice_t*)kmalloc(sizeof(Ps2MouseDevice_t));
	
	/* Setup ps2 device struct */
	memset(Ps2Dev, 0, sizeof(Ps2MouseDevice_t));
	Ps2Dev->Port = Port;

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
	Device->IrqHandler = Ps2MouseIrqHandler;

	/* Type */
	Device->Type = DeviceInput;
	Device->Data = NULL;

	/* Initial */
	Device->Driver.Name = (char*)GlbPs2MouseDriverName;
	Device->Driver.Version = 1;
	Device->Driver.Data = Ps2Dev;
	Device->Driver.Status = DriverActive;

	/* Register us for an irq */
	if (DmRequestResource(Device, ResourceIrq)) {
		LogFatal("PS2M", "Failed to allocate irq for use, bailing out!");

		/* Cleanup */
		kfree(Ps2Dev);
		kfree(Device);

		/* Done */
		return;
	}

	/* Create device in upper layer */
	Ps2Dev->Id = DmCreateDevice("Ps2-Mouse", Device);

	/* Set default settings on mouse, 0xF6 */
	if (Port == 2)
		Ps2SendCommand(X86_PS2_CMD_SELECT_PORT2);
	Ps2WriteData(0xF6);

	/* Ack */
	Response = Ps2ReadData(0);

	/* Enable scan, 0xF4 */
	if (Port == 2)
		Ps2SendCommand(X86_PS2_CMD_SELECT_PORT2);
	Ps2WriteData(0xF4);

	/* Ack */
	Response = Ps2ReadData(0);
}