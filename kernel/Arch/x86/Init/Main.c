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
 * MollenOS x86-32 Init Code.
 */

/* Includes */
#include <MollenOS.h>
#include <Arch.h>
#include <Multiboot.h>
#include <Memory.h>
#include <Gdt.h>
#include <Idt.h>
#include <Interrupts.h>
#include <stddef.h>
#include <stdio.h>
#include <Apic.h>
#include <SysTimers.h>

/* Extern, this function is declared in the MCore project
 * and all platform libs should enter this function */
MCoreBootInfo_t x86BootInfo;
extern void MCoreInitialize(MCoreBootInfo_t*);

/* Externs */
extern x86CpuObject_t GlbBootCpuInfo;

/* Enumerates the APIC */
extern void AcpiEnumerate(void);

/* Initializes FULL access
* across ACPICA */
extern void AcpiSetupFull(void);

/* Inititalizes ACPI and the Apic */
void InitAcpiAndApic(void)
{
	/* Initialize the APIC (if present) */
	if (!(GlbBootCpuInfo.EdxFeatures & CPUID_FEAT_EDX_APIC))
	{
		/* Bail out */
	}

	/* Enumerate Acpi */
	printf("  - Initializing ACPI Systems\n");
	AcpiEnumerate();

	/* Init */
	ApicInitBoot();

	/* Setup Full APICPA */
	AcpiSetupFull();
}

/* Installs Timers */
void InitTimers(void)
{
	/* Setup Timers */
	TimerManagerInit();

	/* Init Apic Timers */
	printf("    * Setting up local timer\n");
	ApicTimerInit();
}

/* Used for initializing base components */
void HALInit(void *BootInfo)
{
	_CRT_UNUSED(BootInfo);

	/* Setup Gdt */
	GdtInit();
	
	/* Setup Idt */
	IdtInit();

	/* Setup Interrupts */
	InterruptInit();

	/* Memory setup! */
	printf("  - Setting up memory systems\n");
	printf("    * Physical Memory Manager...\n");
	MmPhyiscalInit(x86BootInfo.ArchBootInfo, x86BootInfo.KernelSize, x86BootInfo.RamDiskSize);
	printf("    * Virtual Memory Manager...\n");
	MmVirtualInit();
}

void InitX86(Multiboot_t *BootInfo, size_t KernelSize)
{
	/* Setup Boot Info */
	x86BootInfo.ArchBootInfo = (void*)BootInfo;
	x86BootInfo.BootloaderName = (char*)BootInfo->BootLoaderName;
	
	/* Setup Kern & Mod info */
	x86BootInfo.KernelSize = KernelSize;
	x86BootInfo.RamDiskAddr = BootInfo->ModuleAddr;
	x86BootInfo.RamDiskSize = BootInfo->ModuleCount;
	
	/* Setup Functions */
	x86BootInfo.InitHAL = HALInit;
	x86BootInfo.InitPostSystems = InitAcpiAndApic;
	x86BootInfo.InitTimers = InitTimers;

	/* Call Entry Point */
	MCoreInitialize(&x86BootInfo);
}