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
* MollenOS ACPI - HPET Driver
*/

/* Includes */
#include <acpi.h>
#include <Module.h>
#include <DeviceManager.h>
#include <Devices\Timer.h>
#include <Timers.h>
#include "Hpet.h"
#include <Heap.h>
#include <Log.h>

/* Structures */
#pragma pack(push, 1)
typedef struct _HpetManager
{

	/* List of timers */
	Hpet_t **HpetTimers;

} HpetManager_t;
#pragma pack(pop)

/* Globals */
DeviceIoSpace_t *GlbHpetBaseAddress = NULL;
uint32_t GlbHpetMinimumTick = 0;
uint8_t GlbHpetTimerCount = 0;
uint64_t GlbHpetFrequency = 0;
volatile uint64_t GlbHpetCounter = 0;
Hpet_t **GlbHpetTimers = NULL;
const char *GlbHpetDriverName = "MollenOS ACPI HPET Driver";

/* Helpers */

/* Read from Hpet */
uint32_t HpetRead32(uint32_t Offset)
{
	/* Deep Call */
	return IoSpaceRead(GlbHpetBaseAddress, Offset, 4);
}

/* Write to Hpet */
void HpetWrite32(uint32_t Offset, uint32_t Value)
{
	/* Deep Call */
	IoSpaceWrite(GlbHpetBaseAddress, Offset, Value, 4);
}

/* Stop main counter */
void HpetStop(void)
{
	/* Disable main counter */
	uint32_t Temp = HpetRead32(X86_HPET_REGISTER_CONFIG);
	Temp &= ~X86_HPET_CONFIG_ENABLED;
	HpetWrite32(X86_HPET_REGISTER_CONFIG, Temp);
}

/* Start main counter */
void HpetStart(void)
{
	uint32_t Temp = HpetRead32(X86_HPET_REGISTER_CONFIG);
	Temp |= X86_HPET_CONFIG_ENABLED;
	HpetWrite32(X86_HPET_REGISTER_CONFIG, Temp);
}

/* Irq Handler */
int HpetTimerHandler(void *Args)
{
	/* Cast */
	MCoreDevice_t *DevTimer = (MCoreDevice_t*)Args;
	MCoreTimerDevice_t *pTimer = (MCoreTimerDevice_t*)DevTimer->Data;
	Hpet_t *Timer = (Hpet_t*)DevTimer->Driver.Data;

	/* Vars */
	uint32_t TimerBit = (1 << Timer->Id);

	/* Did we even fire? (Shared Only) */
	if (Timer->Irq > 15)
	{
		if (!(HpetRead32(X86_HPET_REGISTER_INTR) & TimerBit))
			return X86_IRQ_NOT_HANDLED;
		else
			HpetWrite32(X86_HPET_REGISTER_INTR, TimerBit);
	}

	/* If timer is not in periodic mode
	* and is meant to be periodic, restart */
	if (Timer->Type == 1)
	{
		/* Inc Counter */
		GlbHpetCounter++;

		/* Apply time (1ms) */
		if (pTimer->ReportMs != NULL)
			pTimer->ReportMs(1);

		/* If we are not periodic restart us */
		if (Timer->Periodic != 1)
		{
			LogFatal("HPET", "Philip implement retarting of non-peridoic timers please!");
			for (;;);
		}
	}

	/* Done */
	return X86_IRQ_HANDLED;
}

