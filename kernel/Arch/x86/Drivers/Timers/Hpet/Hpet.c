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
* MollenOS x86-32 HPET Header
*/

/* Includes */
#include <acpi.h>
#include <Heap.h>
#include <Memory.h>
#include <Timers.h>
#include <LApic.h>
#include <Drivers\Timers\Hpet\Hpet.h>
#include <stdio.h>

/* Globals */
Addr_t GlbHpetBaseAddress = 0;
uint32_t GlbHpetBaseAddressType = 0;
uint32_t GlbHpetMinimumTick = 0;
uint8_t GlbHpetTimerCount = 0;
uint64_t GlbHpetFrequency = 0;
volatile uint64_t GlbHpetCounter = 0;
Hpet_t **GlbHpetTimers = NULL;

/* Externs */
extern void _yield(void);

/* Helpers */
uint32_t HpetRead32(uint32_t Offset)
{
	if (GlbHpetBaseAddressType == ACPI_IO_RANGE)
	{
		uint16_t ioPort = (uint16_t)GlbHpetBaseAddress;
		ioPort += (uint16_t)Offset;

		/* Done */
		return inl(ioPort);
	}
	else
	{
		/* Done */
		return *(volatile Addr_t*)(GlbHpetBaseAddress + (Offset));
	}
}

void HpetWrite32(uint32_t Offset, uint32_t Value)
{
	if (GlbHpetBaseAddressType == ACPI_IO_RANGE)
	{
		uint16_t ioPort = (uint16_t)GlbHpetBaseAddress;
		ioPort += (uint16_t)Offset;

		/* Build response */
		outl(ioPort, Value);
	}
	else
	{
		/* Build response */
		*(volatile Addr_t*)(GlbHpetBaseAddress + (Offset)) = Value;
	}
}

/* Irq Handler */
int HpetTimerHandler(void *Args)
{
	/* Cast */
	Hpet_t *Timer = (Hpet_t*)Args;
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
		timers_apply_time(1);

		/* If we are not periodic restart us */
		if (Timer->Periodic != 1)
		{
			printf("Philip implement retarting of non-peridoic timers please!");
			for (;;);
		}
	}

	/* Done */
	return X86_IRQ_HANDLED;
}

/* Start Comparator */
OsStatus_t HpetComparatorStart(uint32_t Comparator, uint32_t Periodic, uint32_t Freq)
{
	/* Stop main counter */
	uint32_t Now;
	uint64_t Delta;
	uint32_t Itr2;

	/* Disable main counter */
	uint32_t Temp = HpetRead32(X86_HPET_REGISTER_CONFIG);
	Temp &= ~X86_HPET_CONFIG_ENABLED;
	HpetWrite32(X86_HPET_REGISTER_CONFIG, Temp);

	/* Get now */
	Now = HpetRead32(X86_HPET_REGISTER_COUNTER);

	/* We have the hertz of hpet and the fsec */
	Delta = GlbHpetFrequency / Freq;
	Now += (uint32_t)Delta;

#ifdef X86_HPET_DIAGNOSE
	printf("Delta 0x%x, Frequency 0x%x\n", (uint32_t)Delta, (uint32_t)GlbHpetFrequency);
#endif

	/* Find a free interrupt */
	for (Itr2 = 0; Itr2 < 32; Itr2++)
	{
		/* Can we allocate an interrupt for this? */
		if (GlbHpetTimers[Comparator]->Map & (1 << Itr2))
		{
			/* Yes! */
			if (Itr2 > 15)
			{
				/* Allocate PCI Interrupt */
				GlbHpetTimers[Comparator]->Irq = (uint32_t)Itr2;

				/* We want to get the least used irq *
				* of the allowed irq's */
				InterruptInstallShared((uint32_t)Itr2, INTERRUPT_HPET_TIMERS,
					HpetTimerHandler, GlbHpetTimers[Comparator]);

				/* Debug */
#ifdef X86_HPET_DIAGNOSE
				printf("Allocated interrupt %u for timer %u\n", (uint32_t)Itr2, Comparator);
#endif

				/* Go on to next */
				break;
			}
			else
			{
				/* Allocate ISA Interrupt */
				if (InterruptAllocateISA(Itr2) == OS_STATUS_OK)
				{
					/* Save Irq */
					GlbHpetTimers[Comparator]->Irq = (uint32_t)Itr2;

					/* Install Irq */
					InterruptInstallISA((uint32_t)Itr2, INTERRUPT_HPET_TIMERS,
						HpetTimerHandler, GlbHpetTimers[Comparator]);

					/* Debug */
#ifdef X86_HPET_DIAGNOSE
					printf("Allocated interrupt %u for timer %u\n", (uint32_t)Itr2, Comparator);
#endif

					/* Go on to next */
					break;
				}
			}
		}
	}

	/* Sanity */
	if (Itr2 == 32)
	{
		/* Debug */
		printf("Hpet Timer %u has invalid irqmap\n", Comparator);
		return OS_STATUS_FAIL;
	}
		

	/* Update Irq */
	Temp = HpetRead32(X86_HPET_TIMER_REGISTER_CONFIG(Comparator));
#ifdef X86_HPET_DIAGNOSE
	printf("Old TimerInfo: 0x%x\n", Temp);
#endif
	Temp |= ((uint32_t)Itr2 << 9) | X86_HPET_TIMER_CONFIG_IRQENABLED
		 |   X86_HPET_TIMER_CONFIG_SET_CMP_VALUE;

	if (GlbHpetTimers[Comparator]->Irq > 15)
		Temp |= X86_HPET_TIMER_CONFIG_POLARITY;

	if (Periodic == 1)
		Temp |= X86_HPET_TIMER_CONFIG_PERIODIC;

#ifdef X86_HPET_DIAGNOSE
	printf("New TimerInfo: 0x%x\n", Temp);
#endif
	HpetWrite32(X86_HPET_TIMER_REGISTER_CONFIG(Comparator), Temp);
#ifdef X86_HPET_DIAGNOSE
	printf("New TimeEnd: 0x%x\n", Now);
#endif
	HpetWrite32(X86_HPET_TIMER_REGISTER_COMPARATOR(Comparator), Now);

#ifdef X86_HPET_DIAGNOSE
	printf("New Delta: 0x%x\n", (uint32_t)Delta);
#endif

	/*
	 * HPET on AMD 81xx needs a second write (with HPET_TN_SETVAL
	 * cleared) to T0_CMP to set the period. The HPET_TN_SETVAL	
	 * bit is automatically cleared after the first write.
	 * (See AMD-8111 HyperTransport I/O Hub Data Sheet,
	 * Publication # 24674)
	 */
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
	Temp = HpetRead32(X86_HPET_REGISTER_CONFIG);
	Temp |= X86_HPET_CONFIG_ENABLED;
	HpetWrite32(X86_HPET_REGISTER_CONFIG, Temp);

	/* Done */
	return OS_STATUS_OK;
}

