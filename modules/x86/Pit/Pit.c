/* MollenOS
*
* Copyright 2011 - 2015, Philip Meulengracht
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
* MollenOS x86-32 Pit Header
*/

/* Includes */
#include <DeviceManager.h>
#include <Devices\Timer.h>
#include <Module.h>
#include "Pit.h"
#include <Heap.h>
#include <Log.h>

/* CLib */
#include <string.h>

/* Structures */
#pragma pack(push, 1)
typedef struct _PitTimer
{
	/* Device Id */
	DevId_t DeviceId;

	/* Io-space for accessing the PIT
	 * Spans over 4 bytes from 0x40-0x44 */
	DeviceIoSpace_t *IoSpace;

	/* Divisor Value */
	uint32_t Divisor;

	/* Timer Count */
	uint64_t PitCounter;

} PitTimer_t;
#pragma pack(pop)

/* Globals */
const char *GlbPitDriverName = "MollenOS PIT Driver";

/* The Pit Handler */
int PitIrqHandler(void *Data)
{
	/* Cast */
	MCoreDevice_t *tDevice = (MCoreDevice_t*)Data;
	MCoreTimerDevice_t *Timer = (MCoreTimerDevice_t*)tDevice->Data;
	PitTimer_t *Pit = (PitTimer_t*)tDevice->Driver.Data;

	/* Update Peroidic Tick Counter */
	Pit->PitCounter++;

	/* Apply Timer Time (roughly 1 ms) */
	if (Timer->ReportMs != NULL)
		Timer->ReportMs(1);

	/* Done */
	return X86_IRQ_HANDLED;
}

/* Entry point of a module */
MODULES_API void ModuleInit(void *Data)
{
	/* We need these */
	MCoreDevice_t *Device = NULL;
	MCoreTimerDevice_t *Timer = NULL;
	PitTimer_t *Pit = NULL;

	/* We want a frequncy of 1000 hz */
	uint32_t Divisor = (1193181 / 1000);
	IntStatus_t IntrState;

	/* Unused */
	_CRT_UNUSED(Data);

	/* Allocate */
	Device = (MCoreDevice_t*)kmalloc(sizeof(MCoreDevice_t));
	Pit = (PitTimer_t*)kmalloc(sizeof(PitTimer_t));
	Timer = (MCoreTimerDevice_t*)kmalloc(sizeof(MCoreTimerDevice_t));

	/* Setup Pit object */
	Pit->PitCounter = 0;
	Pit->Divisor = Divisor;

	/* Setup io-space */
	Pit->IoSpace = IoSpaceCreate(DEVICE_IO_SPACE_IO, X86_PIT_IO_BASE, 4);

	/* Setup Timer */
	Timer->ReportMs = NULL;
	Timer->Sleep = PitSleep;
	Timer->Stall = PitStall;
	Timer->GetTicks = PitGetClocks;

	/* Setup device */
	memset(Device, 0, sizeof(MCoreDevice_t));

	/* Setup information */
	Device->VendorId = 0x8086;
	Device->DeviceId = 0x0;
	Device->Class = DEVICEMANAGER_LEGACY_CLASS; 
	Device->Subclass = 0x00000018;

	Device->IrqLine = X86_PIT_IRQ;
	Device->IrqPin = -1;
	Device->IrqAvailable[0] = -1;
	Device->IrqHandler = PitIrqHandler;

	/* Type */
	Device->Type = DeviceTimer;
	Device->Data = Timer;

	/* Initial */
	Device->Driver.Name = (char*)GlbPitDriverName;
	Device->Driver.Version = 1;
	Device->Driver.Data = Pit;
	Device->Driver.Status = DriverActive;

	/* Register us for an irq */
	if (DmRequestResource(Device, ResourceIrq)) {
		LogFatal("PIT0", "Failed to allocate irq for use, bailing out!");

		/* Cleanup */
		kfree(Timer);
		kfree(Pit);
		kfree(Device);

		/* Done */
		return;
	}

	/* Before enabling, register us */
	Pit->DeviceId = DmCreateDevice("PIT Timer", Device);

	/* Disable IRQ's for this duration */
	IntrState = InterruptDisable();

	/* We use counter 0, select counter 0 and configure it */
	IoSpaceWrite(Pit->IoSpace, X86_PIT_REGISTER_COMMAND, 
		X86_PIT_COMMAND_SQUAREWAVEGEN | X86_PIT_COMMAND_RL_DATA |
		X86_PIT_COMMAND_COUNTER_0, 1);

	/* Set divisor */
	IoSpaceWrite(Pit->IoSpace, X86_PIT_REGISTER_COUNTER0, (uint8_t)(Divisor & 0xFF), 1);
	IoSpaceWrite(Pit->IoSpace, X86_PIT_REGISTER_COUNTER0, (uint8_t)((Divisor >> 8) & 0xFF), 1);

	/* Done, reenable interrupts */
	InterruptRestoreState(IntrState);
}

/* Pit Ticks */
uint64_t PitGetClocks(void *Device)
{
	/* Cast */
	MCoreDevice_t *tDevice = (MCoreDevice_t*)Device;
	PitTimer_t *Pit = (PitTimer_t*)tDevice->Driver.Data;

	/* Return Val */
	return Pit->PitCounter;
}

/* Sleep for ms */
void PitSleep(void *Device, size_t MilliSeconds)
{
	/* Calculate TickEnd in NanoSeconds */
	uint64_t TickEnd = MilliSeconds + PitGetClocks(Device);

	/* While */
	while (TickEnd > PitGetClocks(Device))
		IThreadYield();
}

/* Stall for ms */
void PitStall(void *Device, size_t MilliSeconds)
{
	/* Calculate TickEnd in NanoSeconds */
	uint64_t TickEnd = MilliSeconds + PitGetClocks(Device);

	/* While */
	while (TickEnd > PitGetClocks(Device))
		_asm nop;
}