/* Start Comparator */
OsStatus_t HpetComparatorStart(uint32_t Comparator, uint32_t Periodic, uint32_t Freq, MCoreDevice_t *Device)
{
	/* Stop main counter */
	uint32_t Now;
	uint64_t Delta;
	uint32_t Temp;

	/* Disable main counter */
	HpetStop();

	/* Get now */
	Now = HpetRead32(X86_HPET_REGISTER_COUNTER);

	/* We have the hertz of hpet and the fsec */
	Delta = GlbHpetFrequency / Freq;
	Now += (uint32_t)Delta;

#ifdef X86_HPET_DIAGNOSE
	LogInformation("HPET", "Delta 0x%x, Frequency 0x%x", (uint32_t)Delta, (uint32_t)GlbHpetFrequency);
#endif

	/* Sanity */
	if (GlbHpetTimers[Comparator]->Irq == 0xFFFFFFFF) {
		/* Irq has not been allocated yet 
		 * Get device interface and allocate one */
		int IrqItr = 0, DevIrqItr = 0;
		
		/* Get a list of avail irqs */
		for (IrqItr = 0; IrqItr < 32; IrqItr++)
		{
			/* Can we allocate an interrupt for this? */
			if (GlbHpetTimers[Comparator]->Map & (1 << IrqItr))
				Device->IrqAvailable[DevIrqItr++] = IrqItr;

			/* Do we have enough? */
			if (DevIrqItr == DEVICEMANAGER_MAX_IRQS)
				break;
		}

		/* End of list */
		if (DevIrqItr != DEVICEMANAGER_MAX_IRQS)
			Device->IrqAvailable[DevIrqItr] = -1;

		Device->IrqHandler = HpetTimerHandler;

		/* Register us for an irq */
		if (DmRequestResource(Device, ResourceIrq)) {
			LogFatal("HPET", "Failed to allocate irq for use, bailing out!");

			/* Done */
			return OsError;
		}

		/* Update */
		GlbHpetTimers[Comparator]->Irq = Device->IrqLine;
	}

	/* Update Irq */
	Temp = HpetRead32(X86_HPET_TIMER_REGISTER_CONFIG(Comparator));
#ifdef X86_HPET_DIAGNOSE
	LogInformation("HPET", "Old TimerInfo: 0x%x", Temp);
#endif
	Temp |= (GlbHpetTimers[Comparator]->Irq << 9) | X86_HPET_TIMER_CONFIG_IRQENABLED
		 | X86_HPET_TIMER_CONFIG_SET_CMP_VALUE;

	if (GlbHpetTimers[Comparator]->Irq > 15)
		Temp |= X86_HPET_TIMER_CONFIG_POLARITY;

	if (Periodic == 1)
		Temp |= X86_HPET_TIMER_CONFIG_PERIODIC;

#ifdef X86_HPET_DIAGNOSE
	LogInformation("HPET", "New TimerInfo: 0x%x", Temp);
#endif
	HpetWrite32(X86_HPET_TIMER_REGISTER_CONFIG(Comparator), Temp);
#ifdef X86_HPET_DIAGNOSE
	LogInformation("HPET", "New TimeEnd: 0x%x", Now);
#endif
	HpetWrite32(X86_HPET_TIMER_REGISTER_COMPARATOR(Comparator), Now);

#ifdef X86_HPET_DIAGNOSE
	LogInformation("HPET", "New Delta: 0x%x", (uint32_t)Delta);
#endif

	/*
	 * HPET on AMD 81xx needs a second write (with HPET_TN_SETVAL
	 * cleared) to T0_CMP to set the period. The HPET_TN_SETVAL	
	 * bit is automatically cleared after the first write.
	 * (See AMD-8111 HyperTransport I/O Hub Data Sheet,
	 * Publication # 24674)
	 */
	if (Periodic)
		HpetWrite32(X86_HPET_TIMER_REGISTER_COMPARATOR(Comparator), (uint32_t)Delta);

	/* If this is not the periodic shitcake then make sure that 
	 * hpet has not already */
	if (Periodic != 1)
	{
		uint32_t CurrTime = HpetRead32(X86_HPET_REGISTER_COUNTER);

		if (CurrTime > Now)
		{
			/* Make sure callback is fired */
		}
	}

	/* Set as active */
	GlbHpetTimers[Comparator]->Active = 1;

	/* Clear */
	HpetWrite32(X86_HPET_REGISTER_INTR, (1 << Comparator));

	/* Start main counter */
	HpetStart();

	/* Done */
	return OsNoError;
}

