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
* MollenOS X86-32 RTC (Timer) Driver
* http://wiki.osdev.org/RTC
*/

/* Includes */
#include <DeviceManager.h>
#include <Devices/Timer.h>
#include <Timers.h>
#include <Module.h>
#include <Cmos.h>
#include <Heap.h>

/* CLib */
#include <string.h>

/* Structures */
#pragma pack(push, 1)
typedef struct _RtcTimer
{
	/* Device Id */
	DevId_t DeviceId;

	/* Total Tick Count */
	uint64_t NsCounter;

	/* Alarm Tick Counter */
	uint32_t AlarmTicks;

	/* Step Value */
	uint32_t NsTick;

} RtcTimer_t;
#pragma pack(pop)

/* Globals */
const char *GlbRtcDriverName = "MollenOS RTC Driver";

/* The Clock Handler */
int RtcIrqHandler(void *DevData)
{
	/* Cast */
	MCoreDevice_t *tDevice = (MCoreDevice_t*)DevData;
	MCoreTimerDevice_t *Timer = (MCoreTimerDevice_t*)tDevice->Data;
	RtcTimer_t *Rtc = (RtcTimer_t*)tDevice->Driver.Data;

	/* Update Peroidic Tick Counter */
	Rtc->NsCounter += Rtc->NsTick;

	/* Apply Timer Time (roughly 1 ms) */
	if (Timer->ReportMs != NULL)
		Timer->ReportMs(1);

	/* Acknowledge Irq 8 by reading register C */
	CmosReadRegister(X86_CMOS_REGISTER_STATUS_C);

	/* Done! */
	return X86_IRQ_HANDLED;
}

/* Rtc Ticks */
uint64_t RtcGetClocks(void *Device)
{
	/* Cast */
	MCoreDevice_t *tDevice = (MCoreDevice_t*)Device;
	RtcTimer_t *Rtc = (RtcTimer_t*)tDevice->Driver.Data;

	/* Done */
	return Rtc->NsCounter;
}

/* Sleep for ms */
void RtcSleep(void *Device, size_t MilliSeconds)
{
	/* Calculate TickEnd in NanoSeconds */
	uint64_t TickEnd = (MilliSeconds * 1000) + RtcGetClocks(Device);

	/* If glb_clock_tick is 0, RTC failure */
	if (RtcGetClocks(Device) == 0)
	{
		DelayMs(MilliSeconds);
		return;
	}

	/* While */
	while (TickEnd >= RtcGetClocks(Device))
		IThreadYield();
}

/* Stall for ms */
void RtcStall(void *Device, size_t MilliSeconds)
{
	/* Calculate TickEnd in NanoSeconds */
	uint64_t TickEnd = (MilliSeconds * 1000) + RtcGetClocks(Device);

	/* If glb_clock_tick is 0, RTC failure */
	if (RtcGetClocks(Device) == 0)
	{
		DelayMs(MilliSeconds);
		return;
	}

	/* While */
	while (TickEnd >= RtcGetClocks(Device))
		_asm nop;
}

/* Entry point of a module */
MODULES_API void ModuleInit(void *Data)
{
	/* Vars */
	IntStatus_t IntrState;
	uint8_t StateB = 0;
	uint8_t Rate = 0x08; /* must be between 3 and 15 */
	
	/* Data pointers */
	MCoreDevice_t *Device = NULL;
	MCoreTimerDevice_t *TimerData = NULL;
	RtcTimer_t *Rtc = NULL;

	/* Unused */
	_CRT_UNUSED(Data);

	/* Allocate */
	Device = (MCoreDevice_t*)kmalloc(sizeof(MCoreDevice_t));
	Rtc = (RtcTimer_t*)kmalloc(sizeof(RtcTimer_t));
	TimerData = (MCoreTimerDevice_t*)kmalloc(sizeof(MCoreTimerDevice_t));

	/* Ms is .97, 1024 ints per sec */
	/* Frequency = 32768 >> (rate-1), 15 = 2, 14 = 4, 13 = 8/s (125 ms) */
	Rtc->NsTick = 976;
	Rtc->NsCounter = 0;
	Rtc->AlarmTicks = 0;

	/* Setup Timer Data */
	TimerData->Stall = RtcStall;
	TimerData->Sleep = RtcSleep;
	TimerData->GetTicks = RtcGetClocks;

	/* Setup device */
	memset(Device, 0, sizeof(MCoreDevice_t));

	/* Setup information */
	Device->VendorId = 0x8086;
	Device->DeviceId = 0x0;
	Device->Class = DEVICEMANAGER_LEGACY_CLASS;
	Device->Subclass = 0x00000020;

	Device->IrqLine = X86_CMOS_RTC_IRQ;
	Device->IrqPin = -1;
	Device->IrqAvailable[0] = -1;
	Device->IrqHandler = RtcIrqHandler;

	/* Type */
	Device->Type = DeviceTimer;
	Device->Data = TimerData;

	/* Initial */
	Device->Driver.Name = (char*)GlbRtcDriverName;
	Device->Driver.Version = 1;
	Device->Driver.Data = Rtc;
	Device->Driver.Status = DriverActive;

	/* Register us for an irq */
	if (DmRequestResource(Device, ResourceIrq)) {
		LogFatal("RTC0", "Failed to allocate irq for use, bailing out!");

		/* Cleanup */
		kfree(TimerData);
		kfree(Rtc);
		kfree(Device);

		/* Done */
		return;
	}

	/* Register us with OS so we can get our function interface */
	Rtc->DeviceId = DmCreateDevice("Rtc Timer", Device);

	/* Disable IRQ's for this duration */
	IntrState = InterruptDisable();

	/* Disable RTC Irq */
	StateB = CmosReadRegister(X86_CMOS_REGISTER_STATUS_B);
	StateB &= ~(0x70);
	CmosWriteRegister(X86_CMOS_REGISTER_STATUS_B, StateB);
	
	/* Update state_b */
	StateB = CmosReadRegister(X86_CMOS_REGISTER_STATUS_B);

	/* Set Frequency */
	CmosWriteRegister(X86_CMOS_REGISTER_STATUS_A, 0x20 | Rate);

	/* Clear pending interrupt */
	CmosReadRegister(X86_CMOS_REGISTER_STATUS_C);

	/* Enable Periodic Interrupts */
	StateB = CmosReadRegister(X86_CMOS_REGISTER_STATUS_B);
	StateB |= X86_CMOSB_RTC_PERIODIC;
	CmosWriteRegister(X86_CMOS_REGISTER_STATUS_B, StateB);

	/* Done, reenable interrupts */
	InterruptRestoreState(IntrState);

	/* Clear pending interrupt again */
	CmosReadRegister(X86_CMOS_REGISTER_STATUS_C);
}