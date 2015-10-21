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
* MollenOS x86-32 Advanced Programmable Interrupt Controller Driver
* Helper Functions
*/

/* Includes */
#include <List.h>
#include <Apic.h>
#include <Acpi.h>
#include <Interrupts.h>
#include <stdio.h>
#include <string.h>

/* Drivers */
#include <SysTimers.h>

/* Globals */
volatile uint32_t GlbTimerTicks[64];
uint8_t GlbBootstrapCpuId = 0;
uint32_t GlbTimerQuantum = 0;

/* Externs */
extern x86CpuObject_t GlbBootCpuInfo;
extern list_t *GlbAcpiNodes;

/* Handlers */
extern int ApicErrorHandler(void *Args);
extern int ApicSpuriousHandler(void *Args);
extern int ApicTimerHandler(void *Args);

/* Setup LVT */
void ApicSetupLvt(Cpu_t Cpu, int Lvt)
{
	/* Vars */
	list_node_t *Node;
	uint32_t Temp = 0;

	/* Iterate */
	_foreach(Node, GlbAcpiNodes)
	{
		if (Node->identifier == ACPI_MADT_TYPE_LOCAL_APIC_NMI)
		{
			/* Cast */
			ACPI_MADT_LOCAL_APIC_NMI *ApicNmi =
				(ACPI_MADT_LOCAL_APIC_NMI*)Node->data;

			/* Is it for us? */
			if (ApicNmi->ProcessorId == 0xFF
				|| ApicNmi->ProcessorId == Cpu)
			{
				/* Yay */
				if (ApicNmi->Lint == Lvt)
				{
					/* Set */
					Temp = APIC_NMI_ROUTE;
					Temp |= (InterruptGetPolarity(ApicNmi->IntiFlags, 0) << 13);
					Temp |= (InterruptGetTrigger(ApicNmi->IntiFlags, 0) << 15);

					/* Done */
					break;
				}
			}
		}
	}

	/* Sanity - LVT 0 Default */
	if (Temp == 0
		&& Lvt == 0)
	{
		/* Lets see */
		Temp = APIC_EXTINT_ROUTE;
	}

	/* Sanity - LVT 1 Default */
	if (Temp == 0
		&& Lvt == 1)
	{
		/* Setup to NMI */
		Temp = APIC_NMI_ROUTE;

		/* Set level triggered */
		if (!ApicIsIntegrated()) 
			Temp |= 0x8000;
	}

	/* Sanity, only BP gets LVT */
	if (Cpu != GlbBootstrapCpuId)
		Temp |= APIC_MASKED;

	/* Write */
	if (Lvt == 1)
		ApicWriteLocal(APIC_LINT1_REGISTER, Temp);
	else
		ApicWriteLocal(APIC_LINT0_REGISTER, Temp);
}

/* Shared Apic Init */
void ApicInitialSetup(Cpu_t Cpu)
{
	uint32_t Temp = 0;
	int i = 0, j = 0;

	/* Disable ESR */
#ifdef _X86_32
	if (ApicIsIntegrated())
	{
		ApicWriteLocal(APIC_ESR, 0);
		ApicWriteLocal(APIC_ESR, 0);
		ApicWriteLocal(APIC_ESR, 0);
		ApicWriteLocal(APIC_ESR, 0);
	}
#endif

	/* Set perf monitor to NMI */
	ApicWriteLocal(APIC_PERF_MONITOR, APIC_NMI_ROUTE);

	/* Set destination format register to flat model */
	ApicWriteLocal(APIC_DEST_FORMAT, 0xFFFFFFFF);

	/* Set our cpu id */
	ApicWriteLocal(APIC_LOGICAL_DEST, (ApicGetCpuMask(Cpu) << 24));

	/* Set initial task priority to accept all */
	ApicSetTaskPriority(0);

	/* Clear interrupt registers ISR, IRR */
	for (i = 8 - 1; i >= 0; i--)
	{
		Temp = ApicReadLocal(0x100 + i * 0x10);
		for (j = 31; j >= 0; j--)
		{
			if (Temp & (1 << j))
				ApicSendEoi(0, 0);
		}
	}
}

