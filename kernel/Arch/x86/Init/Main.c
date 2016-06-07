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
#include <Apic.h>
#include <AcpiSys.h>
#include <Log.h>

/* Extern, this function is declared in the MCore project
 * and all platform libs should enter this function */
MCoreBootInfo_t x86BootInfo;
extern void MCoreInitialize(MCoreBootInfo_t*);

/* Externs */
extern x86CpuObject_t GlbBootCpuInfo;

/* Inititalizes ACPI and the Apic */
void InitAcpiAndApic(void)
{
	/* Info */
	LogInformation("APIC", "Initializing");

	/* Initialize the APIC (if present) */
	if (!(GlbBootCpuInfo.EdxFeatures & CPUID_FEAT_EDX_APIC))
	{
		/* Bail out */
		LogFatal("APIC", "Not Present");
		Idle();
	}

	/* Enumerate Acpi */
	AcpiEnumerate();

	/* Init */
	ApicInitBoot();

	/* Setup Full APICPA */
	AcpiSetupFull();

	/* Scan ACPI Bus */
	AcpiScan();
}

/* Installs Timers */
void InitTimers(void)
{
	/* Setup Timers */
	DevicesInitTimers();

	/* Init Apic Timers */
	ApicTimerInit();

	/* Boot Cores */
	CpuSmpInit();
}

/* Used for initializing base components */
void HALInit(void *BootInfo, MCoreBootDescriptor *Descriptor)
{
	/* Initialize Video */
	VideoInit(BootInfo);

	/* Print */
	LogInformation("HAL0", "Initializing");

	/* Setup Gdt */
	GdtInit();
	
	/* Setup Idt */
	IdtInit();

	/* Setup Interrupts */
	InterruptInit();

	/* Memory setup! */
	LogInformation("HAL0", "Setting Up Memory");
	MmPhyiscalInit(BootInfo, Descriptor);
	MmVirtualInit();
}

/* Median entry between the x86 */
void InitX86(Multiboot_t *BootInfo, MCoreBootDescriptor *BootDescriptor)
{
	/* Setup Boot Info */
	x86BootInfo.ArchBootInfo = (void*)BootInfo;
	x86BootInfo.BootloaderName = (char*)BootInfo->BootLoaderName;
	
	/* Setup Kern & Mod info */
	x86BootInfo.Descriptor.KernelAddress = BootDescriptor->KernelAddress;
	x86BootInfo.Descriptor.KernelSize = BootDescriptor->KernelSize;

	x86BootInfo.Descriptor.RamDiskAddress = BootDescriptor->RamDiskAddress;
	x86BootInfo.Descriptor.RamDiskSize = BootDescriptor->RamDiskSize;

	x86BootInfo.Descriptor.ExportsAddress = BootDescriptor->ExportsAddress;
	x86BootInfo.Descriptor.ExportsSize = BootDescriptor->ExportsSize;

	x86BootInfo.Descriptor.SymbolsAddress = BootDescriptor->SymbolsAddress;
	x86BootInfo.Descriptor.SymbolsSize = BootDescriptor->SymbolsSize;
	
	/* Setup Functions */
	x86BootInfo.InitHAL = HALInit;
	x86BootInfo.InitPostSystems = InitAcpiAndApic;
	x86BootInfo.InitTimers = InitTimers;

	/* Initialize Cpu */
	CpuInit();

	/* Call Entry Point */
	MCoreInitialize(&x86BootInfo);
}