/* Setup Comparator */
OsStatus_t HpetComparatorSetup(uint32_t Comparator)
{
	/* Read info about the timer */
	uint32_t TimerInfo = HpetRead32(X86_HPET_TIMER_REGISTER_CONFIG(Comparator));
	uint32_t TimerIrqMap = HpetRead32(X86_HPET_TIMER_REGISTER_CONFIG(Comparator) + 4);

	/* Debug */
#ifdef X86_HPET_DIAGNOSE
	LogInformation("HPET", "Timer %u, IrqMap 0x%x, Info 0x%x", Comparator, TimerIrqMap, TimerInfo);
#endif

	/* Disable Timer */
	TimerInfo &= ~(X86_HPET_TIMER_CONFIG_IRQENABLED | X86_HPET_TIMER_CONFIG_FSBMODE | X86_HPET_TIMER_CONFIG_POLARITY);

	/* Set info */
	GlbHpetTimers[Comparator]->Id = (uint32_t)Comparator;
	GlbHpetTimers[Comparator]->Map = TimerIrqMap;
	GlbHpetTimers[Comparator]->Active = 0;
	GlbHpetTimers[Comparator]->Type = 0;
	GlbHpetTimers[Comparator]->Irq = 0xFFFFFFFF;

	if (TimerInfo & X86_HPET_TIMER_CONFIG_PERIODICSUPPORT)
		GlbHpetTimers[Comparator]->Periodic = 1;
	if (TimerInfo & X86_HPET_TIMER_CONFIG_FSBSUPPORT)
		GlbHpetTimers[Comparator]->MsiSupport = 1;

	/* Force timers to 32 bit */
	if (TimerInfo & X86_HPET_TIMER_CONFIG_64BITMODESUPPORT)
		TimerInfo |= X86_HPET_TIMER_CONFIG_32BITMODE;

	HpetWrite32(X86_HPET_TIMER_REGISTER_CONFIG(Comparator), TimerInfo);

#ifdef X86_HPET_DIAGNOSE
	LogInformation("HPET", "New TimerInfo: 0x%x", TimerInfo);
#endif

	return OsNoError;
}

