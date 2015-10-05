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
#include <Arch.h>
#include <Drivers\Ps2\Ps2.h>
#include <Drivers\Ps2\Mouse\Ps2Mouse.h>
#include <Heap.h>
#include <stdio.h>
#include <string.h>
#include <InputManager.h>
#include <DeviceManager.h>

/* Ps Mouse Irq */
int Ps2MouseIrqHandler(void *Args)
{
	/* We get 3 bytes describing the state
	 * BUT we can only get one at the time..... -.- */
	ImPointerEvent_t eData;
	ImButtonEvent_t bData;
	Ps2MouseDevice_t *Ps2Dev = (Ps2MouseDevice_t*)Args;

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

			/* Send! */
			InputManagerCreatePointerEvent(&eData);

			/* Check buttons */
			if (Ps2Dev->Buffer[0] != Ps2Dev->MouseButtons)
			{
				/* Setup */
				bData.Type = MCORE_INPUT_TYPE_MOUSE;
				bData.Data = Ps2Dev->Buffer[0];
				bData.State = 0;

				/* Send */
				InputManagerCreateButtonEvent(&bData);
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
	uint8_t Response = 0;

	/* Allocate Data Structure */
	Ps2MouseDevice_t *Ps2Dev = (Ps2MouseDevice_t*)kmalloc(sizeof(Ps2MouseDevice_t));
	memset(Ps2Dev, 0, sizeof(Ps2MouseDevice_t));
	Ps2Dev->Port = Port;

	/* Install Irq */
	if (Port == 1)
		InterruptInstallISA(X86_PS2_PORT1_INTERRUPT, INTERRUPT_PS2_PORT1, Ps2MouseIrqHandler, (void*)Ps2Dev);
	else
		InterruptInstallISA(X86_PS2_PORT2_INTERRUPT, INTERRUPT_PS2_PORT2, Ps2MouseIrqHandler, (void*)Ps2Dev);

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

	/* Create device in upper layer */
	DmCreateDevice("Ps2-Mouse", MCORE_DEVICE_TYPE_INPUT, Ps2Dev);
}