/* Shared Apic Init ESR */
void ApicSetupESR(void)
{
	int MaxLvt = 0;
	uint32_t Temp = 0;

	/* Sanity */
	if (!ApicIsIntegrated())
		return;

	/* Setup ESR */
	MaxLvt = ApicGetMaxLvt();

	/* Sanity */
	if (MaxLvt > 3)
		ApicWriteLocal(APIC_ESR, 0);
	Temp = ApicReadLocal(APIC_ESR);

	/* Enable sending errors */
	ApicWriteLocal(APIC_ERROR_REGISTER, INTERRUPT_LVTERROR);

	/* Clear errors after enabling */
	if (MaxLvt > 3)
		ApicWriteLocal(APIC_ESR, 0);
}

/* Resets Local Apic */
void ApicClear(void)
{
	int MaxLvt = 0;
	uint32_t Temp = 0;

	/* Get Max LVT */
	MaxLvt = ApicGetMaxLvt();

	/* Mask error lvt */
	if (MaxLvt >= 3)
		ApicWriteLocal(APIC_ERROR_REGISTER, INTERRUPT_LVTERROR | APIC_MASKED);

	/* Mask these before deasserting */
	Temp = ApicReadLocal(APIC_TIMER_VECTOR);
	ApicWriteLocal(APIC_TIMER_VECTOR, Temp | APIC_MASKED);
	Temp = ApicReadLocal(APIC_LINT0_REGISTER);
	ApicWriteLocal(APIC_LINT0_REGISTER, Temp | APIC_MASKED);
	Temp = ApicReadLocal(APIC_LINT1_REGISTER);
	ApicWriteLocal(APIC_LINT1_REGISTER, Temp | APIC_MASKED);
	if (MaxLvt >= 4) {
		Temp = ApicReadLocal(APIC_PERF_MONITOR);
		ApicWriteLocal(APIC_PERF_MONITOR, Temp | APIC_MASKED);
	}

	/* Clean out APIC */
	ApicWriteLocal(APIC_TIMER_VECTOR, APIC_MASKED);
	ApicWriteLocal(APIC_LINT0_REGISTER, APIC_MASKED);
	ApicWriteLocal(APIC_LINT1_REGISTER, APIC_MASKED);
	if (MaxLvt >= 3)
		ApicWriteLocal(APIC_ERROR_REGISTER, APIC_MASKED);
	if (MaxLvt >= 4)
		ApicWriteLocal(APIC_PERF_MONITOR, APIC_MASKED);

	/* Integrated APIC (!82489DX) ? */
	if (ApicIsIntegrated()) {
		if (MaxLvt > 3)
			/* Clear ESR due to Pentium errata 3AP and 11AP */
			ApicWriteLocal(APIC_ESR, 0);
		ApicReadLocal(APIC_ESR);
	}
}

/* Enables the Local Aic */
void ApicEnable(void)
{
	uint32_t Temp = 0;

	/* Enable local apic */
	Temp = ApicReadLocal(APIC_SPURIOUS_REG);
	Temp &= ~(0x000FF);
	Temp |= 0x100;

#ifdef _X86_32
	/* This reduces some problems with to fast
	* interrupt mask/unmask */
	Temp &= ~(0x200);
#endif

	/* Set spurious vector */
	Temp |= INTERRUPT_SPURIOUS;

	/* Enable! */
	ApicWriteLocal(APIC_SPURIOUS_REG, Temp);
}