/* Setup Comparator */
OsStatus_t HpetComparatorSetup(uint32_t Comparator)
{
	/* Read info about the timer */
	uint32_t TimerInfo = HpetRead32(X86_HPET_TIMER_REGISTER_CONFIG(Comparator));
	uint32_t TimerIrqMap = HpetRead32(X86_HPET_TIMER_REGISTER_CONFIG(Comparator) + 4);

	/* Debug */
#ifdef X86_HPET_DIAGNOSE
	printf("Timer %u, IrqMap 0x%x, Info 0x%x\n", Comparator, TimerIrqMap, TimerInfo);
#endif

	/* Disable Timer */
	TimerInfo &= ~(X86_HPET_TIMER_CONFIG_IRQENABLED | X86_HPET_TIMER_CONFIG_FSBMODE | X86_HPET_TIMER_CONFIG_POLARITY);

	/* Set info */
	GlbHpetTimers[Comparator]->Id = (uint32_t)Comparator;
	GlbHpetTimers[Comparator]->Map = TimerIrqMap;
	GlbHpetTimers[Comparator]->Active = 0;
	GlbHpetTimers[Comparator]->Type = 0;

	if (TimerInfo & X86_HPET_TIMER_CONFIG_PERIODICSUPPORT)
		GlbHpetTimers[Comparator]->Periodic = 1;
	if (TimerInfo & X86_HPET_TIMER_CONFIG_FSBSUPPORT)
		GlbHpetTimers[Comparator]->MsiSupport = 1;

	/* Force timers to 32 bit */
	if (TimerInfo & X86_HPET_TIMER_CONFIG_64BITMODESUPPORT)
		TimerInfo |= X86_HPET_TIMER_CONFIG_32BITMODE;

	HpetWrite32(X86_HPET_TIMER_REGISTER_CONFIG(Comparator), TimerInfo);

#ifdef X86_HPET_DIAGNOSE
	printf("New TimerInfo: 0x%x \n", TimerInfo);
#endif

	return OS_STATUS_OK;
}