/* Entry point of a module */
MODULES_API void ModuleInit(void *Data)
{
	/* We need these */
	ACPI_TABLE_HPET *Hpet = (ACPI_TABLE_HPET*)Data;
	uint8_t Itr = 0;
	volatile uint32_t Temp = 0;
	IntStatus_t IntState;

	/* Sanity */
	if (Data == NULL)
		return;

	/* Disable Interrupts */
	IntState = InterruptDisable();

#ifdef X86_HPET_DIAGNOSE
	LogInformation("HPET", "Setting up Hpet");
#endif

	/* Save base address */
	if (Hpet->Address.SpaceId == 0)
		GlbHpetBaseAddress = IoSpaceCreate(DEVICE_IO_SPACE_MMIO, (Addr_t)Hpet->Address.Address, PAGE_SIZE);
	else
		GlbHpetBaseAddress = IoSpaceCreate(DEVICE_IO_SPACE_IO, (Addr_t)Hpet->Address.Address, 512);

	/* Save minimum tick */
	GlbHpetMinimumTick = Hpet->MinimumTick;

	/* Reset counter */
	GlbHpetCounter = 0;

#ifdef X86_HPET_DIAGNOSE
	LogInformation("HPET", "Base Address: 0x%x, Address Type: 0x%x",
		GlbHpetBaseAddress, GlbHpetBaseAddressType);
#endif

	/* Get period,  Upper 32 bits */
	volatile uint32_t ClockPeriod = HpetRead32(X86_HPET_REGISTER_CAP_ID + 4);

#ifdef X86_HPET_DIAGNOSE
	LogInformation("HPET", "Base Address: 0x%x, Clock Period: 0x%x, Min Tick: 0x%x",
		(uint32_t)GlbHpetBaseAddress, ClockPeriod, GlbHpetMinimumTick);
#endif

	/* AMD SB700 Systems initialise HPET on first register access,
	* wait for it to setup HPET, its config register reads 0xFFFFFFFF meanwhile */
	for (Itr = 0; Itr < 10000; Itr++)
	{
		/* Read */
		if (HpetRead32(X86_HPET_REGISTER_CONFIG) != 0xFFFFFFFF)
			break;

		/* Sanity */
		if (Itr == 9999)
			return;
	}

	/* Sanity */
	if (ClockPeriod > X86_HPET_MAX_PERIOD || ClockPeriod < X86_HPET_MIN_PERIOD)
		return;

	/* Get count of comparators */
	GlbHpetTimerCount = (uint8_t)(((HpetRead32(X86_HPET_REGISTER_CAP_ID) & X86_HPET_CAP_TIMERCOUNT) >> 8) & 0x1F);

	/* Debug */
#ifdef X86_HPET_DIAGNOSE
	LogInformation("HPET", "Hpet Timer Count: %u", (uint32_t)GlbHpetTimerCount);
#endif

	/* Sanity check this */
	if (ClockPeriod > X86_HPET_MAXTICK || ClockPeriod == 0 || GlbHpetTimerCount == 0)
		return;

	/* Allocate */
	GlbHpetTimers = (Hpet_t**)kmalloc(sizeof(Addr_t*) * GlbHpetTimerCount);

	for (Itr = 0; Itr < GlbHpetTimerCount; Itr++)
		GlbHpetTimers[Itr] = (Hpet_t*)kmalloc(sizeof(Hpet_t));

	/* Now all sanity checks are in place, we can configure it */
	/* Step 1: Halt Timer & Disable legacy */
	Temp = HpetRead32(X86_HPET_REGISTER_CONFIG);
#ifdef X86_HPET_DIAGNOSE
	LogInformation("HPET", "Original Hpet Config 0x%x", Temp);
#endif
	Temp &= ~(X86_HPET_CONFIG_ENABLED);
	HpetWrite32(X86_HPET_REGISTER_CONFIG, Temp);

	/* Reset timer */
	HpetWrite32(X86_HPET_REGISTER_COUNTER, 0);
	HpetWrite32(X86_HPET_REGISTER_COUNTER + 4, 0);

	/* Step 2: Calculate HPET Frequency */
	uint64_t Frequency = FSEC_PER_SEC / ClockPeriod;
	GlbHpetFrequency = Frequency;

	/* Initialize Comparators */
	for (Itr = 0; Itr < GlbHpetTimerCount; Itr++)
		HpetComparatorSetup(Itr);

	/* Enable Interrupts before initializing the periodic */
	InterruptRestoreState(IntState);

	/* Setup main system timer 1 ms, this also starts the main counter */
	uint32_t PeriodicInstalled = 0;
	for (Itr = 0; Itr < GlbHpetTimerCount; Itr++)
	{
		/* Which one supported periodic? */
		if (GlbHpetTimers[Itr]->Periodic == 1
			&& !PeriodicInstalled)
		{
			/* Setup device */
			MCoreDevice_t *pDevice = (MCoreDevice_t*)kmalloc(sizeof(MCoreDevice_t));
			MCoreTimerDevice_t *pTimer = (MCoreTimerDevice_t*)kmalloc(sizeof(MCoreTimerDevice_t));
			memset(pDevice, 0, sizeof(MCoreDevice_t));

			/* Setup information */
			pDevice->VendorId = 0x8086;
			pDevice->DeviceId = 0x0;
			pDevice->Class = DEVICEMANAGER_ACPI_CLASS;
			pDevice->Subclass = 0x00000008;

			/* Setup Irq's */
			pDevice->IrqLine = -1;
			pDevice->IrqPin = -1;
			
			/* Type */
			pDevice->Type = DeviceTimer;
			pDevice->Data = pTimer;

			/* Initial */
			pDevice->Driver.Name = (char*)GlbHpetDriverName;
			pDevice->Driver.Version = 1;
			pDevice->Driver.Data = GlbHpetTimers[Itr];
			pDevice->Driver.Status = DriverActive;

			/* Setup timer */
			pTimer->Sleep = HpetSleep;
			pTimer->Stall = HpetStall;
			pTimer->GetTicks = HpetGetClocks;

			/* Set type */
			GlbHpetTimers[Itr]->Type = 1;
			GlbHpetTimers[Itr]->DeviceId = DmCreateDevice("HPet Timer", pDevice);

			/* Start it */
			HpetComparatorStart(Itr, 1, 1000, pDevice);

			/* Done! */
			PeriodicInstalled = 1;
		}
		else
		{
			/* Create Perf */
		}
	}
}

/* Pit Ticks */
uint64_t HpetGetClocks(void* Device)
{
	/* Not used */
	_CRT_UNUSED(Device);

	/* Return global counter 
	 * since there is only one HPET-periodic */
	return GlbHpetCounter;
}

/* Sleep for ms */
void HpetSleep(void* Device, size_t MilliSeconds)
{
	/* Calculate TickEnd in mSeconds */
	uint64_t TickEnd = MilliSeconds + HpetGetClocks(Device);

	/* While */
	while (TickEnd > HpetGetClocks(Device))
		IThreadYield();
}

/* Stall for ms */
void HpetStall(void* Device, size_t MilliSeconds)
{
	/* Calculate TickEnd in mSeconds */
	uint64_t TickEnd = MilliSeconds + HpetGetClocks(Device);

	/* While */
	while (TickEnd > HpetGetClocks(Device))
		_asm nop;
}