/* Setup Apic on Bsp */
void ApicInitBoot(void)
{
	/* Vars */
	uint32_t BspApicId = 0;
	uint32_t Temp = 0;
	int i = 0, j = 0;

	/* Disable IMCR if present (to-do..) */
	outb(0x22, 0x70);
	outb(0x23, 0x1);

	/* Get Apic Id */
	BspApicId = (ApicReadLocal(APIC_PROCESSOR_ID) >> 24) & 0xFF;

	/* Get bootstrap CPU */
	GlbBootstrapCpuId = (uint8_t)BspApicId;

	/* Initial Setup */
	ApicInitialSetup(BspApicId);

	/* Install Apic Handlers */
	InterruptInstallIdtOnly(0xFFFFFFFF, INTERRUPT_SPURIOUS7, ApicSpuriousHandler, NULL);
	InterruptInstallIdtOnly(0xFFFFFFFF, INTERRUPT_SPURIOUS, ApicSpuriousHandler, NULL);
	InterruptInstallIdtOnly(0xFFFFFFFF, INTERRUPT_LVTERROR, ApicErrorHandler, NULL);

	/* Enable the LAPIC */
	ApicEnable();

	/* Setup LVT0 & LVT1 */
	ApicSetupLvt(BspApicId, 0);
	ApicSetupLvt(BspApicId, 1);

	/* Finish */
	ApicSetupESR();

#ifdef _X86_32
	/* Disable Apic Timer */
	Temp = ApicReadLocal(APIC_TIMER_VECTOR);
	Temp |= (APIC_MASKED | INTERRUPT_TIMER);
	ApicWriteLocal(APIC_TIMER_VECTOR, Temp);
#endif

	/* Done! Enable interrupts */
	printf("    * Enabling interrupts...\n");
	InterruptEnable();

	/* Kickstart things */
	ApicSendEoi(0, 0);
}

/* Setup Apic on Ap */
void ApicInitAp(void)
{
	/* Vars */
	uint32_t Temp = 0;
	uint32_t ApicApId = 0;
	int i = 0, j = 0;
	ApicApId = (ApicReadLocal(APIC_PROCESSOR_ID) >> 24) & 0xFF;

	/* Initial Setup */
	ApicInitialSetup(ApicApId);

	/* Enable the LAPIC */
	ApicEnable();

	/* Setup LVT0 and LVT1 */
	ApicSetupLvt(ApicApId, 0);
	ApicSetupLvt(ApicApId, 1);

	/* Finish */
	ApicSetupESR();

	/* Set divider */
	ApicWriteLocal(APIC_DIVIDE_REGISTER, APIC_TIMER_DIVIDER_1);

	/* Setup timer */
	ApicWriteLocal(APIC_INITIAL_COUNT, GlbTimerQuantum * 20);

	/* Enable timer in one-shot mode */
	ApicWriteLocal(APIC_TIMER_VECTOR, INTERRUPT_TIMER);		//0x20000 - Periodic
}

/* Enable Local Apic Timer
 * Should be enabled after timers
 * and interrupts must be enabled */
void ApicTimerInit(void)
{
	/* Vars */
	uint32_t TimerTicks = 0;

	/* Zero out */
	memset((void*)GlbTimerTicks, 0, sizeof(GlbTimerTicks));

	/* Install Interrupt */
	InterruptInstallIdtOnly(0xFFFFFFFF, INTERRUPT_TIMER, ApicTimerHandler, NULL);

	/* Setup initial local apic timer registers */
	ApicWriteLocal(APIC_TIMER_VECTOR, INTERRUPT_TIMER);
	ApicWriteLocal(APIC_DIVIDE_REGISTER, APIC_TIMER_DIVIDER_1);

	/* Start counters! */
	ApicWriteLocal(APIC_INITIAL_COUNT, 0xFFFFFFFF); /* Set counter to -1 */

	/* Stall, we have no other threads running! */
	StallMs(100);

	/* Stop counter! */
	ApicWriteLocal(APIC_TIMER_VECTOR, 0x10000);

	/* Calculate bus frequency */
	TimerTicks = (0xFFFFFFFF - ApicReadLocal(APIC_CURRENT_COUNT));
	printf("    * Ticks: %u\n", TimerTicks);
	GlbTimerQuantum = (TimerTicks / 100) + 1;

	/* We want a minimum of ca 400, this is to ensure on "slow"
	* computers we atleast get a few ms of processing power */
	if (GlbTimerQuantum < 400)
		GlbTimerQuantum = 400;

	printf("    * Quantum: %u\n", GlbTimerQuantum);

	/* Reset divider to make sure */
	ApicWriteLocal(APIC_DIVIDE_REGISTER, APIC_TIMER_DIVIDER_1);

	/* Reset Timer Tick */
	ApicWriteLocal(APIC_INITIAL_COUNT, GlbTimerQuantum * 20);

	/* Re-enable timer in one-shot mode */
	ApicWriteLocal(APIC_TIMER_VECTOR, INTERRUPT_TIMER);		//0x20000 - Periodic
}