/* Setup */
OsStatus_t HpetSetup(void *AcpiTable)
{
	/* Cast to table */
	ACPI_TABLE_HPET *Hpet = (ACPI_TABLE_HPET*)AcpiTable;
	uint8_t Itr = 0;
	volatile uint32_t Temp = 0;
	IntStatus_t IntState;

	/* Sanity */
	if (AcpiTable == NULL)
		return OS_STATUS_FAIL;

	/* Disable Interrupts */
	IntState = InterruptDisable();

#ifdef X86_HPET_DIAGNOSE
	printf("Setting up Hpet\n");
#endif

	/* Save base address */
	GlbHpetBaseAddressType = Hpet->Address.SpaceId;
	GlbHpetBaseAddress = (Addr_t)Hpet->Address.Address;

	/* Save minimum tick */
	GlbHpetMinimumTick = Hpet->MinimumTick;

	/* Reset counter */
	GlbHpetCounter = 0;

#ifdef X86_HPET_DIAGNOSE
	printf("Base Address: 0x%x, Address Type: 0x%x\n",
		GlbHpetBaseAddress, GlbHpetBaseAddressType);
#endif

	/* Map sys memory */
	if (GlbHpetBaseAddressType == 0)
		GlbHpetBaseAddress = (Addr_t)MmVirtualMapSysMemory(GlbHpetBaseAddress, 1);

	/* Get period,  Upper 32 bits */
	volatile uint32_t ClockPeriod = HpetRead32(X86_HPET_REGISTER_CAP_ID + 4);
	
#ifdef X86_HPET_DIAGNOSE
	printf("Base Address: 0x%x, Clock Period: 0x%x, Min Tick: 0x%x\n",
		(uint32_t)GlbHpetBaseAddress, ClockPeriod, GlbHpetMinimumTick);
#endif

	/* AMD SB700 Systems initialise HPET on first register access,
	 * wait for it to setup HPET, its config register reads 0xFFFFFFFF meanwhile */
	for (Itr = 0; Itr < 1000; Itr++)
	{
		/* Read */
		if (HpetRead32(X86_HPET_REGISTER_CONFIG) != 0xFFFFFFFF)
			break;

		/* Sanity */
		if (Itr == 999)
			return OS_STATUS_FAIL;
	}

	/* Sanity */
	if (ClockPeriod > X86_HPET_MAX_PERIOD || ClockPeriod < X86_HPET_MIN_PERIOD)
		return OS_STATUS_FAIL;

	/* Get count of comparators */
	GlbHpetTimerCount = (uint8_t)(((HpetRead32(X86_HPET_REGISTER_CAP_ID) & X86_HPET_CAP_TIMERCOUNT) >> 8) & 0x1F);

	/* Debug */
#ifdef X86_HPET_DIAGNOSE
	printf("Hpet Timer Count: %u\n", (uint32_t)GlbHpetTimerCount);
#endif

	/* Sanity check this */
	if (ClockPeriod > X86_HPET_MAXTICK || ClockPeriod == 0 || GlbHpetTimerCount == 0)
		return OS_STATUS_FAIL;

	/* Allocate */
	GlbHpetTimers = (Hpet_t**)kmalloc(sizeof(Addr_t*) * GlbHpetTimerCount);

	for (Itr = 0; Itr < GlbHpetTimerCount; Itr++)
		GlbHpetTimers[Itr] = (Hpet_t*)kmalloc(sizeof(Hpet_t));

	/* Now all sanity checks are in place, we can configure it */
	/* Step 1: Halt Timer & Disable legacy */
	Temp = HpetRead32(X86_HPET_REGISTER_CONFIG);
#ifdef X86_HPET_DIAGNOSE
	printf("Original Hpet Config 0x%x\n", Temp);
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

	/* Make sure the Hpet is counting */


	/* Setup main system timer 1 ms, this also starts the main counter */
	for (Itr = 0; Itr < GlbHpetTimerCount; Itr++)
	{
		/* Which one supported periodic? */
		if (GlbHpetTimers[Itr]->Periodic == 1)
		{
			GlbHpetTimers[Itr]->Type = 1;
			HpetComparatorStart(Itr, 1, 1000);
			break;
		}
	}

	/* Done */
	return OS_STATUS_OK;
}

/* Pit Ticks */
uint64_t HpetGetClocks(void)
{
	return GlbHpetCounter;
}

/* Sleep for ms */
void HpetSleep(uint32_t MilliSeconds)
{
	/* Calculate TickEnd in NanoSeconds */
	uint64_t TickEnd = MilliSeconds + HpetGetClocks();

	/* While */
	while (TickEnd >= HpetGetClocks())
		_yield();
}

/* Stall for ms */
void HpetStall(uint32_t MilliSeconds)
{
	/* Calculate TickEnd in NanoSeconds */
	uint64_t TickEnd = MilliSeconds + HpetGetClocks();

	/* While */
	while (TickEnd > HpetGetClocks())
		_asm nop;
}