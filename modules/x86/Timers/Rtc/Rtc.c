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
#include <Module.h>
#include <Cmos.h>

/* CLib */
#include <Heap.h>

/* Externs */
extern void ReadTSC(uint64_t *Value);

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

/* The Clock Handler */
int RtcIrqHandler(void *Data)
{
	/* Cast */
	MCoreTimerDevice_t *TimerData = (MCoreTimerDevice_t*)Data;
	RtcTimer_t *Rtc = (RtcTimer_t*)TimerData->TimerData;

	/* Update Peroidic Tick Counter */
	Rtc->NsCounter += Rtc->NsTick;

	/* Apply Timer Time (roughly 1 ms) */
	TimerData->ReportMs(1);

	/* Acknowledge Irq 8 by reading register C */
	CmosReadRegister(X86_CMOS_REGISTER_STATUS_C);

	/* Done! */
	return X86_IRQ_HANDLED;
}

/* Rtc Ticks */
uint64_t RtcGetClocks(void *Data)
{
	/* Cast */
	RtcTimer_t *Rtc = (RtcTimer_t*)Data;

	/* Done */
	return Rtc->NsCounter;
}

/* Stall for ms */
void RtcStallBackup(void *Data, uint32_t MilliSeconds)
{
	/* Vars */
	uint64_t RdTicks = 0;
	uint64_t TickEnd = 0;

	/* We don't use this */
	_CRT_UNUSED(Data);

	/* Read Time Stamp Counter */
	ReadTSC(&RdTicks);

	/* Calculate ticks */
	TickEnd = RdTicks + (MilliSeconds * 100000);

	/* Wait */
	while (TickEnd > RdTicks)
		ReadTSC(&RdTicks);
}

/* Sleep for ms */
void RtcSleep(void *Data, uint32_t MilliSeconds)
{
	/* Calculate TickEnd in NanoSeconds */
	uint64_t TickEnd = (MilliSeconds * 1000) + RtcGetClocks(Data);

	/* If glb_clock_tick is 0, RTC failure */
	if (RtcGetClocks(Data) == 0)
	{
		RtcStallBackup(Data, MilliSeconds);
		return;
	}

	/* While */
	while (TickEnd >= RtcGetClocks(Data))
		_ThreadYield();
}

/* Stall for ms */
void RtcStall(void *Data, uint32_t MilliSeconds)
{
	/* Calculate TickEnd in NanoSeconds */
	uint64_t TickEnd = (MilliSeconds * 1000) + RtcGetClocks(Data);

	/* If glb_clock_tick is 0, RTC failure */
	if (RtcGetClocks(Data) == 0)
	{
		RtcStallBackup(Data, MilliSeconds);
		return;
	}

	/* While */
	while (TickEnd >= RtcGetClocks(Data))
		_asm nop;
}

/* Entry point of a module */
MODULES_API void ModuleInit(Addr_t *FunctionTable, void *Data)
{
	IntStatus_t IntrState;
	uint8_t StateB = 0;
	uint8_t Rate = 0x08; /* must be between 3 and 15 */
	MCoreTimerDevice_t *TimerData = NULL;
	RtcTimer_t *Rtc = NULL;

	/* Save Table */
	_CRT_UNUSED(Data);
	GlbFunctionTable = FunctionTable;

	/* Allocate */
	Rtc = (RtcTimer_t*)kmalloc(sizeof(RtcTimer_t));
	TimerData = (MCoreTimerDevice_t*)kmalloc(sizeof(MCoreTimerDevice_t));

	/* Ms is .97, 1024 ints per sec */
	/* Frequency = 32768 >> (rate-1), 15 = 2, 14 = 4, 13 = 8/s (125 ms) */
	Rtc->NsTick = 976;
	Rtc->NsCounter = 0;
	Rtc->AlarmTicks = 0;

	/* Setup Timer Data */
	TimerData->TimerData = Rtc;
	TimerData->Stall = RtcStall;
	TimerData->Sleep = RtcSleep;
	TimerData->GetTicks = RtcGetClocks;

	/* Disable IRQ's for this duration */
	IntrState = InterruptDisable();

	/* Disable RTC Irq */
	StateB = CmosReadRegister(X86_CMOS_REGISTER_STATUS_B);
	StateB &= ~(0x70);
	CmosWriteRegister(X86_CMOS_REGISTER_STATUS_B, StateB);
	
	/* Update state_b */
	StateB = CmosReadRegister(X86_CMOS_REGISTER_STATUS_B);

	/* Install ISA IRQ Handler using normal install function */
	InterruptInstallISA(X86_CMOS_RTC_IRQ, INTERRUPT_RTC, RtcIrqHandler, TimerData);

	/* Register us with OS so we can get our function interface */
	Rtc->DeviceId = DmCreateDevice("Rtc Timer", DeviceTimer, TimerData);

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